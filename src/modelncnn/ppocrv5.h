#pragma once
#include <opencv2/opencv.hpp>

struct Character
{
    int id;
    float prob;
};

struct Object
{
    cv::RotatedRect rrect;
    int orientation;
    float prob;
    std::vector<Character> text;
};
 int detect_ppocrv5(const cv::Mat& bgr, std::vector<Object>& objects); //NOLINT