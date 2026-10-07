#include "measurement_geometry.hpp"
#include <algorithm>
namespace {
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
namespace pva {
    std::optional<GrayStats> meltRoiStats(const cv::Mat &image,
                                            const std::array<double, 6> &v, int camera)
    {
        const double cx = v[0] + (camera == 2 ? v[4] : 0.0);
        const double cy = v[1] + (camera == 2 ? v[5] : 0.0);
        const int left = cvRound(cx - v[2] / 2.0), right = cvRound(cx + v[2] / 2.0);
        const int top = cvRound(cy - v[3] / 2.0), bottom = cvRound(cy + v[3] / 2.0);
        return imageStats(image, cv::Rect(left, top, right - left, bottom - top));
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
