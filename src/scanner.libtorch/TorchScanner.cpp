#include "CommonMacros.h"

#include "CRAFT.h"
#include "CRNN.h"
#include "Scanner.h"
#include "TextDetector.h"
#include "cli.h"

#include <EmbeddedResource.h>
#include <torch/torch.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

// NOLINTBEGIN(readability-magic-numbers)

// extern DECLARE_RESOURCE(models, CRAFT_detector_pt);
// extern DECLARE_RESOURCE(models, traced_recog_pt);
extern DECLARE_RESOURCE(config, english_g2_characters_txt);

using namespace torch::indexing;

template <> struct fmt::formatter<cv::Point2i> : fmt::formatter<std::string_view>
{
    template <typename FormatContext> auto format(cv::Point2i const& item, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}:{}", item.x, item.y);
    }
};

template <> struct fmt::formatter<PolygonBox> : fmt::formatter<std::string_view>
{
    template <typename FormatContext> auto format(PolygonBox const& box, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", fmt::join(box.polygon, ","));
    }
};

struct ScannerImpl : Scanner
{
    ScannerImpl(CommonOptions const& optsIn) :
        Scanner(optsIn), detector(TextDetector::Create(torch::kCPU, opts.workDir / "text_detection.ptc"))
    {
        at::globalContext().setQEngine(at::QEngine::QNNPACK);
        recognition.LoadModel(opts.workDir / "traced-recog.pt");
    }

    CLASS_DELETE_COPY_AND_MOVE(ScannerImpl);

    void ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir) override;

    std::unique_ptr<TextDetector> detector;
    CRNNModel                     recognition = CRNNModel(LOAD_RESOURCE(config, english_g2_characters_txt).data);
};

static auto Scan(TextDetector& detector, CRNNModel& recognition, std::filesystem::path const& fpath, std::filesystem::path const& outdir)
try
{
    torch::NoGradGuard noGradGuard;
    c10::InferenceMode guard;

    auto bboxes = detector.Run(fpath);

    cv::Mat matInput = CraftModel::LoadMat(fpath, false, true).clone();
    // resizes input if we need to
    HeatMapRatio processed = CraftModel::ResizeAspect(matInput);
    cv::Mat      clone     = processed.img.clone();
    cv::Mat      grey      = processed.img.clone();
    grey.convertTo(grey, CV_8UC1);
    cv::cvtColor(grey, grey, cv::COLOR_BGR2GRAY);
    for (auto& bb : bboxes)
    {
        for (auto& p : bb.polygon)
        {
            p.x = static_cast<int>(std::round(static_cast<float>(p.x) * processed.ratio));
            p.y = static_cast<int>(std::round(static_cast<float>(p.y) * processed.ratio));
        }
    }
    auto          results = recognition.Recognize(bboxes, grey);
    std::ofstream content(outdir / "bboxes.craft_rcnn.txt");
    std::ofstream bboxf(outdir / "bboxes.craft_rcnn.tsv");

    for (auto& result : results)
    {
        fmt::print(bboxf, "{}\t{}\t{}\n", result.coords, result.confidence, result.text);
        fmt::print(content, "{}\n", result.text);
    }
} catch (std::exception const& ex) { fmt::print("Exception: {}\n", ex.what()); }

void ScannerImpl::ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir)
{
    Scan(*detector, recognition, fpath, outdir);
}

std::unique_ptr<Scanner> Scanner::Create(CommonOptions const& opts)
{
    return std::make_unique<ScannerImpl>(opts);
}

// NOLINTEND(readability-magic-numbers)
