#include "CommonMacros.h"
#include "TextDetector.h"

#include <limits>
#include <opencv2/core/types.hpp>
#include <opencv2/opencv.hpp>
#include <torch/torch.h>

#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#define DEBUG_TRACE_TEXT_DETECTION 1

#if defined DEBUG_TRACE_TEXT_DETECTION
SUPPRESS_WARNINGS_START
SUPPRESS_CLANG_WARNING("-Wexit-time-destructors")
SUPPRESS_CLANG_WARNING("-Wglobal-constructors")
static inline thread_local std::string PredictionPtFilePath;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
SUPPRESS_WARNINGS_END
#endif

static constexpr int  ImageChunkHeight = 1200;    // Height at which to slice images vertically
static constexpr int  ImageChunkWidth  = 1200;
static constexpr auto TextThreshold    = 0.6f;     // Threshold for text detection (above this is considered text)
static constexpr auto BlankThreshold   = 0.35f;    // Threshold for blank space (below this is considered blank)
// static constexpr size_t MinParallelThreshold = 3;        // Minimum number of images before we parallelize
static constexpr int  MaxBatchSize        = 8;
static constexpr auto ImagenetDefaultMean = std::array{0.485f, 0.456f, 0.406f};
static constexpr auto ImagenetDefaultStd  = std::array{0.229f, 0.224f, 0.225f};

template <> struct fmt::formatter<cv::Point2i> : fmt::formatter<std::string_view>
{
    template <typename FormatContext> auto format(cv::Point2i const& item, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "[{}, {}]", item.x, item.y);
    }
};

[[maybe_unused]] static inline void WriteAsImage(torch::Tensor tensor3d, std::filesystem::path const& outf)
{
    auto sizes    = tensor3d.sizes();
    auto channels = sizes[0];
    auto height   = static_cast<int>(sizes[1]);
    auto width    = static_cast<int>(sizes[2]);

    if (channels != 3) { throw std::runtime_error("Require 3 channels on the image"); }
    tensor3d = tensor3d.permute({1, 2, 0}).clone();
    // tensor3d.mul_(torch::tensor({ImagenetDefaultStd})).add_(torch::tensor({ImagenetDefaultMean}));
    tensor3d = tensor3d * 255.0;    // NOLINT

    tensor3d = tensor3d.to(torch::kU8);    // Convert to unsigned 8-bit integer

    cv::Mat cvimage(height, width, CV_8UC3);

    // convert to [height, width, channel]
    std::memcpy(cvimage.data, tensor3d.data_ptr(), static_cast<size_t>(tensor3d.numel()));
    cv::cvtColor(cvimage, cvimage, cv::COLOR_RGB2BGR);

    // Save the image
    cv::imwrite(outf, cvimage);
}

template <typename TIn, typename TOut> [[maybe_unused]] static std::vector<TOut> TensorToVector(const torch::Tensor& tensor)
{
    auto              flat     = tensor.flatten().to(torch::TensorOptions().dtype<TIn>());
    auto              datasize = static_cast<size_t>(tensor.numel());
    auto              data     = std::span(flat.template data_ptr<TIn>(), datasize);
    std::vector<TOut> out(datasize);
    std::transform(std::begin(data), data.begin() + static_cast<int>(datasize), out.begin(), [](TIn elem) { return TOut(elem); });
    return out;
}

template <typename TLeftIterator, typename TRightIterator>
[[maybe_unused]] static inline auto TestCollectionsClose(TLeftIterator  leftBegin,
                                                         TLeftIterator  leftEnd,
                                                         TRightIterator rightBegin,
                                                         TRightIterator rightEnd,
                                                         float const    tolerance  = 3e-5f,
                                                         float const    absDiffMin = 1e-45f)
{

    using expectedValueType = typename TLeftIterator::value_type;
    using RhsType           = typename TRightIterator::value_type;

    struct Result
    {
        double            misMatchRatio{};
        double            skippedRatio{};
        expectedValueType maxTolerance{};
        expectedValueType maxDiff{};
        int               firstMismatch = -1;
    } result{.maxTolerance = tolerance};

    int numSkipped    = 0;
    int numMismatches = 0;
    int numTotal      = 0;

    int               mismatchIndex = -1;
    expectedValueType expectedValueMismatch{};
    RhsType           rhsMismatch{};

    if (std::distance(leftBegin, leftEnd) != std::distance(rightBegin, rightEnd))
    {
        throw std::runtime_error("expectedValue and rhs size does not match: " + std::to_string(std::distance(leftBegin, leftEnd))
                                 + "!=" + std::to_string(std::distance(rightBegin, rightEnd)));
    }

    for (int i = 0; leftBegin != leftEnd; ++leftBegin, ++rightBegin, ++i)
    {
        numTotal++;

        // To avoid division by 0, when the denominator is 0 the % delta is considered to be 100%.
        auto const absDiff      = abs(*leftBegin - *rightBegin);
        auto const leftPctDiff  = (FP_ZERO == fpclassify(*leftBegin)) ? 100.f : ((absDiff / abs(*leftBegin)) * 100.f);
        auto const rightPctDiff = (FP_ZERO == fpclassify(*rightBegin)) ? 100.f : ((absDiff / abs(*rightBegin)) * 100.f);

        // Perform a strong check on tolerance similar to what boost does.
        bool const isMismatch = (leftPctDiff > tolerance || rightPctDiff > tolerance);

        // Skip validating only when there's a mismatch and the delta is smaller than the provided threshold.
        if (isMismatch && absDiff < absDiffMin)
        {
            numSkipped++;
            continue;
        }

        // Count all the mismatches with a delta over the absDiffMin threshold and keep track of the first
        // mismatched pair of values.
        if (isMismatch)
        {
            numMismatches++;
            result.maxDiff      = std::max(result.maxDiff, absDiff);
            result.maxTolerance = std::max(result.maxTolerance, leftPctDiff);

            if (mismatchIndex < 0)
            {
                mismatchIndex         = i;
                expectedValueMismatch = *leftBegin;
                rhsMismatch           = *rightBegin;
            }
        }
    }
    result.misMatchRatio = static_cast<double>(numMismatches) / numTotal;
    result.skippedRatio  = static_cast<double>(numSkipped) / numTotal;
    result.firstMismatch = mismatchIndex;
    return result;
}

static std::pair<float, float>
GetDynamicThresholds(const cv::Mat& linemap, float textThreshold, float lowText, float typicalTop10Avg = 0.7f)    // NOLINT
{
    // Flatten the linemap
    auto flatMap = std::vector<float>(linemap.begin<float>(), linemap.end<float>());

    // Calculate number of pixels in top 10%
    auto top10Count = static_cast<unsigned>(static_cast<float>(flatMap.size()) * 0.9f);    // NOLINT
    // Sort the vector to find average intensity of the top 10%
    std::nth_element(flatMap.begin(), flatMap.begin() + top10Count, flatMap.end());
    float avgIntensity
        = std::accumulate(flatMap.begin() + top10Count, flatMap.end(), 0.0f) / static_cast<float>(flatMap.size() - top10Count);

    // Calculate scaling factor
    float scalingFactor = std::clamp(avgIntensity / typicalTop10Avg, 0.0f, 1.0f);
    scalingFactor       = std::sqrt(scalingFactor);    // Apply square root

    // Adjust thresholds
    lowText       = std::clamp(lowText * scalingFactor, 0.1f, 0.6f);           // NOLINT
    textThreshold = std::clamp(textThreshold * scalingFactor, 0.15f, 0.8f);    // NOLINT

    return {textThreshold, lowText};    // Return updated thresholds
}

static cv::Mat TorchTensorToMat(const at::Tensor& tensor)
{
    // Convert to numpy-like layout
    [[maybe_unused]] auto vec = TensorToVector<float, float>(tensor);
    cv::Mat               matImage(static_cast<int>(tensor.size(0)), static_cast<int>(tensor.size(1)), CV_32FC1, tensor.data_ptr<float>());

    return matImage;
}

static std::vector<PolygonBox>
DetectBoxes(const cv::Mat& linemap, cv::Size processorSize, cv::Size imageSize, float textThreshold, float lowText)
{

#if defined DEBUG_TRACE_TEXT_DETECTION
    torch::Tensor bboxftensor;
    torch::Tensor bboxitensor;
    torch::load(bboxftensor, (PredictionPtFilePath + ".trace.preds.bboxesf.pt"));
    torch::load(bboxitensor, PredictionPtFilePath + ".trace.preds.bboxesi.pt");
    auto refbboxesf = TensorToVector<float, float>(bboxftensor);
    auto refbboxesi = TensorToVector<int64_t, int>(bboxitensor);
#endif

    int imgH = linemap.rows;
    int imgW = linemap.cols;

    // Get dynamic thresholds (this function needs to be defined separately)
    std::tie(textThreshold, lowText) = GetDynamicThresholds(linemap, textThreshold, lowText);

    cv::Mat               textScoreComb = (linemap > static_cast<double>(lowText));    // Convert to binary image
    cv::Mat               labels;
    cv::Mat               stats;
    cv::Mat               centroids;
    [[maybe_unused]] auto flatMap = std::vector<uint8_t>(textScoreComb.begin<uint8_t>(), textScoreComb.end<uint8_t>());

    // Find connected components
    cv::connectedComponentsWithStats(textScoreComb, labels, stats, centroids, 4);
    int labelCount = stats.rows;

    std::vector<PolygonBox> det;
    float                   maxConfidence = 0.0f;
    cv::Size2f              scalef{static_cast<float>(imageSize.width) / static_cast<float>(processorSize.width),
                      static_cast<float>(imageSize.height) / static_cast<float>(processorSize.height)};

    for (int k = 1; k < labelCount; ++k)
    {
        int size = stats.at<int>(k, cv::CC_STAT_AREA);
        if (size < 10)    // NOLINT
        {
            continue;    // Size filtering
        }

        int x = stats.at<int>(k, cv::CC_STAT_LEFT);
        int y = stats.at<int>(k, cv::CC_STAT_TOP);
        int w = stats.at<int>(k, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(k, cv::CC_STAT_HEIGHT);

        int niter  = static_cast<int>(std::sqrt(std::min(w, h)));
        int buffer = 1;
        int sx     = std::max(0, x - niter - buffer);
        int sy     = std::max(0, y - niter - buffer);
        int ex     = std::min(imgW, x + w + niter + buffer);
        int ey     = std::min(imgH, y + h + niter + buffer);

        cv::Mat mask = (labels(cv::Rect(sx, sy, ex - sx, ey - sy)) == k);
        cv::Mat selectedLinemap;
        linemap(cv::Rect(sx, sy, ex - sx, ey - sy)).copyTo(selectedLinemap, mask);

        double lineMaxD = std::numeric_limits<double>::quiet_NaN();
        cv::minMaxLoc(selectedLinemap, nullptr, &lineMaxD);

        auto lineMax = static_cast<float>(lineMaxD);
        if (lineMax < textThreshold)
        {
            continue;    // Thresholding
        }

        cv::Mat segmap = mask.clone();
        int     ksize  = buffer + niter;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksize, ksize));
        cv::dilate(segmap, segmap, kernel);

        std::vector<cv::Point2f> contours;
        std::vector<cv::Point>   nonZeroPoints;
        cv::findNonZero(segmap, nonZeroPoints);

        // Step 3: Adjust indices and create contour points
        contours.reserve(nonZeroPoints.size());
        for (const auto& point : nonZeroPoints)
        {
            contours.emplace_back(point.x + sx, point.y + sy);    // Adjust coordinates
        }

        cv::RotatedRect            rectangle = cv::minAreaRect(contours);
        std::array<cv::Point2f, 4> box;
        rectangle.points(box.data());

        // Align to rectangular shape if close to square
        auto bw       = cv::norm(box[0] - box[1]);
        auto bh       = cv::norm(box[1] - box[2]);
        auto boxRatio = static_cast<float>(std::max(bw, bh) / (std::min(bw, bh) + 1e-5));    // NOLINT
        if (std::abs(1.f - boxRatio) <= 0.1f)                                                // NOLINT
        {
            auto bl = std::ranges::min_element(contours, [](const auto& a, const auto& b) { return a.x < b.x; })->x;
            auto br = std::ranges::max_element(contours, [](const auto& a, const auto& b) { return a.x < b.x; })->x;
            auto bt = std::ranges::min_element(contours, [](const auto& a, const auto& b) { return a.y < b.y; })->y;
            auto bb = std::ranges::max_element(contours, [](const auto& a, const auto& b) { return a.y < b.y; })->y;
            box     = std::array<cv::Point2f, 4>{{{bl, bt}, {br, bt}, {br, bb}, {bl, bb}}};
        }
        // Order points in clockwise direction
        std::ranges::rotate(box, std::ranges::min_element(box, [](auto& a, auto& b) { return a.x + a.y < b.x + b.y; }));

        maxConfidence = std::max(maxConfidence, lineMax);

        det.emplace_back(box, scalef, imageSize, lineMax);
#if defined DEBUG_TRACE_TEXT_DETECTION
        {
            auto it = refbboxesf.begin() + static_cast<int>((det.size() - 1) * 8u);
            auto [pt1, pt2, pt3, pt4]
                = std::array<cv::Point2f, 4>{{{*(it), *(++it)}, {*(++it), *(++it)}, {*(++it), *(++it)}, {*(++it), *(++it)}}};
            auto diff = 0.f;
            diff += std::abs(pt1.x - box[0].x) + std::abs(pt1.y - box[0].y);
            diff += std::abs(pt2.x - box[1].x) + std::abs(pt2.y - box[1].y);
            diff += std::abs(pt3.x - box[2].x) + std::abs(pt3.y - box[2].y);
            diff += std::abs(pt4.x - box[3].x) + std::abs(pt4.y - box[3].y);
            if (diff > 0.1f)    // NOLINT
            {                   //
                throw std::logic_error("Found mismatch");
            }
        }
        {
            auto it   = refbboxesi.begin() + static_cast<int>((det.size() - 1) * 8u);
            auto pts  = std::array<cv::Point2i, 4>{{{*(it), *(++it)}, {*(++it), *(++it)}, {*(++it), *(++it)}, {*(++it), *(++it)}}};
            auto boxi = det.back().polygon;
            if (boxi != pts)
            {    //
                throw std::logic_error("Found mismatch");
            }
        }
#endif
    }

    if (maxConfidence > 0)
    {
        for (auto& d : det)
        {
            d.confidence /= maxConfidence;    // Normalize confidence scores
        }
    }
    return det;
}

static std::vector<PolygonBox> CleanBoxes(const std::vector<PolygonBox>& boxes)
{
    std::vector<PolygonBox> newBoxes;

    for (const auto& boxObj : boxes)
    {
        auto box = boxObj.Rect();
        if (box.width == 0 || box.height == 0) { continue; }

        bool contained = false;

        for (const auto& otherBoxObj : boxes)
        {
            if (otherBoxObj.polygon == boxObj.polygon)
            {
                continue;    // Skip the same box
            }

            const auto& otherBox = otherBoxObj.Rect();
            if (box == otherBox)
            {
                continue;    // Skip identical bounding boxes
            }

            if (box.x >= otherBox.x && box.y >= otherBox.y && (box.x + box.width) <= (otherBox.x + otherBox.width)
                && (box.y + box.height) <= (otherBox.y + otherBox.height))
            {
                contained = true;    // Current box is contained in another
                break;
            }
        }

        if (!contained)
        {
            newBoxes.push_back(boxObj);    // Keep the box if not contained
        }
    }

    return newBoxes;
}

static auto GetAndCleanBoxes(const cv::Mat& linemap, cv::Size processorSize, cv::Size imageSize, float textThreshold, float lowText)
{
    auto bboxes = DetectBoxes(linemap, processorSize, imageSize, textThreshold, lowText);
    return CleanBoxes(bboxes);
}

struct EfficientViTForSemanticSegmentation : TextDetector
{
    public:
    EfficientViTForSemanticSegmentation(torch::Device device, std::filesystem::path const& fpath) : _device(device)
    {
        std::ifstream ifs{fpath};
        _model = LoadModule_(device, ifs);
    }

    ~EfficientViTForSemanticSegmentation() override = default;

    CLASS_DELETE_COPY_AND_MOVE(EfficientViTForSemanticSegmentation);

    std::vector<PolygonBox> Run(std::filesystem::path const& image) override;

    private:
    static torch::jit::script::Module LoadModule_(torch::Device device, std::istream& strm)
    {
        // auto startModel = chrono::steady_clock::now();
        auto model = torch::jit::load(strm);
        model.to(device);
        model.eval();
        return model;
    }

    // Assuming logits is a 3D vector where logits[i][k] gives the heatmap for image i and channel k
    using Heatmap      = std::vector<std::vector<float>>;      // 2D heatmap
    using Logits       = std::vector<std::vector<Heatmap>>;    // 3D logits
    using SplitIndex   = std::vector<int>;
    using SplitHeights = std::vector<int>;

    static auto ProcessPredictions_(torch::Tensor const& logits, cv::Mat const& img)
    {
        // [numImages, Labels, height, width] => [labels, numImages * height, width]

        auto sizes     = logits.sizes();
        auto chunks    = sizes[0];
        auto numLabels = sizes[1];
        auto height    = static_cast<int>(sizes[2]);
        auto width     = static_cast<int>(sizes[3]);

        auto preds = logits.permute({1, 0, 2, 3}).reshape({numLabels, chunks * height, width}).slice(1, 0, img.rows);
#if defined DEBUG_TRACE_TEXT_DETECTION
        torch::save(preds, PredictionPtFilePath + ".trace.preds.pt");
#endif
        [[maybe_unused]] auto predSizes   = preds.sizes();
        [[maybe_unused]] auto heatMap     = preds[0].contiguous();
        [[maybe_unused]] auto affinityMap = preds[1].contiguous();
        [[maybe_unused]] auto bboxes      = GetAndCleanBoxes(
            TorchTensorToMat(heatMap), cv::Size(width, img.rows), cv::Size(img.cols, img.rows), TextThreshold, BlankThreshold);
#if defined DEBUG_TRACE_TEXT_DETECTION
        {
            std::ofstream bboxdump(PredictionPtFilePath + ".trace.preds.bbox.txt");
            for (auto const& p : bboxes) { bboxdump << fmt::format("poly = {}\n", fmt::join(p.polygon, ", ")); }
        }
#endif
        // auto   bboxes = GetBoundingBoxes(preds.permute({1, 2, 0}), TextThreshold, BlankThreshold, BlankThreshold);
        auto clone1 = img.clone();
        auto clone2 = img.clone();

        size_t count = 0;
        for (auto const& p : bboxes)
        {
            const cv::Scalar green(cv::Scalar(0, 255, 0));
            const cv::Scalar red(cv::Scalar(0, 255, 0));
            cv::rectangle(clone1, p.Rect(), red);
            cv::polylines(clone2, p.polygon, false, green);
            cv::putText(clone2,
                        std::to_string(count++),
                        (p.Rect().tl() + p.Rect().br()) / 2,
                        cv::FONT_HERSHEY_COMPLEX,
                        .6,    // NOLINT
                        red);
        }
#if defined DEBUG_TRACE_TEXT_DETECTION
        cv::imwrite(PredictionPtFilePath + ".trace.preds.bbox.overlay.png", clone1);
        cv::imwrite(PredictionPtFilePath + ".trace.preds.bbox.overlay2.png", clone2);

#endif
        return bboxes;
    }

    static torch::Tensor Process_(cv::Mat img)
    {
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        assert(img.type() == CV_8UC3);
        if (img.rows > (ImageChunkHeight * MaxBatchSize)) { throw std::runtime_error("Large Image size not implemented"); }
        auto numSplits = (img.rows + ImageChunkHeight - 1) / ImageChunkHeight;

        int                        top = 0;
        std::vector<torch::Tensor> tensors;
        for (int i = 0; i < numSplits; i++)
        {
            int height        = std::min(top + ImageChunkHeight, img.rows) - top;
            int paddingNeeded = ImageChunkHeight - height;
            img               = img(cv::Rect{0, top, img.cols, height});

            if (paddingNeeded > 0)
            {
                const cv::Scalar red(0, 0, 255);    // BGR color for red
                cv::Mat          padded;
                cv::copyMakeBorder(img, padded, 0, paddingNeeded, 0, 0, cv::BORDER_CONSTANT, red);
                img = padded;
            }
            {
                // This double resize actually necessary for downstream accuracy
                cv::Mat resized1;
                int     aspectRatioHeight = static_cast<int>(ImageChunkHeight * (ImageChunkWidth / static_cast<double>(img.cols)));
                cv::resize(img, resized1, {ImageChunkWidth, aspectRatioHeight}, 0, 0, cv::INTER_LANCZOS4);
                cv::Mat resized2;
                cv::resize(resized1, resized2, {ImageChunkWidth, ImageChunkHeight}, 0, 0, cv::INTER_LANCZOS4);
                img = resized2;
            }

            torch::Tensor converted = torch::zeros({img.rows, img.cols, 3}, torch::kU8);
            std::memcpy(converted.data_ptr(), img.data, static_cast<size_t>(converted.numel()));
            converted = converted.to(torch::kFloat32);
            converted = converted / 255.0f;    // NOLINT
            converted.sub_(torch::tensor({ImagenetDefaultMean})).div_(torch::tensor({ImagenetDefaultStd}));
            converted = converted.permute({2, 0, 1});
#if defined DEBUG_TRACE_TEXT_DETECTION1
            WriteAsImage(converted, "converted.jpg");
            torch::save(converted, "/home/ankurv/papertrail/cpp/libtorch_generated.pt");
            {
                torch::Tensor reftensor;
                torch::load(reftensor, "/home/ankurv/papertrail/cpp/libtorch_reference.pt");
                std::vector<float>    mine       = TensorToVector<float, float>(converted);
                std::vector<float>    ref        = TensorToVector<float, float>(reftensor);
                [[maybe_unused]] auto mismatches = TestCollectionsClose(mine.begin(), mine.end(), ref.begin(), ref.end());
                WriteAsImage(reftensor, "reference.jpg");
            }
#endif
            tensors.push_back(converted);
        }
        return torch::stack(tensors);
    }

    torch::Device              _device;
    torch::jit::script::Module _model;
};

std::vector<PolygonBox> EfficientViTForSemanticSegmentation::Run(std::filesystem::path const& image)
{
#if defined DEBUG_TRACE_TEXT_DETECTION
    PredictionPtFilePath = std::filesystem::absolute(image.parent_path()) / image.stem();
#endif
    auto img       = cv::imread(image, cv::IMREAD_COLOR);
    auto imgTensor = Process_(img);

    std::vector<torch::jit::IValue> testInputs;
    testInputs.emplace_back(imgTensor.to(_device));
    auto logits = _model.forward(testInputs).toTensor();

    logits = torch::nn::functional::interpolate(logits,
                                                torch::nn::functional::InterpolateFuncOptions()
                                                    .size(std::vector<int64_t>{ImageChunkWidth, ImageChunkHeight})
                                                    .mode(torch::kBilinear)
                                                    .align_corners(false));

    return ProcessPredictions_(logits, img);
}
std::unique_ptr<TextDetector> TextDetector::Create(torch::Device device)
{
    return std::make_unique<EfficientViTForSemanticSegmentation>(device,
                                                                 "/home/ankurv/papertrail/cpp/efficient_vit_for_semantic_segmentation.pt");
}
