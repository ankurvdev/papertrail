#pragma once
#include "CommonMacros.h"

#include <opencv2/opencv.hpp>
#include <torch/script.h>
#include <torch/torch.h>

#include <filesystem>

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

struct TextDetector
{
    TextDetector()          = default;
    virtual ~TextDetector() = default;
    CLASS_DELETE_COPY_AND_MOVE(TextDetector);

    virtual std::vector<BoundingBox> Run(std::filesystem::path const& img) = 0;

    static std::unique_ptr<TextDetector> Create(torch::Device device);
};
