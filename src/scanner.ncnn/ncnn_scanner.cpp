#include "CommonMacros.h"

#include "Scanner.h"
#include <opencv2/imgcodecs.hpp>

SUPPRESS_WARNINGS_START
SUPPRESS_CLANG_WARNING("-Weverything")
SUPPRESS_GCC_WARNING("-Wunused-function")

#include "ppocrv5.h"
#include "ppocrv5_dict.h"
#include <opencv2/opencv.hpp>

SUPPRESS_WARNINGS_END

#include <fmt/chrono.h>
#include <fmt/std.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

// NOLINTBEGIN
using namespace std::string_view_literals;

// NOLINTEND

struct ScannerImpl : Scanner
{
    ScannerImpl(CommonOptions const& optsIn) : Scanner(optsIn)
    {
        auto detParamPath = optsIn.workDir / "models_ncnn" / "paddleocrv5" / "det" / "inference.ncnn.param";
        auto detModelPath = optsIn.workDir / "models_ncnn" / "paddleocrv5" / "det" / "inference.ncnn.bin";
        auto recParamPath = optsIn.workDir / "models_ncnn" / "paddleocrv5" / "rec" / "inference.ncnn.param";
        auto recModelPath = optsIn.workDir / "models_ncnn" / "paddleocrv5" / "rec" / "inference.ncnn.bin";

        ppocrv5.Load(detParamPath.c_str(), detModelPath.c_str(), recParamPath.c_str(), recModelPath.c_str(), false, false);
        maxThreads = 1;
        Start();

        // ppocrv5->set_target_size(sizetypes[(int)sizeid]);
    }

    ~ScannerImpl() override = default;
    CLASS_DELETE_COPY_AND_MOVE(ScannerImpl);

    void    ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir) override;
    PPOCRv5 ppocrv5;
};

// pip install paddlepaddle==3.0.0
// pip install paddleocr==3.0.0
// paddlex --install paddle2onnx
// paddleocr ocr -i test.png
// paddlex --paddle2onnx --paddle_model_dir ~/.paddlex/official_models/PP-OCRv5_mobile_det --onnx_model_dir PP-OCRv5_mobile_det
// paddlex --paddle2onnx --paddle_model_dir ~/.paddlex/official_models/PP-OCRv5_mobile_rec --onnx_model_dir PP-OCRv5_mobile_rec
// pnnx PP-OCRv5_mobile_det.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256]
// pnnx PP-OCRv5_mobile_rec.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256]
// pnnx PP-OCRv5_server_det.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256] fp16=0
// pnnx PP-OCRv5_server_rec.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256] fp16=0

void ScannerImpl::ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir)
{

    auto outf    = outdir / "index.txt";
    auto outftmp = outf;
    outftmp.replace_extension(".tmp");
    fmt::print("Scanning file: {} to {} ...", fpath, outf);
    std::fflush(nullptr);
    auto startAt = std::chrono::steady_clock::now();
    // load iamge
    cv::Mat image = cv::imread(fpath.string(), cv::IMREAD_COLOR_RGB);
    if (image.empty()) { throw std::runtime_error(fmt::format("Failed to load image from {}", fpath)); }
    std::vector<Object> objects;
    ppocrv5.DetectAndRecognize(image, objects);

    {
        std::ofstream ofs(outftmp, std::ios::out);
        if (!ofs.is_open()) { throw std::runtime_error(fmt::format("Unable to open output file: {}", outftmp)); }
        auto extractText = [](Object const& obj) {
            std::string text;
            for (size_t j = 0; j < obj.text.size(); j++)
            {
                Character const& ch = obj.text[j];
                if (ch.id >= CharacterDictSize)
                {
                    if (!text.empty() && text.back() != ' ') { text += " "; }
                    continue;
                }

                if (obj.orientation == 0) { text += CharacterDict[ch.id]; }
                else
                {
                    text += CharacterDict[ch.id];
                    if (j + 1 < obj.text.size()) text += "\n";
                }
            }
            fmt::print("{}\n", text);
            return text;
        };
        for (auto const& obj : objects)
        {
            auto str = extractText(obj);
            ofs << str << "\n";
        }
    }
    std::filesystem::rename(outftmp, outf);
    auto endAt = std::chrono::steady_clock::now();
    fmt::print(" done in {}\n", std::chrono::duration_cast<std::chrono::milliseconds>(endAt - startAt));
}

std::unique_ptr<Scanner> Scanner::Create(CommonOptions const& opts)
{ return std::make_unique<ScannerImpl>(opts); }

// NOLINTEND(readability-magic-numbers)
