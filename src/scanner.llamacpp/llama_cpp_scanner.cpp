#include "CommonMacros.h"

#include "Scanner.h"
#include <chrono>

SUPPRESS_WARNINGS_START
SUPPRESS_CLANG_WARNING("-Weverything")
SUPPRESS_GCC_WARNING("-Wunused-function")
#include <llama/arg.h>
#include <llama/common.h>
#include <llama/download.h>
#include <llama/llama-cpp.h>
#include <llama/log.h>
#include <llama/mtmd-helper.h>
#include <llama/mtmd.h>
#include <llama/sampling.h>
SUPPRESS_WARNINGS_END

#include <fmt/chrono.h>
#include <fmt/std.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// NOLINTBEGIN
using namespace std::string_view_literals;

struct mtmd_cli_context
{
    common_params                       params = init_common_params();
    mtmd::context_ptr                   ctx_vision;
    std::unique_ptr<common_init_result> llama_init{common_init_from_params(params)};

    llama_model*       model;
    llama_context*     lctx;
    llama_vocab const* vocab;
    common_sampler*    smpl;
    llama_batch        batch;
    int                n_batch;

    mtmd::bitmaps bitmaps;

    // chat template
    // /common_chat_templates_ptr tmpls;
    bool use_jinja = false;
    // TODO: support for --system-prompt with /clear command

    // support for legacy templates (models not having EOT token)
    llama_tokens antiprompt_tokens;

    int       n_threads = 1;
    llama_pos n_past    = 0;

    static void log_callback(enum ggml_log_level /* level */, char const* /* text */, void* /* user_data */) {}

    static common_params init_common_params()
    {
        common_params params{};
        params.warmup                    = false;
        params.sampling.temp             = 0.2f;     // lower temp by default for better quality
        params.n_ctx                     = 65536;    // 131072;
        params.n_predict                 = 4096;
        params.n_batch                   = 4096;
        params.cpuparams_batch.n_threads = 16;
        params.cpuparams.n_threads       = 16;

        params.prompt = "You are an OCR machine. Convert this image to plain text for indexing";

        // static constexpr auto mmproj_hf_project = "ggml-org"sv;
        // static constexpr auto mmproj_hf_rep     = "GLM-OCR-GGUF"sv;
        //  static constexpr auto mmproj_hf_file    = "mmproj-F16.gguf"sv;
        static constexpr auto model_hf_project = "ggml-org"sv;
        static constexpr auto model_hf_rep     = "GLM-OCR-GGUF"sv;
        // static constexpr auto model_hf_file     = "Qwen3-VL-30B-A3B-Instruct-Q4_K_M.gguf"sv;

        // params.mmproj.url = fmt::format("https://huggingface.co/{}/{}/resolve/main/{}", mmproj_hf_project, mmproj_hf_rep,
        // mmproj_hf_file);
        // params.mmproj.hf_repo = fmt::format("{}/{}", mmproj_hf_project, mmproj_hf_rep);
        // params.mmproj.hf_file = mmproj_hf_file;
        // params.mmproj.path = get_default_local_path(params.mmproj.url);

        // params.model.url     = fmt::format("https://huggingface.co/{}/{}/resolve/main/{}", model_hf_project, model_hf_rep,
        // model_hf_file);
        params.model.hf_repo = fmt::format("{}/{}", model_hf_project, model_hf_rep);
        // params.model.hf_file = model_hf_file;
        // params.model.path = get_default_local_path(params.model.url);
        common_download_opts opts{};
        opts.download_mmproj = true;
        opts.download_mtp    = true;
        opts.offline         = true;
        auto handler         = common_models_handler_init(params, LLAMA_EXAMPLE_SERVER);
        common_models_handler_apply(handler, params);
        return params;
    }

    mtmd_cli_context()
    {
        common_init();
        common_log_set_verbosity_thold(GGML_LOG_LEVEL_WARN);
        llama_log_set(log_callback, NULL);
        mtmd_log_set(log_callback, NULL);
        mtmd_helper_log_set(log_callback, NULL);

        model     = llama_init->model();
        lctx      = llama_init->context();
        vocab     = llama_model_get_vocab(model);
        n_threads = params.cpuparams.n_threads;

        n_batch = params.n_batch;

        if (!model || !lctx) { throw std::runtime_error("Failed to initialize model or context"); }

        if (!llama_model_chat_template(model, nullptr) && params.chat_template.empty())
        {
            throw std::runtime_error("Model does not have chat template.\n"
                                     "  For old llava models, you may need to use '--chat-template vicuna'\n"
                                     "  For MobileVLM models, use '--chat-template deepseek'\n"
                                     "  For Mistral Small 3.1, use '--chat-template mistral-v7'\n");
        }

        // tmpls     = common_chat_templates_init(model, params.chat_template);
        use_jinja = params.use_jinja;

        init_vision_context();

        // load antiprompt tokens for legacy templates
        if (params.chat_template == "vicuna") { antiprompt_tokens = common_tokenize(lctx, "ASSISTANT:", false, true); }
        else if (params.chat_template == "deepseek") { antiprompt_tokens = common_tokenize(lctx, "###", false, true); }
    }

    ~mtmd_cli_context() {}

    void init_vision_context()
    {
        char const*         clip_path = params.mmproj.path.c_str();
        mtmd_context_params mparams   = mtmd_context_params_default();
        mparams.use_gpu               = params.mmproj_use_gpu;
        mparams.print_timings         = true;
        mparams.n_threads             = params.cpuparams.n_threads;
        mparams.flash_attn_type       = params.flash_attn_type;
        mparams.image_min_tokens      = params.image_min_tokens;
        mparams.image_max_tokens      = params.image_max_tokens;
        ctx_vision.reset(mtmd_init_from_file(clip_path, model, mparams));
        if (!ctx_vision.get()) { throw std::runtime_error("Failed to load vision model from " + std::string(clip_path)); }
    }

    bool check_antiprompt(llama_tokens const& generated_tokens)
    {
        if (antiprompt_tokens.empty() || generated_tokens.size() < antiprompt_tokens.size()) { return false; }
        //
        return std::equal(
            generated_tokens.end() - static_cast<long>(antiprompt_tokens.size()), generated_tokens.end(), antiprompt_tokens.begin());
    }

    bool load_media(std::string const& fname)
    {
        mtmd::bitmap bmp(mtmd_helper_bitmap_init_from_file(ctx_vision.get(), fname.c_str(), false, {}).bitmap);
        if (!bmp.ptr) { return false; }
        bitmaps.entries.push_back(std::move(bmp));
        return true;
    }
};

static void generate_response(std::ostream& ostrm, mtmd_cli_context& ctx, int n_predict, int n_past)
{
    llama_tokens generated_tokens;
    for (int i = 0; i < n_predict; i++)
    {
        if (i > n_predict)
        {
            ostrm << "\n";
            break;
        }
        llama_token token_id = common_sampler_sample(ctx.smpl, ctx.lctx, -1);
        generated_tokens.push_back(token_id);
        common_sampler_accept(ctx.smpl, token_id, true);

        if (llama_vocab_is_eog(ctx.vocab, token_id) || ctx.check_antiprompt(generated_tokens))
        {
            ostrm << "\n";
            break;    // end of generation
        }
        ostrm << common_token_to_piece(ctx.lctx, token_id);
        // eval the token
        common_batch batch(ctx.lctx);
        batch.clear();
        batch.add(token_id, n_past++, {}, true);
        if (llama_decode(ctx.lctx, ctx.batch)) { throw std::runtime_error("Unable to decode generated token"); }
    }
}

static void eval_message(mtmd_cli_context& ctx, std::string const& msg, int& n_past)
{
    bool add_bos = true;
    // auto formatted_chat = chat_add_and_format(ctx, msg);

    mtmd_input_text text;
    text.text          = msg.c_str();
    text.add_special   = add_bos;
    text.parse_special = true;

    mtmd::input_chunks chunks(mtmd_input_chunks_init());
    auto               bitmaps_c_ptr = ctx.bitmaps.c_ptr();
    int32_t            res           = mtmd_tokenize(ctx.ctx_vision.get(),
                                                     chunks.ptr.get(),    // output
                                                     &text,               // text
                                                     bitmaps_c_ptr.data(),
                                                     bitmaps_c_ptr.size());
    if (res != 0)
    {    //
        throw std::runtime_error(fmt::format("Unable to tokenize prompt, res = {}", res));
    }

    ctx.bitmaps.entries.clear();

    llama_pos new_n_past = 0;
    if (mtmd_helper_eval_chunks(ctx.ctx_vision.get(),
                                ctx.lctx,            // lctx
                                chunks.ptr.get(),    // chunks
                                n_past,              // n_past
                                0,                   // seq_id
                                ctx.n_batch,         // n_batch
                                true,                // logits_last
                                &new_n_past))
    {
        throw std::runtime_error("Unable to eval prompt");
    }

    n_past = new_n_past;
}

// NOLINTEND
namespace
{
struct ScannerImpl : Scanner
{
    ScannerImpl(CommonOptions const& optsIn) : Scanner(optsIn)
    {
        maxThreads = 1;
        Start();
    }

    ~ScannerImpl() override = default;
    CLASS_DELETE_COPY_AND_MOVE(ScannerImpl);

    void ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir) override;

    mtmd_cli_context ctx;
};
}    // namespace

void ScannerImpl::ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir)
{
    auto outf    = outdir / "index.txt";
    auto outftmp = outf;
    outftmp.replace_extension(".tmp");
    fmt::print("Scanning file: {} to {} ...", fpath, outf);
    std::fflush(nullptr);
    auto startAt = std::chrono::steady_clock::now();
    struct RunContext    // NOLINT
    {
        RunContext(mtmd_cli_context& context FMT_LIFETIMEBOUND) : ctx(context)
        {
            ctx.smpl  = common_sampler_init(ctx.model, ctx.params.sampling);
            ctx.batch = llama_batch_init(1, 0, 1);    // batch for next token generation
        }
        ~RunContext()
        {
            llama_memory_clear(llama_get_memory(ctx.lctx), true);
            common_sampler_free(ctx.smpl);
            llama_batch_free(ctx.batch);
            ctx.batch = {};
            ctx.smpl  = nullptr;
        }
        mtmd_cli_context& ctx;    // NOLINT
    } runCtx{ctx};

    auto& params = ctx.params;
    if (!params.prompt.contains(mtmd_default_marker()))
    {
        for (size_t i = 0; i < params.image.size(); i++) { params.prompt += mtmd_default_marker(); }
    }

    if (!ctx.load_media(fpath.string())) { throw std::runtime_error(fmt::format("Failed to load media from {}", fpath)); }

    int nPast = ctx.n_past;
    eval_message(ctx, params.prompt, nPast);
    {
        std::ofstream ofs(outftmp, std::ios::out);
        if (!ofs.is_open()) { throw std::runtime_error(fmt::format("Unable to open output file: {}", outftmp)); }
        generate_response(ofs, ctx, params.n_predict, nPast);
    }
    std::filesystem::rename(outftmp, outf);
    auto endAt = std::chrono::steady_clock::now();
    fmt::print(" done in {}\n", std::chrono::duration_cast<std::chrono::milliseconds>(endAt - startAt));
}

std::unique_ptr<Scanner> Scanner::Create(CommonOptions const& opts)
{ return std::make_unique<ScannerImpl>(opts); }

// NOLINTEND(readability-magic-numbers)
