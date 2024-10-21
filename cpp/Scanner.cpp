#include "Scanner.h"

#include "CRAFT.h"
#include "CRNN.h"

#include <EmbeddedResource.h>
#include <torch/torch.h>

#include <filesystem>

// NOLINTBEGIN(readability-magic-numbers)

extern DECLARE_RESOURCE(models, CRAFT_detector_pt);
extern DECLARE_RESOURCE(models, traced_recog_pt);

using namespace torch::indexing;

Scanner::Scanner(cli::SearchArgs const& args) : _workDir(args.work_dir.str())
{
    for (auto const& scanf : args.file) { AddItem(std::filesystem::path{scanf.str()}); }
    for (auto const& scand : args.scan) { ScanDirectory(std::filesystem::path{scand.str()}); }
}

void Scanner::Process(std::filesystem::path& fpath)
{
    torch::NoGradGuard noGradGuard;
    c10::InferenceMode guard;
    CRNNModel          recognition;
    CraftModel         detection;

    // set to mimi Tesseract
    cv::setNumThreads(4);
    torch::set_num_threads(4);

    std::string det = LOAD_RESOURCE(models, CRAFT_detector_pt).data;
    std::string rec = LOAD_RESOURCE(models, traced_recog_pt).data;

    auto startModel = std::chrono::steady_clock::now();
    // Always check the model was loaded successully
    auto checkRec = recognition.LoadModel(rec);
    auto checkDet = detection.LoadModel(det);
    auto endModel = std::chrono::steady_clock::now();

    auto diff = endModel - startModel;
    std::cout << "MODEL TIME " << std::chrono::duration<double, std::milli>(diff).count() << " ms" << '\n';

    // CHECK IF BOTH MODEL LOADED SUCESSFULLY
    if (checkRec && checkDet)
    {
        int runs = 1;
        // Load in image into openCV Mat (bW or color)
        cv::Mat matInput = CraftModel::LoadMat(fpath, false, true).clone();
        // resizes input if we need to
        HeatMapRatio processed = CraftModel::ResizeAspect(matInput);
        cv::Mat      clone     = processed.img.clone();
        cv::Mat      grey      = processed.img.clone();
        grey.convertTo(grey, CV_8UC1);
        cv::cvtColor(grey, grey, cv::COLOR_BGR2GRAY);
        torch::Tensor tempTensor = detection.ConvertToTensor(grey.clone(), true, false).squeeze(0);
        clone.convertTo(clone, CV_8UC3);
        for (int i = 0; i < runs; i++)
        {

            torch::Tensor input = detection.PreProcess(processed.img.clone());
            auto          ss    = std::chrono::high_resolution_clock::now();
            // use custom algorithm for bounding box merging
            std::vector<BoundingBox> dets     = detection.RunDetector(input, true);
            int                      maxWidth = 0;
            std::vector<TextResult>  results  = recognition.Recognize(dets, grey, maxWidth);
            auto                     ee       = std::chrono::high_resolution_clock::now();
            auto                     difff    = ee - ss;
            int                      count    = 0;
            for (auto x : dets)
            {
                rectangle(clone, x.topLeft, x.bottomRight, cv::Scalar(0, 255, 0));
                putText(
                    clone, std::to_string(count), (x.bottomRight + x.topLeft) / 2, cv::FONT_HERSHEY_COMPLEX, .6, cv::Scalar(100, 0, 255));
                count++;
            }
            for (auto& result : results)
            {
                std::cout << "LOCATION: " << result.coords.topLeft << " " << result.coords.bottomRight << '\n';
                std::cout << "TEXT: " << result.text << '\n';
                std::cout << "CONFIDENCE " << result.confidence << '\n';
                std::cout << "################################################" << '\n';
            }
            cv::imwrite("../output-heatmap.jpg", clone);
            std::cout << "TOTAL INFERENCE TIME " << std::chrono::duration<double, std::milli>(difff).count() << " ms" << '\n';
        }
    }
}

int main(int argc, char const* const argv[])
try
{

    auto parseResult
        = Stencil::CLI::Parse<cli::SearchArgs>(argc - 1, &argv[1]);    // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (parseResult.helpRequested || !parseResult.success)
    {
        fmt::print(stderr, "{}\n", fmt::join(Stencil::CLI::GenerateHelp(parseResult.obj), "\n"));
        return parseResult.helpRequested ? 0 : -1;
    }
    Scanner session(parseResult.obj);
    session.Start();
    session.WaitForFinish();
    return 0;
} catch (std::exception const& ex)
{
    fmt::print("Exception: {}\n", ex.what());
    return -1;
}
// NOLINTEND(readability-magic-numbers)
