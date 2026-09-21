#include "algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace pva::algorithms
{
    DetectionResult<EndconeHit> findEndcone(const cv::Mat &gray, cv::Point2d center,
                                            cv::Vec2i span, double scale,
                                            const EndconeSettings &s)
    {
        if (gray.empty())
            return DetectionResult<EndconeHit>::failure("input image is empty");
        if (gray.channels() != 1)
            return DetectionResult<EndconeHit>::failure("input image is not single-channel grayscale");
        if (gray.rows < 3 || gray.cols < 3)
            return DetectionResult<EndconeHit>::failure("input image is smaller than 3 x 3 pixels");
        if (!std::isfinite(center.x) || !std::isfinite(center.y))
            return DetectionResult<EndconeHit>::failure("body center contains a non-finite coordinate");
        if (!std::isfinite(scale) || scale <= 0.0)
            return DetectionResult<EndconeHit>::failure("millimetres-per-pixel must be positive and finite");

        int x0 = std::max(0, span[0]), x1 = std::min(gray.cols, span[1] + 1), y0 = std::clamp(int(center.y), 1, gray.rows - 2);
        if (x1 - x0 < 3)
            return DetectionResult<EndconeHit>::failure(
                "neck x-span is too narrow after clipping (width=" +
                std::to_string(x1 - x0) + ")");
        if (y0 >= gray.rows - 2)
            return DetectionResult<EndconeHit>::failure(
                "body center leaves fewer than two rows for the endcone search");
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
        return DetectionResult<EndconeHit>::success(
            EndconeHit{boundary, std::abs(boundary - center.y) * scale, x0, x1});
    }
}
