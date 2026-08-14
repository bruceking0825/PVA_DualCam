#include "pva/algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace pva::algorithms
{
    static double bottomAt(const ReflectorRoi &roi, double x)
    {
        if (roi.bottomCurve.empty())
            return std::max(roi.leftBoundary.y, roi.rightBoundary.y);
        if (x <= roi.bottomCurve.front().x)
            return roi.bottomCurve.front().y;
        if (x >= roi.bottomCurve.back().x)
            return roi.bottomCurve.back().y;
        const auto upper = std::lower_bound(roi.bottomCurve.begin(), roi.bottomCurve.end(), x,
                                            [](const cv::Point2d &point, double value) { return point.x < value; });
        const auto lower = std::prev(upper);
        const double fraction = (x - lower->x) / std::max(upper->x - lower->x, 1e-9);
        return lower->y + fraction * (upper->y - lower->y);
    }
    static double value(const cv::Vec3d &c, double x) { return c[0] * x * x + c[1] * x + c[2]; }
    static bool robustFit(std::vector<cv::Point2d> &points, std::vector<double> &weights, double limit, cv::Vec3d &c)
    {
        if (points.size() < 5)
            return false;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            cv::Mat a(int(points.size()), 3, CV_64F), b(int(points.size()), 1, CV_64F);
            for (int i = 0; i < a.rows; ++i)
            {
                double w = std::sqrt(std::max(weights[i], 1e-6)), x = points[i].x;
                a.at<double>(i, 0) = x * x * w;
                a.at<double>(i, 1) = x * w;
                a.at<double>(i, 2) = w;
                b.at<double>(i) = points[i].y * w;
            }
            cv::Mat solution;
            if (!cv::solve(a, b, solution, cv::DECOMP_SVD))
                return false;
            c = {solution.at<double>(0), solution.at<double>(1), solution.at<double>(2)};
            std::vector<double> residual;
            for (auto p : points)
                residual.push_back(p.y - value(c, p.x));
            auto sorted = residual;
            std::sort(sorted.begin(), sorted.end());
            double median = sorted[sorted.size() / 2];
            for (double &v : sorted)
                v = std::abs(v - median);
            std::sort(sorted.begin(), sorted.end());
            double threshold = std::max(limit, 3 * 1.4826 * sorted[sorted.size() / 2]);
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
    static std::optional<CurveHit> finish(std::vector<cv::Point2d> points, std::vector<double> strengths, int minPoints, double residual, double middle, double width, double minCoverage)
    {
        if (points.size() < size_t(minPoints))
            return {};
        cv::Vec3d coefficients;
        if (!robustFit(points, strengths, std::max(1.0, residual), coefficients) || points.size() < size_t(minPoints))
            return {};
        auto [minIt, maxIt] = std::minmax_element(points.begin(), points.end(), [](auto a, auto b)
                                                  { return a.x < b.x; });
        const double minX = minIt->x, maxX = maxIt->x, coverage = (maxX - minX + 1) / width;
        // 下顶点横坐标采用实际拟合点范围的中心，不再使用外部 ROI 中心。
        middle = (minX + maxX) * 0.5;
        // if (coverage < minCoverage || middle < minX || middle > maxX || value(coefficients, middle) - .5 * (value(coefficients, minX) + value(coefficients, maxX)) < 0)
        //     return {};
        CurveHit hit;
        hit.edges = std::move(points);
        hit.boundary = {middle, value(coefficients, middle)};
        hit.coverage = coverage;
        for (int x = int(minX); x <= int(maxX); ++x)
            hit.curve.emplace_back(x, value(coefficients, x));
        return hit;
    }

    std::optional<CurveHit> findCrownMeniscus(const cv::Mat &gray, const ReflectorRoi &roi, const CrownSettings &s, std::optional<double> previous)
    {
        const double left = std::min(roi.leftBoundary.x, roi.rightBoundary.x);
        const double right = std::max(roi.leftBoundary.x, roi.rightBoundary.x);
        int x0 = std::max(1, int(std::ceil(left)) + s.horizontalMarginPx);
        int x1 = std::min(gray.cols - 2, int(std::floor(right)) - s.horizontalMarginPx);
        int y0 = 1;
        int y1 = 1;
        for (int x = x0; x <= x1; ++x)
            y1 = std::max(y1, int(std::ceil(bottomAt(roi, x) - s.bottomMarginPx)));
        y1 = std::min(y1, gray.rows - 1);
        if (x1 - x0 + 1 < s.minEdgePoints || y1 - y0 < 3)
            return {};
        if (previous)
        {
            y0 = std::max(y0, int(*previous) - s.searchHalfHeightPx);
            y1 = std::min(y1, int(*previous) + s.searchHalfHeightPx + 1);
        }
        if (x1 - x0 + 1 < s.minEdgePoints || y1 - y0 < 3)
            return {};
        cv::Mat blur, gradient, score;
        cv::GaussianBlur(gray, blur, {7, 7}, 1.5);
        cv::Sobel(blur, gradient, CV_32F, 0, 1, 3);
        cv::max(-gradient, 0, score);
        cv::blur(score, score, {std::max(3, int(std::lround((x1 - x0 + 1) * .01))), 1});
        std::vector<double> maxima(x1 - x0 + 1);
        double global = 0;
        for (int x = x0; x <= x1; ++x)
        {
            double maximum = 0;
            const int columnStop = std::min(y1, int(std::floor(bottomAt(roi, x) - s.bottomMarginPx)));
            if (columnStop <= y0)
                continue;
            cv::minMaxLoc(score(cv::Rect(x, y0, 1, columnStop - y0)), nullptr, &maximum);
            maxima[x - x0] = maximum;
            global = std::max(global, maximum);
        }
        double minimum = global * std::max(0.0, s.columnMaxFactor);
        std::vector<double> rowScores(y1 - y0);
        std::vector<int> counts(y1 - y0);
        for (int x = x0; x <= x1; ++x)
            if (maxima[x - x0] >= std::max(minimum, 1e-9))
                for (int y = y0; y < y1; ++y)
                {
                    if (y >= bottomAt(roi, x) - s.bottomMarginPx)
                        continue;
                    rowScores[y - y0] += score.at<float>(y, x);
                    ++counts[y - y0];
                }
        for (size_t i = 0; i < rowScores.size(); ++i)
            if (counts[i])
                rowScores[i] /= counts[i];
        const int globalRow = int(std::max_element(rowScores.begin(), rowScores.end()) - rowScores.begin());
        int seed = globalRow;
        for (int i = 1; i + 1 < int(rowScores.size()); ++i)
            if (rowScores[i] >= rowScores[i - 1] && rowScores[i] >= rowScores[i + 1] && rowScores[i] >= rowScores[globalRow] * .5)
                seed = i;
        seed += y0;
        int localStart = std::max(y0, seed - s.searchHalfHeightPx), localStop = std::min(y1, seed + s.searchHalfHeightPx + 1);
        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        for (int x = x0; x <= x1; ++x)
            if (maxima[x - x0] >= std::max(minimum, 1e-9))
            {
                cv::Point location;
                double maximum;
                const int columnStop = std::min(localStop, int(std::floor(bottomAt(roi, x) - s.bottomMarginPx)));
                if (columnStop <= localStart)
                    continue;
                cv::minMaxLoc(score(cv::Rect(x, localStart, 1, columnStop - localStart)), nullptr, &maximum, nullptr, &location);
                if (maximum >= minimum)
                {
                    points.emplace_back(x, localStart + location.y);
                    strengths.push_back(maximum);
                }
            }
        return finish(std::move(points), std::move(strengths), s.minEdgePoints, s.fitResidualPx, roi.center.x, x1 - x0 + 1, 0);
    }

    std::optional<CurveHit> findBodyMeniscus(const cv::Mat &gray, const ReflectorRoi &roi, const BodySettings &s, double offset, std::optional<double> previous)
    {
        const double left = std::min(roi.leftBoundary.x, roi.rightBoundary.x);
        const double right = std::max(roi.leftBoundary.x, roi.rightBoundary.x);
        int x0 = std::max(1, int(std::ceil(left)) + s.horizontalMarginPx);
        int x1 = std::min(gray.cols - 2, int(std::floor(right)) - s.horizontalMarginPx);
        int y0 = std::max(1, int(std::lround(gray.rows * s.startSearchRatio)));
        const int ratioStop = std::min(gray.rows - 1, int(std::lround(gray.rows * s.stopSearchRatio)));
        int y1 = y0;
        for (int x = x0; x <= x1; ++x)
            y1 = std::max(y1, int(std::ceil(bottomAt(roi, x) - s.bottomMarginPx)));
        y1 = std::min(y1, ratioStop);
        if (previous)
        {
            y0 = std::max(y0, int(*previous) - s.searchHalfHeightPx);
            y1 = std::min(y1, int(*previous) + s.searchHalfHeightPx + 1);
        }
        if (x1 - x0 + 1 < s.minEdgePoints || y1 - y0 < 4)
            return {};
        cv::Mat brightness;
        cv::GaussianBlur(gray, brightness, {7, 7}, 1.5);
        cv::blur(brightness, brightness, {std::max(3, int(std::lround((x1 - x0 + 1) * .05))), 1});
        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        for (int x = x0; x <= x1; ++x)
        {
            const int columnStop = std::min(y1, int(std::floor(bottomAt(roi, x) - s.bottomMarginPx)));
            if (columnStop - y0 < 4)
                continue;
            double maximum = 0;
            cv::minMaxLoc(brightness(cv::Rect(x, y0, 1, columnStop - y0)), nullptr, &maximum);
            double threshold = maximum - std::max(offset, 0.0);
            for (int y = columnStop - 2; y >= y0 + 3; --y)
                if (brightness.at<uchar>(y, x) < threshold && brightness.at<uchar>(y - 1, x) >= threshold && brightness.at<uchar>(y - 2, x) >= threshold && brightness.at<uchar>(y - 3, x) >= threshold)
                {
                    double inside = brightness.at<uchar>(y - 1, x), outside = brightness.at<uchar>(y, x), fraction = std::abs(inside - outside) < 1e-12 ? 0 : std::clamp((inside - threshold) / (inside - outside), 0.0, 1.0);
                    points.emplace_back(x, y - 1 + fraction);
                    strengths.push_back(maximum);
                    break;
                }
        }
        return finish(std::move(points), std::move(strengths), s.minEdgePoints, s.fitResidualPx, roi.center.x, x1 - x0 + 1, s.minCoverageRatio);
    }
}
