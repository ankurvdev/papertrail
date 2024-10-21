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
struct boxSorter
{
    bool operator()(const BoundingBox& a, const BoundingBox& b)
    {
        // Check if the boxes are on the same row
        if (std::abs(a.bottomRight.y - b.bottomRight.y) < 7) { return a.bottomRight.x < b.bottomRight.x; }
        // If the boxes are not on the same row, sort by their y-coordinate
        else { return a.bottomRight.y < b.bottomRight.y; }
    }
};

struct pointSorter
{
    bool operator()(const cv::Point& a, const cv::Point& b)
    {
        int sumA = a.x + a.y;
        int sumB = b.x + b.y;
        return sumA < sumB;
    }
};

class CraftModel : public TorchModel
{

    public:
    static HeatMapRatio             ResizeAspect(cv::Mat& img);
    static cv::Mat                  Normalize(const cv::Mat& img);
    static std::vector<BoundingBox> GetBoundingBoxes(const torch::Tensor& input,
                                                     const torch::Tensor& output,
                                                     float                textThresh = .7,
                                                     float                linkThresh = .4,
                                                     float                lowText    = .4);
    torch::Tensor                   PreProcess(const cv::Mat& matInput);
    static std::vector<BoundingBox> MergeBoundingBoxes(std::vector<BoundingBox>& dets, float distanceThresh, int height, int width);
    std::vector<BoundingBox>        RunDetector(torch::Tensor& input, bool merge);
    // stores the last computed ratio (resize/rescale) from input image.
    float ratio;
};
#endif
