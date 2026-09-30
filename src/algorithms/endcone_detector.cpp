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

        const int y0 = std::max(0, span[0]);
        const int y1 = std::min(gray.rows, span[1] + 1);
        const int x1 = std::clamp(int(center.x), 2, gray.cols - 1);
        if (y1 - y0 < 3)
            return DetectionResult<EndconeHit>::failure(
                "neck y-span is too narrow after clipping (height=" +
                std::to_string(y1 - y0) + ")");
        if (x1 < 3)
            return DetectionResult<EndconeHit>::failure(
                "body center leaves fewer than two columns for the endcone search");
        cv::Mat profile;
        cv::reduce(gray(cv::Rect(0, y0, x1, y1 - y0)), profile, 0, cv::REDUCE_AVG, CV_64F);
        double best = -1;
        int index = 0;
        // 从中心向图像左侧搜索，保持旧图像中“向下”的物理方向。
        for (int x = x1 - 2; x >= 0; --x)
        {
            double d = std::abs(profile.at<double>(0, x + 1) - profile.at<double>(0, x));
            if (d > best)
            {
                best = d;
                index = x;
            }
        }
        const double boundary = index - s.boundaryOffsetPx;
        return DetectionResult<EndconeHit>::success(
            EndconeHit{boundary, std::abs(boundary - center.x) * scale, y0, y1});
    }
}
