#include "measurement_geometry.hpp"
#include <algorithm>
#include <cmath>
namespace
{
    std::optional<pva::GrayStats> imageStats(const cv::Mat &image, cv::Rect roi)
    {
        if (image.empty())
            return {};
        roi &= cv::Rect(0, 0, image.cols, image.rows);
        if (roi.empty())
            return {};
        const cv::Mat pixels = image(roi);
        double minimum = 0.0, maximum = 0.0;
        cv::minMaxLoc(pixels, &minimum, &maximum);
        return pva::GrayStats{cv::mean(pixels)[0], maximum, minimum};
    }
}
namespace pva
{
    cv::Rect diameterRoiRect(const std::array<double, 8> &v, double height, int camera, cv::Size size)
    {
        if (size.empty() || !std::isfinite(height) || height <= 0 ||
            !std::all_of(v.begin(), v.end(), [](double value)
                         { return std::isfinite(value); }) ||
            v[4] < 0 || v[5] <= v[4])
            return {};
        const double cx = v[0] + (camera == 2 ? v[6] : 0.0);
        const double cy = v[1] + (camera == 2 ? v[7] : 0.0);
        // Clamp before conversion to avoid overflow for off-image PLC coordinates.
        const int left = cvRound(std::clamp(cx - v[5], 0.0, double(size.width)));
        const int right = cvRound(std::clamp(cx - v[4], 0.0, double(size.width)));
        const int top = cvRound(std::clamp(cy - height / 2.0, 0.0, double(size.height)));
        const int bottom = cvRound(std::clamp(cy + height / 2.0, 0.0, double(size.height)));
        if (right <= left || bottom <= top)
            return {};
        return {left, top, right - left, bottom - top};
    }
    cv::Rect meltRoiRect(const std::array<double, 6> &v, int camera, cv::Size size)
    {
        const double cx = v[0] + (camera == 2 ? v[4] : 0.0);
        const double cy = v[1] + (camera == 2 ? v[5] : 0.0);
        const int left = cvRound(cx - v[2] / 2.0), right = cvRound(cx + v[2] / 2.0);
        const int top = cvRound(cy - v[3] / 2.0), bottom = cvRound(cy + v[3] / 2.0);
        return cv::Rect(left, top, right - left, bottom - top) & cv::Rect(0, 0, size.width, size.height);
    }
    cv::Rect effectiveAutoExposureRoi(const MeasurementRois &rois, int camera,
                                      cv::Rect fallback, cv::Size size, bool *fromPlc)
    {
        if (fromPlc)
            *fromPlc = false;
        if (rois.melt)
        {
            const auto roi = meltRoiRect(*rois.melt, camera, size);
            if (!roi.empty())
            {
                if (fromPlc)
                    *fromPlc = true;
                return roi;
            }
        }
        return fallback & cv::Rect(0, 0, size.width, size.height);
    }
    std::optional<GrayStats> meltRoiStats(const cv::Mat &image,
                                          const std::array<double, 6> &v, int camera)
    {
        return imageStats(image, meltRoiRect(v, camera, image.size()));
    }

    std::optional<double> dipLineMean(const cv::Mat &image,
                                      const std::array<double, 3> &v)
    {
        if (image.empty())
            return {};
        const int x = cvRound(v[0]);
        const int top = std::max(0, cvRound(v[1] - v[2] / 2.0));
        const int bottom = std::min(image.rows, cvRound(v[1] + v[2] / 2.0) + 1);
        if (x < 0 || x >= image.cols || top >= bottom)
            return {};
        return cv::mean(image(cv::Rect(x, top, 1, bottom - top)))[0];
    }

}
