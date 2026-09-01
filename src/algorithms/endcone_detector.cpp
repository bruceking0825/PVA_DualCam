#include "algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace pva::algorithms
{
    std::optional<EndconeHit> findEndcone(const cv::Mat &gray, cv::Point2d center, cv::Vec2i span, double scale, const EndconeSettings &s)
    {
        int x0 = std::max(0, span[0]), x1 = std::min(gray.cols, span[1] + 1), y0 = std::clamp(int(center.y), 1, gray.rows - 2);
        if (x1 - x0 < 3 || y0 >= gray.rows - 2)
            return {};
        cv::Mat profile;
        cv::reduce(gray(cv::Rect(x0, y0, x1 - x0, gray.rows - y0)), profile, 1, cv::REDUCE_AVG, CV_64F);
        double best = -1;
        int index = 0;
        for (int y = 0; y + 1 < profile.rows; ++y)
        {
            double d = std::abs(profile.at<double>(y + 1) - profile.at<double>(y));
            if (d > best)
            {
                best = d;
                index = y;
            }
        }
        double boundary = y0 + index + s.boundaryOffsetPx;
        return EndconeHit{boundary, std::abs(boundary - center.y) * scale, x0, x1};
    }
}
