#pragma once

#include "Indexer.h"
#include "ParallelWorker.h"
#include "cli.h"
#include "utils.h"

#include <stencil/protocol_json.h>

#include <filesystem>
#include <utility>

template <> struct fmt::formatter<std::shared_ptr<cli::Item>> : fmt::formatter<std::string_view>
{

    // Formats the point p using the parsed format specification (presentation)
    // stored in this formatter.
    template <typename FormatContext> auto format(std::shared_ptr<cli::Item> const& item, FormatContext& ctx) const
    { return fmt::format_to(ctx.out(), "{}", Stencil::Json::Stringify(*item)); }
};

struct Scanner : ParallelWorker<cli::Item>
{
    struct ScannerListener : ::Scanner::Listener
    {
        void OnQueueItemCompleted(cli::Item& item) override { indexer->AddItem(item); }

        Scanner* that{};
        Indexer* indexer{};
    };

    Scanner(CommonOptions optsIn) : opts(std::move(optsIn))
    {
        listener.that    = this;
        listener.indexer = indexer.get();
        AttachListener(listener);
        indexer->Start();

        // for (auto const& scand : args.scan) { ScanPath(std::filesystem::path{scand.str()}); }
    }

    ~Scanner() override
    {
        WaitForFinish();
        indexer->WaitForFinish();
    }
    CLASS_DELETE_COPY_AND_MOVE(Scanner);

    [[nodiscard]] std::string_view WorkName() const override { return "ocr-scan"; }
    [[nodiscard]] std::string WorkDesc(cli::Item const& item) const override { return fmt::format("{}", Stencil::Json::Stringify(item)); }

    void ScanPath(std::filesystem::path const& fpath)
    {
        if (std::filesystem::is_directory(fpath))
        {
            for (auto const& entry : std::filesystem::directory_iterator(fpath)) { ScanPath(std::filesystem::path{entry.path()}); }
        }
        else
        {
            cli::Item item{};
            item.path = shared_string(fpath.string());
            AddItem(std::move(item));
        }
    }

    virtual bool Filter(cli::Item& /* item */) { return true; }

    virtual void ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir) = 0;
    // virtual void Process(std::filesystem::path& fpath) override;

    void Process(cli::Item& item) override
    {
        auto fpath = std::filesystem::path(item.path.str());

        if (fpath.extension() != ".jpg") { return; }

        auto hash = Sha256HashOfFile(fpath);
        item.hash = shared_string(hash);

        std::filesystem::path fileworkdir = opts.workDir / "scanned" / hash;
        if (!std::filesystem::is_directory(fileworkdir)) { std::filesystem::create_directories(fileworkdir); }

        bool scanned       = false;
        bool symlinkExists = false;

        for (auto const& dirf : std::filesystem::directory_iterator(fileworkdir))
        {
            if (!dirf.is_symlink()) { scanned = scanned || dirf.path().filename() == "index.txt"; }
            else if (!std::filesystem::exists(dirf)) { std::filesystem::remove(dirf); }
            else
            {
                symlinkExists = symlinkExists || std::filesystem::equivalent(dirf, fpath);
            }
        }
        if (!scanned)
        {    // TODO:
            ScanFile(fpath, fileworkdir);
        }

        if (!symlinkExists)
        {
            for (size_t i = 0; i < 50; i++)
            {
                auto lnk = fileworkdir / std::filesystem::path(fmt::format("file_{}", i));
                if (!std::filesystem::exists(lnk))
                {
                    std::filesystem::create_symlink(std::filesystem::absolute(fpath), lnk);
                    break;
                }
            }
        }
        indexer->AddItem(item);
    }

    static std::unique_ptr<Scanner> Create(CommonOptions const& opts);

    CommonOptions            opts;
    std::unique_ptr<Indexer> indexer = Indexer::Create(opts);
    ScannerListener          listener{};
};
