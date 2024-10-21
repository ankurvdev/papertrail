#ifndef CRNN_H
#define CRNN_H
#include "CRAFT.h"
#include "TorchModel.h"
#include "string"
#include <opencv2/opencv.hpp>
#include <torch/script.h>
#include <torch/torch.h>
struct TextResult
{
    std::string text;
    float       confidence;
    BoundingBox coords;
};

class CRNNModel : public TorchModel
{

    public:
    CRNNModel();
    std::vector<TextResult> Recognize(std::vector<BoundingBox>& dets, cv::Mat& img, int& maxWidth);
    torch::Tensor           PreProcess(cv::Mat& det);
    torch::Tensor           NormalizePad(cv::Mat& processed, int maxWidth);
    std::string             GreedyDecode(torch::Tensor& input, int size);

    private:
    // stores the last computed ratio (resize/rescale) from input image.
    float             _ratio{};
    std::vector<char> _characters{};
};
#endif
