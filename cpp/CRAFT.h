#pragma once
#include "TextDetector.h"
#include "TorchModel.h"

class CraftModel : public TorchModel
{

    public:
    static HeatMapRatio      ResizeAspect(cv::Mat& img);
    static cv::Mat           Normalize(const cv::Mat& img);
    torch::Tensor            PreProcess(const cv::Mat& matInput);
    std::vector<BoundingBox> RunDetector(torch::Tensor& input, bool merge);
    // stores the last computed ratio (resize/rescale) from input image.
    float ratio;
};
