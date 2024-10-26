#include "CommonMacros.h"
#include "TextDetector.h"
#include <ATen/core/TensorBody.h>
#include <ATen/ops/stack.h>
#include <c10/core/Device.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <torch/enum.h>
#include <torch/nn/functional.h>
#include <torch/script.h>
#include <torch/serialize.h>
#include <torch/torch.h>
#include <torch/types.h>
#include <vector>

static constexpr int ImageChunkHeight = 1200;    // Height at which to slice images vertically
static constexpr int ImageChunkWidth  = 1200;
// static constexpr float  TextThreshold        = 0.6f;     // Threshold for text detection (above this is considered text)
// static constexpr float  BlankThreshold       = 0.35f;    // Threshold for blank space (below this is considered blank)
// static constexpr size_t MinParallelThreshold = 3;        // Minimum number of images before we parallelize
static constexpr int  MaxBatchSize        = 8;
static constexpr auto ImagenetDefaultMean = std::array{0.485f, 0.456f, 0.406f};
static constexpr auto ImagenetDefaultStd  = std::array{0.229f, 0.224f, 0.225f};

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
    auto              data     = flat.template data_ptr<TIn>();
    auto              datasize = static_cast<size_t>(tensor.numel());
    std::vector<TOut> out(datasize);
    std::transform(data, data + datasize, out.begin(), [](TIn elem) { return TOut(elem); });
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
    return result;
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

    std::vector<BoundingBox> Run(std::filesystem::path const& image) override
    {
        auto                            imgTensor = Process_(image);
        std::vector<torch::jit::IValue> testInputs;
        testInputs.emplace_back(imgTensor.to(_device));
        auto logits = _model.forward(testInputs).toTensor();
        logits      = torch::nn::functional::interpolate(logits,
                                                    torch::nn::functional::InterpolateFuncOptions()
                                                        .size(std::vector<int64_t>{ImageChunkWidth, ImageChunkHeight})
                                                        .mode(torch::kBilinear)
                                                        .align_corners(false));

        return {};
    }

    private:
    static torch::jit::script::Module LoadModule_(torch::Device device, std::istream& strm)
    {
        // auto startModel = chrono::steady_clock::now();
        auto model = torch::jit::load(strm);
        model.to(device);
        model.eval();
        return model;
    }

    static torch::Tensor Process_(std::filesystem::path const& fpath)
    {
        auto img = cv::imread(fpath, cv::IMREAD_COLOR);
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        if (img.type() != CV_8UC3)
        {
            throw std::runtime_error("Shit");
            // convert to CV_8UC3;
        }
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

            {
                // Split and pad last image
                // const auto scale = 1.0 / 255.0;
                // assert(img.channels() == 3);
                //  img.convertTo(img, CV_32FC3, scale);
            }
            torch::Tensor converted = torch::zeros({img.rows, img.cols, 3}, torch::kU8);
            std::memcpy(converted.data_ptr(), img.data, static_cast<size_t>(converted.numel()));
            converted = converted.to(torch::kFloat32);
            converted = converted / 255.0f;    // NOLINT
            converted.sub_(torch::tensor({ImagenetDefaultMean})).div_(torch::tensor({ImagenetDefaultStd}));
            converted = converted.permute({2, 0, 1});
#if defined TODO_VERIFY
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

std::unique_ptr<TextDetector> TextDetector::Create(torch::Device device)
{
    return std::make_unique<EfficientViTForSemanticSegmentation>(device, "/home/ankurv/papertrail/cpp/traced_det.pt");
}
