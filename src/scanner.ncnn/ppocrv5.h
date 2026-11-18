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

#ifndef PPOCRV5_H
#define PPOCRV5_H

#include <opencv2/core/core.hpp>

#include <net.h>

struct Character
{
    int   id;
    float prob;
};

struct Object
{
    cv::RotatedRect        rrect;
    int                    orientation{};
    float                  prob{};
    std::vector<Character> text;
};

class PPOCRv5
{
    public:
    PPOCRv5();
    ~PPOCRv5();

    int Load(char const* detParampath,
             char const* detModelpath,
             char const* recParampath,
             char const* recModelpath,
             bool        useFp16 = false,
             bool        useGpu  = false);
    // int load(AAssetManager* mgr, const char* det_parampath, const char* det_modelpath, const char* rec_parampath, const char*
    // rec_modelpath, bool use_fp16 = false, bool use_gpu = false);

    void SetTargetSize(int targetSize);

    int Detect(cv::Mat const& rgb, std::vector<Object>& objects);

    int Recognize(cv::Mat const& rgb, Object& object);

    int DetectAndRecognize(cv::Mat const& rgb, std::vector<Object>& objects);
    int Draw(cv::Mat& rgb, std::vector<Object> const& objects);

    protected:
    ncnn::Net ppocrv5Det;
    ncnn::Net ppocrv5Rec;
    int       targetSize;
};

#endif    // PPOCRV5_H
