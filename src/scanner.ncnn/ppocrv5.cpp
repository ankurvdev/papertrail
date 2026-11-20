// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "ppocrv5.h"

#include "cpu.h"
#include "net.h"

#include <algorithm>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "ppocrv5_dict.h"

// #include "myfontface.h"

static float ContourScore(cv::Mat const& binary, std::vector<cv::Point> const& contour)
{
    cv::Rect rect = cv::boundingRect(contour);
    rect.x        = std::max(rect.x, 0);
    rect.y        = std::max(rect.y, 0);
    if (rect.x + rect.width > binary.cols) rect.width = binary.cols - rect.x;
    if (rect.y + rect.height > binary.rows) rect.height = binary.rows - rect.y;

    cv::Mat binROI = binary(rect);

    cv::Mat                mask = cv::Mat::zeros(rect.height, rect.width, CV_8U);
    std::vector<cv::Point> roiContour;
    for (auto i : contour)
    {
        cv::Point pt = cv::Point(i.x - rect.x, i.y - rect.y);
        roiContour.push_back(pt);
    }

    std::vector<std::vector<cv::Point>> roiContours = {roiContour};
    cv::fillPoly(mask, roiContours, cv::Scalar(255));

    float score = static_cast<float>(cv::mean(binROI, mask).val[0]);
    return score / 255.f;
}

static cv::Mat GetRotateCropImage(cv::Mat const& rgb, Object const& object)
{
    int const   orientation = object.orientation;
    float const rw          = object.rrect.size.width;
    float const rh          = object.rrect.size.height;

    int const   targetHeight = 48;
    float const targetWidth  = rh * targetHeight / rw;

    // warpperspective shall be used to rotate the image
    // but actually they are all rectangles, so warpaffine is almost enough  :P

    cv::Mat dst;

    cv::Point2f corners[4];
    object.rrect.points(corners);

    if (orientation == 0)
    {
        // horizontal text
        // corner points order
        //  0--------1
        //  |        |rw  -> as angle=90
        //  3--------2
        //      rh

        std::vector<cv::Point2f> srcPts(3);
        srcPts[0] = corners[0];
        srcPts[1] = corners[1];
        srcPts[2] = corners[3];

        std::vector<cv::Point2f> dstPts(3);
        dstPts[0] = cv::Point2f(0, 0);
        dstPts[1] = cv::Point2f(targetWidth, 0);
        dstPts[2] = cv::Point2f(0, targetHeight);

        cv::Mat tm = cv::getAffineTransform(srcPts, dstPts);

        cv::warpAffine(
            rgb, dst, tm, cv::Size(static_cast<int>(targetWidth), static_cast<int>(targetHeight)), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    else
    {
        // vertial text
        // corner points order
        //  1----2
        //  |    |
        //  |    |
        //  |    |rh  -> as angle=0
        //  |    |
        //  |    |
        //  0----3
        //    rw

        std::vector<cv::Point2f> srcPts(3);
        srcPts[0] = corners[2];
        srcPts[1] = corners[3];
        srcPts[2] = corners[1];

        std::vector<cv::Point2f> dstPts(3);
        dstPts[0] = cv::Point2f(0, 0);
        dstPts[1] = cv::Point2f(targetWidth, 0);
        dstPts[2] = cv::Point2f(0, targetHeight);

        cv::Mat tm = cv::getAffineTransform(srcPts, dstPts);

        cv::warpAffine(
            rgb, dst, tm, cv::Size(static_cast<int>(targetWidth), static_cast<int>(targetHeight)), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }

    return dst;
}

PPOCRv5::PPOCRv5() : targetSize(640)
{}

PPOCRv5::~PPOCRv5() = default;

int PPOCRv5::Load(char const*           detParampath,
                  char const*           detModelpath,
                  char const*           recParampath,
                  char const*           recModelpath,
                  [[maybe_unused]] bool useFp16,
                  [[maybe_unused]] bool useGpu)
{
    ppocrv5Det.clear();
    ppocrv5Rec.clear();

    ppocrv5Det.opt.use_fp16_packed     = useFp16;
    ppocrv5Det.opt.use_fp16_storage    = useFp16;
    ppocrv5Det.opt.use_fp16_arithmetic = useFp16;

#if NCNN_VULKAN
    ppocrv5_det.opt.use_vulkan_compute = useGpu;
#endif

    ppocrv5Det.load_param(detParampath);
    ppocrv5Det.load_model(detModelpath);

    // default to 1 thread, as we rec multiple lines in parallel
    ppocrv5Rec.opt.num_threads = 1;

    ppocrv5Rec.opt.use_fp16_packed     = useFp16;
    ppocrv5Rec.opt.use_fp16_storage    = useFp16;
    ppocrv5Rec.opt.use_fp16_arithmetic = useFp16;

#if NCNN_VULKAN
    ppocrv5_rec.opt.use_vulkan_compute = use_gpu;
#endif

    ppocrv5Rec.load_param(recParampath);
    ppocrv5Rec.load_model(recModelpath);

    return 0;
}
#if 0
int PPOCRv5::load(AAssetManager* mgr,
                  char const*    det_parampath,
                  char const*    det_modelpath,
                  char const*    rec_parampath,
                  char const*    rec_modelpath,
                  bool           use_fp16,
                  bool           use_gpu)
{
    ppocrv5_det.clear();
    ppocrv5_rec.clear();

    ppocrv5_det.opt.use_fp16_packed     = use_fp16;
    ppocrv5_det.opt.use_fp16_storage    = use_fp16;
    ppocrv5_det.opt.use_fp16_arithmetic = use_fp16;

#if NCNN_VULKAN
    ppocrv5_det.opt.use_vulkan_compute = use_gpu;
#endif

    ppocrv5_det.load_param(mgr, det_parampath);
    ppocrv5_det.load_model(mgr, det_modelpath);

    // default to 1 thread, as we rec multiple lines in parallel
    ppocrv5_rec.opt.num_threads = 1;

    ppocrv5_rec.opt.use_fp16_packed     = use_fp16;
    ppocrv5_rec.opt.use_fp16_storage    = use_fp16;
    ppocrv5_rec.opt.use_fp16_arithmetic = use_fp16;

#if NCNN_VULKAN
    ppocrv5_rec.opt.use_vulkan_compute = use_gpu;
#endif

    ppocrv5_rec.load_param(mgr, rec_parampath);
    ppocrv5_rec.load_model(mgr, rec_modelpath);

    return 0;
}
#endif

void PPOCRv5::SetTargetSize(int targetSizeIn)
{ targetSize = targetSizeIn; }

int PPOCRv5::Detect(cv::Mat const& rgb, std::vector<Object>& objects)
{
    cv::setNumThreads(ncnn::get_big_cpu_count());

    int imgW = rgb.cols;
    int imgH = rgb.rows;

    int const targetStride = 32;

    // letterbox pad to multiple of target_stride
    auto  w     = imgW;
    auto  h     = imgH;
    float scale = 1.f;
    if (std::max(w, h) > targetSize)
    {
        if (w > h)
        {
            scale = static_cast<float>(targetSize) / static_cast<float>(w);
            w     = targetSize;
            h     = (h * targetSize) / w;
        }
        else
        {
            scale = static_cast<float>(targetSize) / static_cast<float>(h);
            h     = targetSize;
            w     = (w * targetSize) / h;
        }
    }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(rgb.data, ncnn::Mat::PIXEL_RGB2BGR, imgW, imgH, w, h);

    int       wpad = ((w + targetStride - 1) / targetStride * targetStride) - w;
    int       hpad = ((h + targetStride - 1) / targetStride * targetStride) - h;
    ncnn::Mat inPad;
    ncnn::copy_make_border(in, inPad, hpad / 2, hpad - (hpad / 2), wpad / 2, wpad - (wpad / 2), ncnn::BORDER_CONSTANT, 114.f);

    float const meanVals[3] = {0.485f * 255.f, 0.456f * 255.f, 0.406f * 255.f};
    float const normVals[3] = {1 / 0.229f / 255.f, 1 / 0.224f / 255.f, 1 / 0.225f / 255.f};
    inPad.substract_mean_normalize(meanVals, normVals);

    ncnn::Extractor ex = ppocrv5Det.create_extractor();

    ex.input("in0", inPad);

    ncnn::Mat out;
    ex.extract("out0", out);

    float const denormVals[1] = {255.f};
    out.substract_mean_normalize(nullptr, denormVals);

    cv::Mat pred(out.h, out.w, CV_8UC1);
    out.to_pixels(pred.data, ncnn::Mat::PIXEL_GRAY);

    // threshold binary
    cv::Mat      bitmap;
    double const threshold = 0.3;
    cv::threshold(pred, bitmap, threshold * 255, 255, cv::THRESH_BINARY);

    // boxes from bitmap
    {
        // should use dbnet post process, but I think unclip process is difficult to write
        // so simply implement expansion. This may lose detection accuracy
        // original implementation can be referenced
        // https://github.com/MhLiao/DB/blob/master/structure/representers/seg_detector_representer.py

        float const boxThresh    = 0.6f;
        float const enlargeRatio = 1.95f;

        float const minSize       = 3 * scale;
        int const   maxCandidates = 1000;

        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i>              hierarchy;

        cv::findContours(bitmap, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        contours.resize(std::min(contours.size(), static_cast<size_t>(maxCandidates)));

        for (auto const& contour : contours)
        {
            if (contour.size() <= 2) continue;

            float score = ContourScore(pred, contour);
            if (score < boxThresh) continue;

            cv::RotatedRect rrect = cv::minAreaRect(contour);

            float rrectMaxwh = std::max(rrect.size.width, rrect.size.height);
            if (rrectMaxwh < minSize) continue;

            int orientation = 0;
            if (rrect.angle >= -30 && rrect.angle <= 30 && rrect.size.height > static_cast<float>(rrect.size.width) * 2.7f)
            {
                // vertical text
                orientation = 1;
            }
            if ((rrect.angle <= -60 || rrect.angle >= 60) && rrect.size.width > static_cast<float>(rrect.size.height) * 2.7f)
            {
                // vertical text
                orientation = 1;
            }

            if (rrect.angle < -30)
            {
                // make orientation from -90 ~ -30 to 90 ~ 150
                rrect.angle += 180;
            }
            if (orientation == 0 && rrect.angle < 30)
            {
                // make it horizontal
                rrect.angle += 90;
                std::swap(rrect.size.width, rrect.size.height);
            }
            if (orientation == 1 && rrect.angle >= 60)
            {
                // make it vertical
                rrect.angle -= 90;
                std::swap(rrect.size.width, rrect.size.height);
            }

            // enlarge
            rrect.size.height += rrect.size.width * (enlargeRatio - 1);
            rrect.size.width *= enlargeRatio;

            // adjust offset to original unpadded
            rrect.center.x    = (rrect.center.x - static_cast<float>(wpad / 2)) / scale;
            rrect.center.y    = (rrect.center.y - static_cast<float>(hpad / 2)) / scale;
            rrect.size.width  = rrect.size.width / scale;
            rrect.size.height = rrect.size.height / scale;

            Object obj;
            obj.rrect       = rrect;
            obj.orientation = orientation;
            obj.prob        = score;
            objects.push_back(obj);
        }
    }

    return 0;
}

int PPOCRv5::Recognize(cv::Mat const& rgb, Object& object)
{
    cv::setNumThreads(1);

    cv::Mat roi = GetRotateCropImage(rgb, object);

    ncnn::Mat in = ncnn::Mat::from_pixels(roi.data, ncnn::Mat::PIXEL_RGB2BGR, roi.cols, roi.rows);

    // ~/.paddlex/official_models/PP-OCRv5_mobile_rec/inference.yml
    float const meanVals[3] = {127.5f, 127.5f, 127.5f};
    float const normVals[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
    in.substract_mean_normalize(meanVals, normVals);

    ncnn::Extractor ex = ppocrv5Rec.create_extractor();

    ex.input("in0", in);

    ncnn::Mat out;
    ex.extract("out0", out);

    // 18385 x len
    int lastToken = 0;

    for (int i = 0; i < out.h; i++)
    {
        float const* p = out.row(i);

        int   index    = 0;
        float maxScore = -9999.f;
        for (int j = 0; j < out.w; j++)
        {
            float score = *p++;
            if (score > maxScore)
            {
                maxScore = score;
                index    = j;
            }
        }

        if (lastToken == index)    // CTC rule, if index is same as last one, they will be merged into one token
            continue;

        lastToken = index;

        if (index <= 0) continue;

        Character ch{};
        ch.id   = index - 1;
        ch.prob = maxScore;

        object.text.push_back(ch);
    }

    return 0;
}

int PPOCRv5::DetectAndRecognize(cv::Mat const& rgb, std::vector<Object>& objects)
{
    Detect(rgb, objects);

    // #pragma omp parallel for num_threads(ncnn::get_big_cpu_count()) schedule(dynamic)
    for (auto& object : objects) { Recognize(rgb, object); }

    return 0;
}
#if 0
int PPOCRv5::draw(cv::Mat& rgb, std::vector<Object> const& objects)
{
    static cv::Scalar const colors[] = {cv::Scalar(176, 39, 156),
                                        cv::Scalar(183, 58, 103),
                                        cv::Scalar(181, 81, 63),
                                        cv::Scalar(243, 150, 33),
                                        cv::Scalar(244, 169, 3),
                                        cv::Scalar(212, 188, 0),
                                        cv::Scalar(136, 150, 0),
                                        cv::Scalar(80, 175, 76),
                                        cv::Scalar(74, 195, 139),
                                        cv::Scalar(57, 220, 205),
                                        cv::Scalar(59, 235, 255),
                                        cv::Scalar(7, 193, 255),
                                        cv::Scalar(0, 152, 255),
                                        cv::Scalar(34, 87, 255),
                                        cv::Scalar(72, 85, 121),
                                        cv::Scalar(158, 158, 158),
                                        cv::Scalar(139, 125, 96)};

    for (size_t i = 0; i < objects.size(); i++)
    {
        Object const& obj = objects[i];

        cv::Scalar const& color = colors[i % 17];

        // fprintf(stderr, "%s %.5f at %.2f %.2f %.2f x %.2f  @ %.2f  =  ", obj.orientation == 0 ? "H" : "V", obj.prob,
        //         obj.rrect.center.x, obj.rrect.center.y, obj.rrect.size.width, obj.rrect.size.height, obj.rrect.angle);

        cv::Point2f corners[4];
        obj.rrect.points(corners);
        cv::line(rgb, corners[0], corners[1], color);
        cv::line(rgb, corners[1], corners[2], color);
        cv::line(rgb, corners[2], corners[3], color);
        cv::line(rgb, corners[3], corners[0], color);

        // std::string text;
        // for (size_t j = 0; j < objects[i].text.size(); j++)
        // {
        //     const Character& ch = objects[i].text[j];
        //     if (ch.id >= character_dict_size)
        //         continue;
        //
        //     text += character_dict[ch.id];
        // }
        // fprintf(stderr, "%s\n", text.c_str());
    }

    auto myfont = cv::FONT_HERSHEY_SIMPLEX;

    for (size_t i = 0; i < objects.size(); i++)
    {
        Object const& obj = objects[i];

        cv::Scalar const& color = colors[i % 17];

        std::string text;
        for (size_t j = 0; j < objects[i].text.size(); j++)
        {
            Character const& ch = objects[i].text[j];
            if (ch.id >= character_dict_size)
            {
                if (!text.empty() && text.back() != ' ') { text += " "; }
                continue;
            }

            if (obj.orientation == 0) { text += character_dict[ch.id]; }
            else
            {
                text += character_dict[ch.id];
                if (j + 1 < objects[i].text.size()) text += "\n";
            }
        }

        int      baseLine  = 0;
        int      font_size = (int)obj.rrect.size.width;
        cv::Size label_size;
        if (font_size < 6)
        {
            font_size  = 6;
            label_size = cv::getTextSize(rgb.size(), text, cv::Point(0, 0), myfont, font_size).size();
        }
        else if (obj.orientation == 0)
        {
            label_size = cv::getTextSize(rgb.size(), text, cv::Point(0, 0), myfont, font_size).size();
            while (font_size > 6 && (label_size.height > obj.rrect.size.width || label_size.width > obj.rrect.size.height))
            {
                font_size -= 1;
                label_size = cv::getTextSize(rgb.size(), text, cv::Point(0, 0), myfont, font_size).size();
            }
        }
        else    // if (obj.orientation == 1)
        {
            label_size = cv::getTextSize(rgb.size(), text, cv::Point(0, 0), myfont, font_size).size();
            while (font_size > 6 && (label_size.width > obj.rrect.size.height || label_size.height > obj.rrect.size.height))
            {
                font_size -= 1;
                label_size = cv::getTextSize(rgb.size(), text, cv::Point(0, 0), myfont, font_size).size();
            }
        }

        int x = obj.rrect.center.x - label_size.width / 2;
        int y = obj.rrect.center.y - label_size.height / 2 - baseLine;
        if (y < 0) y = 0;
        if (y + label_size.height > rgb.rows) y = rgb.rows - label_size.height;
        if (x < 0) x = 0;
        if (x + label_size.width > rgb.cols) x = rgb.cols - label_size.width;

        cv::rectangle(
            rgb, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)), cv::Scalar(255, 255, 255), -1);

        if (obj.orientation == 0) { cv::putText(rgb, text, cv::Point(x, y + label_size.height), cv::Scalar(0, 0, 0), myfont, font_size); }
        else
        {
            cv::putText(rgb, text, cv::Point(x, y + label_size.width), cv::Scalar(0, 0, 0), myfont, font_size);
        }
    }

    // cv::imshow("image", image);
    // cv::waitKey(0);

    return 0;
}
#endif
