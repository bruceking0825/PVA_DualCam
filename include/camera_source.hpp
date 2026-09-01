#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace pva
{
    class CameraSource
    {
    public:
        virtual ~CameraSource() = default;
        virtual bool open(std::string &error) = 0;
        virtual bool readStereo(cv::Mat &camera1, cv::Mat &camera2, std::string &error) = 0;
        virtual void close() = 0;
    };
} // namespace pva
