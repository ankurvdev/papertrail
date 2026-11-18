#pragma once
#include "CRAFT.h"
#include "TextDetector.h"
#include "TorchModel.h"

#include <opencv2/opencv.hpp>
#include <torch/script.h>
#include <torch/torch.h>

struct TextResult
{
    std::string text;
    float       confidence;
    PolygonBox  coords;
};

class CRNNModel : public TorchModel
{

    public:
    CRNNModel(std::string_view const& charactersstr);
    std::vector<TextResult> Recognize(std::vector<PolygonBox> const& dets, cv::Mat const& img);

    private:
    torch::Tensor PreProcess_(cv::Mat& det);
    torch::Tensor NormalizePad_(cv::Mat& processed, int maxWidth);
    std::string   GreedyDecode_(torch::Tensor& input);

    // stores the last computed ratio (resize/rescale) from input image.
    // float             _ratio{};
    std::vector<char> _characters;
};
