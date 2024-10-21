#ifndef TORCHMODEL_H
#define TORCHMODEL_H
#include <c10/core/Device.h>
#include <c10/core/DeviceType.h>
#include <opencv2/opencv.hpp>
#include <torch/script.h>
#include <torch/torch.h>

#include <filesystem>

class TorchModel
{
    public:
    TorchModel();
    ~TorchModel() = default;
    bool           LoadModel(const std::filesystem::path& modelPath);
    torch::Tensor  Predict(const std::vector<torch::Tensor>& input);
    void           ChangeDevice(const torch::DeviceType& deviceSet, const int& index);
    torch::Tensor  ConvertToTensor(const cv::Mat& img, bool normalize = false, bool color = true);
    torch::Tensor  ConvertListToTensor(std::list<cv::Mat>& imgs);
    torch::Tensor  PredictTuple(const std::vector<torch::Tensor>& input);
    static cv::Mat ConvertToMat(const torch::Tensor& output, bool isFloat, bool permute, bool bgr, bool color);
    static cv::Mat LoadMat(std::filesystem::path const& file, bool grey, bool rgb);

    torch::jit::script::Module model;
    // Default device is CUDA, if avail
    torch::Device device = torch::kCUDA;
};
#endif
