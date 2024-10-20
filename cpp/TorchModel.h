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
    ~TorchModel();
    bool          loadModel(const std::filesystem::path& modelPath);
    torch::Tensor predict(const std::vector<torch::Tensor>& input);
    void          changeDevice(const torch::DeviceType& deviceSet, const int& index);
    torch::Tensor convertToTensor(const cv::Mat& img, bool normalize = false, bool color = true);
    torch::Tensor convertListToTensor(std::list<cv::Mat>& imgs);
    torch::Tensor predictTuple(const std::vector<torch::Tensor>& input);
    cv::Mat       convertToMat(const torch::Tensor& output, bool isFloat, bool permute, bool bgr, bool color);
    cv::Mat       loadMat(std::filesystem::path const& file, bool grey, bool rgb);

    torch::jit::script::Module model;
    // Default device is CUDA, if avail
    torch::Device device = torch::kCUDA;
};
#endif
