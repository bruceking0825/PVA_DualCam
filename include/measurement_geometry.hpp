#pragma once
#include "models.hpp"
namespace pva
{
    struct MeasurementRois
    {
        // 领域 ROI 使用像素与角度单位，坐标均对应当前视图中的原始图像像素。
        std::optional<std::array<double, 8>> diameter;
        std::optional<std::array<double, 6>> melt;
        std::optional<std::array<double, 3>> dip;
    };

    struct GrayStats { double average{}, maximum{}, minimum{}; };
    cv::Rect meltRoiRect(const std::array<double, 6> &values, int camera, cv::Size size);
    cv::Rect effectiveAutoExposureRoi(const MeasurementRois &rois, int camera,
                                     cv::Rect fallback, cv::Size size, bool *fromPlc = nullptr);
    std::optional<GrayStats> meltRoiStats(const cv::Mat &image,
                                            const std::array<double, 6> &values,
                                            int camera);
    std::optional<double> dipLineMean(const cv::Mat &image,
                                      const std::array<double, 3> &values);
}
