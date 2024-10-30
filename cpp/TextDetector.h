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
struct PolygonBox
{
    std::array<cv::Point2i, 4> polygon{};
    float                      confidence;

    constexpr PolygonBox(std::array<cv::Point2f, 4> const& polyf, cv::Size2f scale, cv::Size2i bounds, float confidenceIn) :
        confidence(confidenceIn)
    {
        std::array<cv::Point2i, 4> arr;
        for (size_t i = 0; i < polygon.size(); i++)
        {
            polygon[i].x = std::max(std::min(static_cast<int>(polyf[i].x * scale.width), bounds.width), 0);      // NOLINT
            polygon[i].y = std::max(std::min(static_cast<int>(polyf[i].y * scale.height), bounds.height), 0);    // NOLINT
        }
    }

    [[nodiscard]] auto Rect() const
    {

        auto compx = [](auto const& p1, auto const& p2) { return p1.x < p2.x; };
        auto compy = [](auto const& p1, auto const& p2) { return p1.y < p2.y; };
        auto minx  = *std::ranges::min_element(polygon, compx);
        auto miny  = *std::ranges::min_element(polygon, compy);
        auto maxx  = *std::ranges::max_element(polygon, compx);
        auto maxy  = *std::ranges::max_element(polygon, compy);

        cv::Rect2i rect;
        rect.x      = minx.x;
        rect.width  = maxx.x - minx.x;
        rect.y      = miny.y;
        rect.height = maxy.y - miny.y;
        return rect;
    }
};

std::vector<BoundingBox> GetBoundingBoxes(const torch::Tensor&    output,
                                          [[maybe_unused]] double textThresh = .7,    // NOLINT
                                          double                  linkThresh = .4,    // NOLINT
                                          double                  lowText    = .4);                       // NOLINT

struct TextDetector
{
    TextDetector()          = default;
    virtual ~TextDetector() = default;
    CLASS_DELETE_COPY_AND_MOVE(TextDetector);

    virtual std::vector<PolygonBox> Run(std::filesystem::path const& img) = 0;

    static std::unique_ptr<TextDetector> Create(torch::Device device);
};
