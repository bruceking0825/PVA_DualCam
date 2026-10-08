#include "algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace pva::algorithms
{
    DetectionResult<EndconeHit> findEndcone(const cv::Mat &gray, const cv::Rect &configuredRoi, cv::Point2d center,
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

        // 搜索范围取配置 ROI、Neck 高度范围及中心左侧区域的交集。
        const cv::Rect roi = configuredRoi & cv::Rect(0, 0, gray.cols, gray.rows);
        const int y0 = std::max(roi.y, span[0]);
        const int y1 = std::min(roi.y + roi.height, span[1] + 1);
        const int x0 = roi.x;
        const int x1 = std::min(roi.x + roi.width, std::clamp(int(center.x), 0, gray.cols));
        if (y1 - y0 < 3)
            return DetectionResult<EndconeHit>::failure(
                "neck y-span is too narrow after clipping (height=" +
                std::to_string(y1 - y0) + ")");
        if (x1 - x0 < 3)
            return DetectionResult<EndconeHit>::failure(
                "body center leaves fewer than two columns for the endcone search");
        cv::Mat profile;
        cv::reduce(gray(cv::Rect(x0, y0, x1 - x0, y1 - y0)), profile, 0, cv::REDUCE_AVG, CV_64F);
        double best = -1;
        int index = 0;
        // 从中心向图像左侧搜索，保持旧图像中“向下”的物理方向。
        for (int x = x1 - x0 - 2; x >= 0; --x)
        {
            double d = std::abs(profile.at<double>(0, x + 1) - profile.at<double>(0, x));
            if (d > best)
            {
                best = d;
                index = x;
            }
        }
        const double boundary = x0 + index - s.boundaryOffsetPx;
        return DetectionResult<EndconeHit>::success(
            EndconeHit{boundary, std::abs(boundary - center.x) * scale, y0, y1});
    }
}
