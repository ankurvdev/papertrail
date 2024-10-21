#ifndef CRAFT_H
#define CRAFT_H
#include "TorchModel.h"
#include "string"
#include <opencv2/opencv.hpp>
#include <torch/script.h>
#include <torch/torch.h>

struct HeatMapRatio
{
    cv::Mat  img;
    cv::Size heatMapSize;
    float    ratio;
};
struct BoundingBox
{
    cv::Point topLeft;
    cv::Point bottomRight;
};

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
#endif
