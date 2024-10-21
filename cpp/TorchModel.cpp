
#include "TorchModel.h"

#include <sstream>

// NOLINTBEGIN(readability-magic-numbers)

TorchModel::TorchModel()
{

    if (torch::cuda::device_count() > 0)
    {
        torch::Device defaultDevice(torch::kCUDA, 0);
        _device = defaultDevice;
    }
    else
    {
        torch::Device defaultCpu = torch::kCPU;
        _device                  = defaultCpu;
    }
}

bool TorchModel::LoadModel(const std::string& data)    // NOLINT(readability-convert-member-functions-to-static)
{
    bool success = false;
    try
    {
        std::istringstream iss(data);
        // auto startModel = chrono::steady_clock::now();
        _model = torch::jit::load(iss);
        _model.to(_device);
        // auto endModel = chrono::steady_clock::now();
        // auto diff = endModel - startModel;
        // std::cout <<"MODEL TIME "<< chrono::duration <double, milli> (diff).count() << " ms"<<std::endl;
        _model.eval();
        success = true;

    } catch (std::exception& e)
    {
        std::cout << "ERRORS";
        std::cout << e.what();
    }
    return success;
}

torch::Tensor TorchModel::Predict(const std::vector<torch::Tensor>& input)
{
    torch::Tensor                   result = torch::empty({0}).to(_device);
    std::vector<torch::jit::IValue> testInputs;
    testInputs.reserve(input.size());
    for (const auto& x : input) { testInputs.emplace_back(x.to(_device)); }
    try
    {
        auto res = _model.forward(testInputs).toTensor();
        return res;

    }

    catch (std::exception& e)
    {
        std::cout << e.what() << '\n';
    }

    // Clears growing cuda cache and frees memory if process is interupted.
    // c10::cuda::CUDACachingAllocator::emptyCache();
    return result;
}

torch::Tensor TorchModel::PredictTuple(const std::vector<torch::Tensor>& input)
{
    torch::Tensor                   result = torch::empty({0}).to(_device);
    std::vector<torch::jit::IValue> testInputs;
    testInputs.reserve(input.size());
    for (auto const& x : input) { testInputs.emplace_back(x.to(_device)); }

    try
    {
        auto res = _model.forward(testInputs).toTuple()->elements()[0].toTensor();
        return res;

    }

    catch (std::exception& e)
    {
        std::cout << e.what() << '\n';
    }
    // Clears growing cuda cache and frees memory if process is interupted.
    // c10::cuda::CUDACachingAllocator::emptyCache();
    return result;
}
/*
void TorchModel::ChangeDevice(const torch::DeviceType& deviceSet, const size_t& index)
{
    auto deviceCount = torch::cuda::device_count();
    // MOVE model and all tensors created from now on to desired device
    if (deviceCount > 0 && deviceSet == torch::kCUDA)
    {
        if (index < deviceCount)
        {
            torch::Device dev(deviceSet, index);
            _device = dev;
            _model.to(_device);
        }
        else
        {
            // Trying to use a device thats not there, set to next available GPU
            torch::Device dev(deviceSet, deviceCount - 1);
            _device = dev;
            _model.to(_device);
        }
    }
    else
    // Set to CPU if there are no CUDA devices
    {
        torch::Device dev = torch::kCPU;
        _device           = dev;
        _model.to(device);
    }
}
 */
torch::Tensor TorchModel::ConvertToTensor(const cv::Mat& img, bool normalize, bool color)
{
    cv::Mat c = img.clone();
    if (color) { cv::cvtColor(c, c, cv::COLOR_BGR2RGB); }

    auto scale     = (normalize) ? 1.0 / 255.0 : 1.0;
    int  channels  = c.channels();
    auto colorRead = (channels == 3) ? CV_32FC3 : CV_32FC1;
    c.convertTo(c, colorRead, scale);

    torch::Tensor converted = torch::zeros({c.rows, c.cols, channels}, torch::kF32);
    std::memcpy(converted.data_ptr(), c.data, sizeof(float) * static_cast<size_t>(converted.numel()));

    // add color dimension if it is greyscale 1
    converted = converted.permute({2, 0, 1});

    // Add batch dimension
    converted = converted.unsqueeze(0).to(_device);

    return converted;
}

cv::Mat TorchModel::LoadMat(std::filesystem::path const& file, bool grey, bool /* rgb */)
{
    auto    readMode  = (grey) ? cv::IMREAD_GRAYSCALE : cv::IMREAD_COLOR;
    cv::Mat returnMat = cv::imread(file, readMode);
    return returnMat;
}

torch::Tensor TorchModel::ConvertListToTensor(std::list<cv::Mat>& imgs)
{

    // Initalize tensor with first image and pop it from list
    cv::Mat       first     = imgs.front();
    torch::Tensor converted = ConvertToTensor(first);
    imgs.pop_front();
    // Concat all images to a single tensor
    for (auto& img : imgs)
    {
        torch::Tensor next = ConvertToTensor(img);
        converted          = torch::cat({next, converted});
    }
    return converted.to(_device);
}

cv::Mat TorchModel::ConvertToMat(const torch::Tensor& output, bool isFloat, bool /* permute */, bool bgr, bool /* color */)
{
    torch::Tensor tensor = output.clone();
    tensor               = tensor.permute({1, 2, 0}).contiguous();
    // if float, image is range of 0 -> 1
    tensor            = (isFloat) ? tensor.mul(255).clamp(0, 255).to(torch::kU8) : tensor.to(torch::kU8);
    tensor            = tensor.to(torch::kCPU);
    int     height    = static_cast<int>(tensor.size(0));
    int     width     = static_cast<int>(tensor.size(1));
    int     channels  = static_cast<int>(tensor.size(2));
    auto    dataType  = (channels == 3) ? CV_8UC3 : CV_8UC1;
    cv::Mat outputMat = cv::Mat(cv::Size(width, height), dataType, tensor.data_ptr());
    if (bgr) { cv::cvtColor(outputMat, outputMat, cv::COLOR_RGB2BGR); }
    return outputMat.clone();
}

// NOLINTEND(readability-magic-numbers)