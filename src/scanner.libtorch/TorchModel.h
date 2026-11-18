#pragma once

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
    bool          LoadModel(std::filesystem::path const& fpath);
    torch::Tensor Predict(std::vector<torch::Tensor> const& input);
    // void           ChangeDevice(const torch::DeviceType& deviceSet, const size_t& index);
    torch::Tensor  ConvertToTensor(cv::Mat const& img, bool normalize = false, bool color = true);
    torch::Tensor  ConvertListToTensor(std::list<cv::Mat>& imgs);
    torch::Tensor  PredictTuple(std::vector<torch::Tensor> const& input);
    static cv::Mat ConvertToMat(torch::Tensor const& output, bool isFloat, bool permute, bool bgr, bool color);
    static cv::Mat LoadMat(std::filesystem::path const& file, bool grey, bool rgb);

    protected:
    torch::jit::script::Module _model;
    // Default device is CUDA, if avail
    torch::Device _device = torch::kCUDA;
};
