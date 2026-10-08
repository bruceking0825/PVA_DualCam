#include "algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pva::algorithms
{
    static cv::Rect clippedRoi(const cv::Rect &roi, const cv::Mat &image)
    {
        return roi & cv::Rect(0, 0, image.cols, image.rows);
    }
    // 当前图像坐标：每一行 y 找到边界 x，曲线直接拟合 x=f(y)。
    static double value(const cv::Vec3d &c, double y) { return c[0] * y * y + c[1] * y + c[2]; }
    static double percentile(std::vector<double> values, double fraction)
    {
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        const double index = std::clamp(fraction, 0.0, 1.0) * (values.size() - 1);
        const size_t lower = size_t(std::floor(index));
        const size_t upper = size_t(std::ceil(index));
        const double weight = index - lower;
        return values[lower] * (1.0 - weight) + values[upper] * weight;
    }
    static bool robustFit(std::vector<cv::Point2d> &points, std::vector<double> &weights, double limit, cv::Vec3d &c)
    {
        if (points.size() < 5)
            return false;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            cv::Mat a(int(points.size()), 3, CV_64F), b(int(points.size()), 1, CV_64F);
            for (int i = 0; i < a.rows; ++i)
            {
                double w = std::sqrt(std::max(weights[i], 1e-6)), y = points[i].y;
                a.at<double>(i, 0) = y * y * w;
                a.at<double>(i, 1) = y * w;
                a.at<double>(i, 2) = w;
                b.at<double>(i) = points[i].x * w;
            }
            cv::Mat solution;
            if (!cv::solve(a, b, solution, cv::DECOMP_SVD))
                return false;
            c = {solution.at<double>(0), solution.at<double>(1), solution.at<double>(2)};
            std::vector<double> residual;
            for (auto p : points)
                residual.push_back(p.x - value(c, p.y));
            const double median = percentile(residual, 0.5);
            auto sorted = residual;
            for (double &v : sorted)
                v = std::abs(v - median);
            const double threshold = std::max(limit, 3 * 1.4826 * percentile(sorted, 0.5));
            std::vector<cv::Point2d> kept;
            std::vector<double> keptWeights;
            for (size_t i = 0; i < points.size(); ++i)
                if (std::abs(residual[i] - median) <= threshold)
                {
                    kept.push_back(points[i]);
                    keptWeights.push_back(weights[i]);
                }
            if (kept.size() < 5 || kept.size() == points.size())
                return true;
            points = std::move(kept);
            weights = std::move(keptWeights);
        }
        return true;
    }
    static DetectionResult<CurveHit> finish(std::vector<cv::Point2d> points,
                                            std::vector<double> strengths,
                                            int minPoints, double residual,
                                            double middle, double width,
                                            double minCoverage, bool enforceCoverage)
    {
        if (points.size() < size_t(minPoints))
            return DetectionResult<CurveHit>::failure(
                "too few edge points for curve fitting (found=" +
                std::to_string(points.size()) + ", required=" +
                std::to_string(minPoints) + ")");
        const int edgePointCount = int(points.size());
        const double residualLimit = std::max(1.0, residual);
        cv::Vec3d coefficients;
        if (!robustFit(points, strengths, residualLimit, coefficients))
            return DetectionResult<CurveHit>::failure("robust quadratic curve fit failed");
        if (points.size() < size_t(minPoints))
            return DetectionResult<CurveHit>::failure(
                "too few inliers after robust curve fitting (kept=" +
                std::to_string(points.size()) + ", required=" +
                std::to_string(minPoints) + ")");
        auto [minIt, maxIt] = std::minmax_element(points.begin(), points.end(), [](auto a, auto b)
                                                  { return a.y < b.y; });
        const double minY = minIt->y, maxY = maxIt->y, coverage = (maxY - minY + 1) / width;
        const double sagitta = .5 * (value(coefficients, minY) + value(coefficients, maxY)) -
                               value(coefficients, middle);
        // 当前图像中的有效弧线在中间向左凹；以 Neck 中心 y 投影得到边界顶点。
        if (enforceCoverage && coverage < minCoverage)
            return DetectionResult<CurveHit>::failure(
                "curve coverage is below the configured minimum (coverage=" +
                std::to_string(coverage) + ", minimum=" +
                std::to_string(minCoverage) + ")");
        if (middle < minY || middle > maxY)
            return DetectionResult<CurveHit>::failure(
                "neck center y is outside the fitted curve range (center_y=" +
                std::to_string(middle) + ", range=" + std::to_string(minY) +
                ".." + std::to_string(maxY) + ")");
        if (sagitta < 0)
            return DetectionResult<CurveHit>::failure(
                "fitted curve bends in the wrong direction (sagitta=" +
                std::to_string(sagitta) + ")");
        CurveHit hit;
        hit.edges = std::move(points);
        hit.boundary = {value(coefficients, middle), middle};
        hit.coverage = coverage;
        hit.edgePointCount = edgePointCount;
        hit.robustInlierCount = int(hit.edges.size());
        hit.residualLimitPx = residualLimit;
        hit.sagittaPx = sagitta;
        if (!strengths.empty())
            hit.fitStrengthMean = std::accumulate(strengths.begin(), strengths.end(), 0.0) / strengths.size();
        double squaredError = 0.0;
        for (const auto &point : hit.edges)
        {
            const double error = point.x - value(coefficients, point.y);
            squaredError += error * error;
        }
        hit.fitErrorPx = std::sqrt(squaredError / std::max<size_t>(hit.edges.size(), 1));
        std::vector<double> fittedY;
        fittedY.reserve(hit.edges.size());
        for (const auto &point : hit.edges)
            fittedY.push_back(point.x);
        hit.seedX = percentile(std::move(fittedY), 0.5);
        for (int y = int(minY); y <= int(maxY); ++y)
            hit.curve.emplace_back(value(coefficients, y), y);
        return DetectionResult<CurveHit>::success(std::move(hit));
    }

    static int firstPeakIndex(const std::vector<double> &scores)
    {
        if (scores.empty())
            return 0;
        const auto maximumIt = std::max_element(scores.begin(), scores.end());
        const double minimumHeight = *maximumIt * 0.5;
        for (int begin = 1; begin + 1 < int(scores.size());)
        {
            int end = begin;
            while (end + 1 < int(scores.size()) && scores[end + 1] == scores[begin])
                ++end;
            if (end + 1 < int(scores.size()) && scores[begin] > scores[begin - 1] &&
                scores[end] > scores[end + 1] && scores[begin] >= minimumHeight)
                return (begin + end) / 2;
            begin = end + 1;
        }
        return int(maximumIt - scores.begin());
    }

    DetectionResult<CurveHit> findCrownMeniscus(const cv::Mat &gray,
                                                const cv::Rect &configuredRoi,
                                                cv::Point2d expectedCenter,
                                                const CrownSettings &s,
                                                std::optional<double> previous)
    {
        if (gray.empty())
            return DetectionResult<CurveHit>::failure("input image is empty");
        if (gray.channels() != 1)
            return DetectionResult<CurveHit>::failure("input image is not single-channel grayscale");
        if (gray.rows < 3 || gray.cols < 3)
            return DetectionResult<CurveHit>::failure("input image is smaller than 3 x 3 pixels");
        if (!std::isfinite(expectedCenter.x) || !std::isfinite(expectedCenter.y))
            return DetectionResult<CurveHit>::failure("neck center contains a non-finite coordinate");

        const cv::Rect roi = clippedRoi(configuredRoi, gray);
        // 竖直边距裁剪图像上下边；左边距裁剪 x 搜索起点。
        const int y0 = std::max(1, roi.y + std::max(0, s.verticalMarginPx));
        const int y1 = std::min(gray.rows - 2, roi.y + roi.height - 1 - std::max(0, s.verticalMarginPx));
        if (y1 - y0 + 1 < s.minEdgePoints)
            return DetectionResult<CurveHit>::failure(
                "ROI vertical search height is smaller than min_edge_points (height=" +
                std::to_string(std::max(0, y1 - y0 + 1)) + ", required=" +
                std::to_string(s.minEdgePoints) + ")");

        if (roi.width < 3)
            return DetectionResult<CurveHit>::failure(
                "ROI width is smaller than 3 pixels after clipping (width=" +
                std::to_string(roi.width) + ")");
        int searchStart = std::max(1, roi.x + std::max(0, s.leftMarginPx));
        int searchStop = std::min({gray.cols - 1, roi.x + roi.width,
                                   int(std::ceil(expectedCenter.x)) + 1});
        const int trackingHalfWidth = std::max(8, s.searchHalfWidthPx);
        if (previous)
        {
            searchStart = std::max(searchStart, int(std::floor(*previous)) - trackingHalfWidth);
            searchStop = std::min(searchStop, int(std::ceil(*previous)) + trackingHalfWidth + 1);
        }
        if (searchStop - searchStart < 3)
            return DetectionResult<CurveHit>::failure(
                "horizontal search range is smaller than 3 pixels (start=" +
                std::to_string(searchStart) + ", stop=" +
                std::to_string(searchStop) + ")");

        cv::Mat blurred, gradient, score;
        // 独立 ROI 图像避免滤波读取父图像中 ROI 外的像素。
        cv::GaussianBlur(gray(roi).clone(), blurred, {7, 7}, 1.5);
        cv::Sobel(blurred, gradient, CV_32F, 1, 0, 3);
        cv::max(gradient, 0, score);
        cv::blur(score, score, {1, std::max(3, int(std::lround((y1 - y0 + 1) * .01)))});

        std::vector<double> maxima(y1 - y0 + 1, 0.0);
        double globalMaximum = 0.0;
        for (int y = y0; y <= y1; ++y)
        {
            const int index = y - y0;
            cv::minMaxLoc(score(cv::Rect(searchStart - roi.x, y - roi.y, searchStop - searchStart, 1)),
                          nullptr, &maxima[index]);
            globalMaximum = std::max(globalMaximum, maxima[index]);
        }
        const double minimumStrength = globalMaximum * std::max(0.0, s.rowMaxFactor);
        const double keepThreshold = std::max(minimumStrength, std::numeric_limits<double>::epsilon());
        const double rowStrengthsMean = std::accumulate(maxima.begin(), maxima.end(), 0.0) / maxima.size();
        const int keptRowCount = int(std::count_if(maxima.begin(), maxima.end(),
                                                   [keepThreshold](double strength)
                                                   { return strength >= keepThreshold; }));
        if (keptRowCount < s.minEdgePoints)
            return DetectionResult<CurveHit>::failure(
                "too few rows have sufficient positive-gradient strength (kept=" +
                std::to_string(keptRowCount) + ", required=" +
                std::to_string(s.minEdgePoints) + ", maximum_strength=" +
                std::to_string(globalMaximum) + ")");

        std::vector<double> columnScores(searchStop - searchStart, 0.0);
        std::vector<int> validCounts(searchStop - searchStart, 0);
        int selectedY0 = y1;
        int selectedY1 = y0;
        for (int y = y0; y <= y1; ++y)
        {
            const int index = y - y0;
            if (maxima[index] < keepThreshold)
                continue;
            selectedY0 = std::min(selectedY0, y);
            selectedY1 = std::max(selectedY1, y);
            for (int x = searchStart; x < searchStop; ++x)
            {
                columnScores[x - searchStart] += score.at<float>(y - roi.y, x - roi.x);
                ++validCounts[x - searchStart];
            }
        }
        for (size_t i = 0; i < columnScores.size(); ++i)
            columnScores[i] /= std::max(validCounts[i], 1);
        const int seed = searchStart + firstPeakIndex(columnScores);
        const int localStart = std::max(searchStart, seed - trackingHalfWidth);

        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        for (int y = y0; y <= y1; ++y)
        {
            const int index = y - y0;
            if (maxima[index] < keepThreshold)
                continue;
            const int localStop = std::min(searchStop, seed + trackingHalfWidth + 1);
            if (localStop <= localStart)
                continue;
            cv::Point location;
            double maximum = 0.0;
            cv::minMaxLoc(score(cv::Rect(localStart - roi.x, y - roi.y, localStop - localStart, 1)),
                          nullptr, &maximum, nullptr, &location);
            if (maximum >= minimumStrength)
            {
                points.emplace_back(localStart + location.x, y);
                strengths.push_back(maximum);
            }
        }
        auto hit = finish(std::move(points), std::move(strengths), s.minEdgePoints,
                          s.fitResidualPx, expectedCenter.y,
                          std::max(selectedY1 - selectedY0 + 1, 1), 0.0, false);
        if (hit)
        {
            hit->center = expectedCenter;
            hit->seedX = seed;
            hit->rowStrengthsMean = rowStrengthsMean;
            hit->rowStrengthsMaximum = globalMaximum;
            hit->minimumStrength = minimumStrength;
            hit->keptRowCount = keptRowCount;
        }
        return hit;
    }

    DetectionResult<CurveHit> findBodyMeniscus(const cv::Mat &gray,
                                               const cv::Rect &configuredRoi,
                                               cv::Point2d expectedCenter,
                                               const BodySettings &s,
                                               double offset,
                                               std::optional<double> previous)
    {
        if (gray.empty())
            return DetectionResult<CurveHit>::failure("input image is empty");
        if (gray.channels() != 1)
            return DetectionResult<CurveHit>::failure("input image is not single-channel grayscale");
        if (gray.rows < 3 || gray.cols < 3)
            return DetectionResult<CurveHit>::failure("input image is smaller than 3 x 3 pixels");
        if (!std::isfinite(expectedCenter.x) || !std::isfinite(expectedCenter.y))
            return DetectionResult<CurveHit>::failure("neck center contains a non-finite coordinate");
        if (!std::isfinite(offset))
            return DetectionResult<CurveHit>::failure("brightness offset is not finite");
        if (!std::isfinite(s.startSearchRatio) || !std::isfinite(s.stopSearchRatio) ||
            s.startSearchRatio < 0.0 || s.stopSearchRatio > 1.0 ||
            s.startSearchRatio >= s.stopSearchRatio)
            return DetectionResult<CurveHit>::failure("body search ratios must satisfy 0 <= start < stop <= 1");

        const cv::Rect roi = clippedRoi(configuredRoi, gray);
        // 竖直边距裁剪图像上下边；左边距裁剪 x 搜索起点。
        const int y0 = std::max(1, roi.y + std::max(0, s.verticalMarginPx));
        const int y1 = std::min(gray.rows - 2, roi.y + roi.height - 1 - std::max(0, s.verticalMarginPx));
        if (y1 - y0 + 1 < s.minEdgePoints)
            return DetectionResult<CurveHit>::failure(
                "ROI vertical search height is smaller than min_edge_points (height=" +
                std::to_string(std::max(0, y1 - y0 + 1)) + ", required=" +
                std::to_string(s.minEdgePoints) + ")");
        if (roi.width < 3)
            return DetectionResult<CurveHit>::failure(
                "ROI width is smaller than 3 pixels after clipping (width=" +
                std::to_string(roi.width) + ")");

        const int ratioStart = std::clamp(int(std::nearbyint(gray.cols * s.startSearchRatio)), 0, gray.cols - 1);
        const int ratioStop = std::clamp(int(std::nearbyint(gray.cols * s.stopSearchRatio)), ratioStart + 1, gray.cols);
        int searchStart = std::max({ratioStart, roi.x + std::max(0, s.leftMarginPx), 1});
        int searchStop = std::min({ratioStop, roi.x + roi.width, gray.cols - 1,
                                   int(std::ceil(expectedCenter.x)) + 1});
        const int trackingHalfWidth = std::max(8, s.searchHalfWidthPx);
        if (previous)
        {
            searchStart = std::max(searchStart, int(std::floor(*previous)) - trackingHalfWidth);
            searchStop = std::min(searchStop, int(std::ceil(*previous)) + trackingHalfWidth + 1);
        }
        if (searchStop - searchStart < 3)
            return DetectionResult<CurveHit>::failure(
                "horizontal search range is smaller than 3 pixels (start=" +
                std::to_string(searchStart) + ", stop=" + std::to_string(searchStop) + ")");

        // 在配置 ROI 与原生 x 比例窗口的交集内滤波，隔离 ROI 外像素。
        const cv::Rect processingRoi = roi & cv::Rect(ratioStart, 0, ratioStop - ratioStart, gray.rows);
        const cv::Mat ratioRoi = gray(processingRoi).clone();
        cv::Mat blurred, brightness;
        cv::GaussianBlur(ratioRoi, blurred, {7, 7}, 1.5);
        cv::blur(blurred, brightness, {1, std::max(3, int(std::lround((y1 - y0 + 1) * .05)))});

        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        std::vector<double> usableMaxima;
        for (int y = y0; y <= y1; ++y)
        {
            double maximum = 0.0;
            cv::minMaxLoc(brightness(cv::Rect(searchStart - processingRoi.x, y - processingRoi.y,
                                               searchStop - searchStart, 1)),
                          nullptr, &maximum);
            if (maximum > std::numeric_limits<double>::epsilon())
                usableMaxima.push_back(maximum);
            const double threshold = maximum - std::max(offset, 0.0);
            for (int outside = searchStart; outside + 3 < searchStop; ++outside)
            {
                const int local = outside - processingRoi.x;
                const int row = y - processingRoi.y;
                if (brightness.at<uchar>(row, local) < threshold &&
                    brightness.at<uchar>(row, local + 1) >= threshold &&
                    brightness.at<uchar>(row, local + 2) >= threshold &&
                    brightness.at<uchar>(row, local + 3) >= threshold)
                {
                    const double insideValue = brightness.at<uchar>(row, local + 1);
                    const double outsideValue = brightness.at<uchar>(row, local);
                    const double denominator = insideValue - outsideValue;
                    const double fraction = std::abs(denominator) < 1e-12
                                                ? 0.0
                                                : std::clamp((insideValue - threshold) / denominator, 0.0, 1.0);
                    points.emplace_back(outside + 1 - fraction, y);
                    strengths.push_back(maximum);
                    break;
                }
            }
        }
        const int thresholdCrossingCount = int(points.size());
        const double maximumP90 = percentile(usableMaxima, 0.9);
        const double maximumMaximum = usableMaxima.empty() ? 0.0 : *std::max_element(usableMaxima.begin(), usableMaxima.end());
        auto hit = finish(std::move(points), std::move(strengths), s.minEdgePoints,
                          s.fitResidualPx, expectedCenter.y, y1 - y0 + 1,
                          s.minCoverageRatio, true);
        if (hit)
        {
            hit->center = expectedCenter;
            hit->leftMarginPx = s.leftMarginPx;
            hit->trackingHalfWidthPx = trackingHalfWidth;
            hit->brightnessOffset = offset;
            hit->thresholdCrossingCount = thresholdCrossingCount;
            hit->rowMaximumP90 = maximumP90;
            hit->rowMaximumMaximum = maximumMaximum;
            hit->searchStartX = ratioStart;
            hit->searchStopX = ratioStop;
        }
        return hit;
    }
}
