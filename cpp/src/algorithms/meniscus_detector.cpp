#include "pva/algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pva::algorithms
{
    static std::vector<double> bottomProfile(const ReflectorRoi &roi, int x0, int x1)
    {
        std::vector<cv::Point2d> curve = roi.bottomCurve;
        if (curve.size() < 2)
            curve = {roi.leftBoundary, roi.rightBoundary};
        std::stable_sort(curve.begin(), curve.end(), [](const auto &left, const auto &right)
                         { return left.x < right.x; });
        curve.erase(std::unique(curve.begin(), curve.end(), [](const auto &left, const auto &right)
                                { return left.x == right.x; }),
                    curve.end());
        if (curve.size() < 2 || x1 < x0)
            return {};

        // Match numpy.interp: sort and deduplicate x, then clamp outside the endpoint range.
        std::vector<double> values;
        values.reserve(x1 - x0 + 1);
        size_t upperIndex = 1;
        for (int x = x0; x <= x1; ++x)
        {
            if (x <= curve.front().x)
            {
                values.push_back(curve.front().y);
                continue;
            }
            if (x >= curve.back().x)
            {
                values.push_back(curve.back().y);
                continue;
            }
            while (upperIndex < curve.size() && curve[upperIndex].x < x)
                ++upperIndex;
            const auto &upper = curve[upperIndex];
            const auto &lower = curve[upperIndex - 1];
            const double fraction = (x - lower.x) / (upper.x - lower.x);
            values.push_back(lower.y + fraction * (upper.y - lower.y));
        }
        return values;
    }
    static double value(const cv::Vec3d &c, double x) { return c[0] * x * x + c[1] * x + c[2]; }
    static double percentile(std::vector<double> values, double fraction)
    {
        if (values.empty()) return 0.0;
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
    static std::optional<CurveHit> finish(std::vector<cv::Point2d> points, std::vector<double> strengths,
                                          int minPoints, double residual, double middle, double width,
                                          double minCoverage, bool enforceCoverage)
    {
        if (points.size() < size_t(minPoints))
            return {};
        const int edgePointCount = int(points.size());
        const double residualLimit = std::max(1.0, residual);
        cv::Vec3d coefficients;
        if (!robustFit(points, strengths, residualLimit, coefficients) || points.size() < size_t(minPoints))
            return {};
        auto [minIt, maxIt] = std::minmax_element(points.begin(), points.end(), [](auto a, auto b)
                                                  { return a.x < b.x; });
        const double minX = minIt->x, maxX = maxIt->x, coverage = (maxX - minX + 1) / width;
        const double sagitta = value(coefficients, middle) -
                                .5 * (value(coefficients, minX) + value(coefficients, maxX));
        // Python 以 Neck 中心投影到曲线得到下顶点，并拒绝外推、反向弯曲及低覆盖结果。
        if ((enforceCoverage && coverage < minCoverage) || middle < minX || middle > maxX || sagitta < 0)
            return {};
        CurveHit hit;
        hit.edges = std::move(points);
        hit.boundary = {middle, value(coefficients, middle)};
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
            const double error = point.y - value(coefficients, point.x);
            squaredError += error * error;
        }
        hit.fitErrorPx = std::sqrt(squaredError / std::max<size_t>(hit.edges.size(), 1));
        std::vector<double> fittedY;
        fittedY.reserve(hit.edges.size());
        for (const auto &point : hit.edges) fittedY.push_back(point.y);
        hit.seedY = percentile(std::move(fittedY), 0.5);
        for (int x = int(minX); x <= int(maxX); ++x)
            hit.curve.emplace_back(x, value(coefficients, x));
        return hit;
    }

    static int lastPeakIndex(const std::vector<double> &scores)
    {
        if (scores.empty())
            return 0;
        const auto maximumIt = std::max_element(scores.begin(), scores.end());
        const double minimumHeight = *maximumIt * 0.5;
        int lastPeak = -1;
        for (int begin = 1; begin + 1 < int(scores.size());)
        {
            int end = begin;
            while (end + 1 < int(scores.size()) && scores[end + 1] == scores[begin])
                ++end;
            if (end + 1 < int(scores.size()) && scores[begin] > scores[begin - 1] &&
                scores[end] > scores[end + 1] && scores[begin] >= minimumHeight)
                lastPeak = (begin + end) / 2;
            begin = end + 1;
        }
        return lastPeak >= 0 ? lastPeak : int(maximumIt - scores.begin());
    }

    std::optional<CurveHit> findCrownMeniscus(const cv::Mat &gray, const ReflectorRoi &roi,
                                               cv::Point2d expectedCenter, const CrownSettings &s,
                                               std::optional<double> previous)
    {
        const double left = std::min(roi.leftBoundary.x, roi.rightBoundary.x);
        const double right = std::max(roi.leftBoundary.x, roi.rightBoundary.x);
        const int x0 = std::max(1, int(std::ceil(left)) + std::max(0, s.horizontalMarginPx));
        const int x1 = std::min(gray.cols - 2, int(std::floor(right)) - std::max(0, s.horizontalMarginPx));
        if (x1 - x0 + 1 < s.minEdgePoints)
            return {};

        auto bottom = bottomProfile(roi, x0, x1);
        if (bottom.empty())
            return {};
        for (double &y : bottom)
            y = std::min(y - std::max(0, s.bottomMarginPx), gray.rows - 2.0);
        const double maximumBottom = *std::max_element(bottom.begin(), bottom.end());
        int searchStart = std::max(1, int(std::floor(expectedCenter.y)));
        int searchStop = std::min(gray.rows - 1, int(std::ceil(maximumBottom)));
        const int trackingHalfHeight = std::max(8, s.searchHalfHeightPx);
        if (previous)
        {
            searchStart = std::max(searchStart, int(std::floor(*previous)) - trackingHalfHeight);
            searchStop = std::min(searchStop, int(std::ceil(*previous)) + trackingHalfHeight + 1);
        }
        if (searchStop - searchStart < 3)
            return {};

        cv::Mat blurred, gradient, score;
        cv::GaussianBlur(gray, blurred, {7, 7}, 1.5);
        cv::Sobel(blurred, gradient, CV_32F, 0, 1, 3);
        cv::max(-gradient, 0, score);
        cv::blur(score, score, {std::max(3, int(std::lround((x1 - x0 + 1) * .01))), 1});

        std::vector<double> maxima(x1 - x0 + 1, 0.0);
        double globalMaximum = 0.0;
        for (int x = x0; x <= x1; ++x)
        {
            const int index = x - x0;
            const int columnStop = std::min(searchStop, int(std::ceil(bottom[index])));
            if (columnStop <= searchStart)
                continue;
            cv::minMaxLoc(score(cv::Rect(x, searchStart, 1, columnStop - searchStart)),
                          nullptr, &maxima[index]);
            globalMaximum = std::max(globalMaximum, maxima[index]);
        }
        const double minimumStrength = globalMaximum * std::max(0.0, s.columnMaxFactor);
        const double keepThreshold = std::max(minimumStrength, std::numeric_limits<double>::epsilon());
        const double columnStrengthsMean = std::accumulate(maxima.begin(), maxima.end(), 0.0) / maxima.size();
        const int keptColumnCount = int(std::count_if(maxima.begin(), maxima.end(),
                                                      [keepThreshold](double strength)
                                                      { return strength >= keepThreshold; }));
        if (keptColumnCount < s.minEdgePoints)
            return {};

        std::vector<double> rowScores(searchStop - searchStart, 0.0);
        std::vector<int> validCounts(searchStop - searchStart, 0);
        int selectedX0 = x1;
        int selectedX1 = x0;
        for (int x = x0; x <= x1; ++x)
        {
            const int index = x - x0;
            if (maxima[index] < keepThreshold)
                continue;
            selectedX0 = std::min(selectedX0, x);
            selectedX1 = std::max(selectedX1, x);
            for (int y = searchStart; y < searchStop; ++y)
                if (y < bottom[index])
                {
                    rowScores[y - searchStart] += score.at<float>(y, x);
                    ++validCounts[y - searchStart];
                }
        }
        for (size_t i = 0; i < rowScores.size(); ++i)
            rowScores[i] /= std::max(validCounts[i], 1);
        const int seed = searchStart + lastPeakIndex(rowScores);
        const int localStart = std::max(searchStart, seed - trackingHalfHeight);

        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        for (int x = x0; x <= x1; ++x)
        {
            const int index = x - x0;
            if (maxima[index] < keepThreshold)
                continue;
            const int localStop = std::min({searchStop, int(std::floor(bottom[index])) + 1,
                                            seed + trackingHalfHeight + 1});
            if (localStop <= localStart)
                continue;
            cv::Point location;
            double maximum = 0.0;
            cv::minMaxLoc(score(cv::Rect(x, localStart, 1, localStop - localStart)),
                          nullptr, &maximum, nullptr, &location);
            if (maximum >= minimumStrength)
            {
                points.emplace_back(x, localStart + location.y);
                strengths.push_back(maximum);
            }
        }
        auto hit = finish(std::move(points), std::move(strengths), s.minEdgePoints,
                          s.fitResidualPx, expectedCenter.x,
                          std::max(selectedX1 - selectedX0 + 1, 1), 0.0, false);
        if (hit)
        {
            hit->center = expectedCenter;
            hit->seedY = seed;
            hit->columnStrengthsMean = columnStrengthsMean;
            hit->columnStrengthsMaximum = globalMaximum;
            hit->minimumStrength = minimumStrength;
            hit->keptColumnCount = keptColumnCount;
        }
        return hit;
    }

    std::optional<CurveHit> findBodyMeniscus(const cv::Mat &gray, const ReflectorRoi &roi,
                                              cv::Point2d expectedCenter, const BodySettings &s,
                                              double offset, std::optional<double> previous)
    {
        const double left = std::min(roi.leftBoundary.x, roi.rightBoundary.x);
        const double right = std::max(roi.leftBoundary.x, roi.rightBoundary.x);
        const int x0 = std::max(1, int(std::ceil(left)) + std::max(0, s.horizontalMarginPx));
        const int x1 = std::min(gray.cols - 2, int(std::floor(right)) - std::max(0, s.horizontalMarginPx));
        if (x1 - x0 + 1 < s.minEdgePoints)
            return {};
        auto bottom = bottomProfile(roi, x0, x1);
        if (bottom.empty())
            return {};
        for (double &y : bottom)
            y = std::min(y - std::max(0, s.bottomMarginPx), gray.rows - 2.0);

        const int ratioStart = std::clamp(int(std::nearbyint(gray.rows * s.startSearchRatio)), 0, gray.rows - 1);
        const int ratioStop = std::clamp(int(std::nearbyint(gray.rows * s.stopSearchRatio)), ratioStart + 1, gray.rows);
        int y0 = std::max({ratioStart, 1, int(std::floor(expectedCenter.y))});
        int y1 = std::min({ratioStop, gray.rows - 1,
                           int(std::ceil(*std::max_element(bottom.begin(), bottom.end())))});
        const int trackingHalfHeight = std::max(8, s.searchHalfHeightPx);
        if (previous)
        {
            y0 = std::max(y0, int(std::floor(*previous)) - trackingHalfHeight);
            y1 = std::min(y1, int(std::ceil(*previous)) + trackingHalfHeight + 1);
        }
        if (y1 - y0 < 3)
            return {};

        // Python 仅在比例 ROI 内执行滤波，边界像素处理也保持一致。
        const cv::Mat ratioRoi = gray.rowRange(ratioStart, ratioStop);
        cv::Mat blurred, brightness;
        cv::GaussianBlur(ratioRoi, blurred, {7, 7}, 1.5);
        cv::blur(blurred, brightness, {std::max(3, int(std::lround((x1 - x0 + 1) * .05))), 1});

        std::vector<cv::Point2d> points;
        std::vector<double> strengths;
        std::vector<double> usableMaxima;
        for (int x = x0; x <= x1; ++x)
        {
            const int index = x - x0;
            const int columnStop = std::min(y1, int(std::floor(bottom[index])) + 1);
            if (columnStop <= y0)
                continue;
            double maximum = 0.0;
            cv::minMaxLoc(brightness(cv::Rect(x, y0 - ratioStart, 1, columnStop - y0)),
                          nullptr, &maximum);
            if (maximum > std::numeric_limits<double>::epsilon())
                usableMaxima.push_back(maximum);
            const double threshold = maximum - std::max(offset, 0.0);
            for (int outside = columnStop - 1; outside >= y0 + 3; --outside)
            {
                const int local = outside - ratioStart;
                if (brightness.at<uchar>(local, x) < threshold &&
                    brightness.at<uchar>(local - 1, x) >= threshold &&
                    brightness.at<uchar>(local - 2, x) >= threshold &&
                    brightness.at<uchar>(local - 3, x) >= threshold)
                {
                    const double insideValue = brightness.at<uchar>(local - 1, x);
                    const double outsideValue = brightness.at<uchar>(local, x);
                    const double denominator = insideValue - outsideValue;
                    const double fraction = std::abs(denominator) < 1e-12
                                                ? 0.0
                                                : std::clamp((insideValue - threshold) / denominator, 0.0, 1.0);
                    points.emplace_back(x, outside - 1 + fraction);
                    strengths.push_back(maximum);
                    break;
                }
            }
        }
        const int thresholdCrossingCount = int(points.size());
        const double maximumP90 = percentile(usableMaxima, 0.9);
        const double maximumMaximum = usableMaxima.empty() ? 0.0 : *std::max_element(usableMaxima.begin(), usableMaxima.end());
        auto hit = finish(std::move(points), std::move(strengths), s.minEdgePoints,
                          s.fitResidualPx, expectedCenter.x, x1 - x0 + 1,
                          s.minCoverageRatio, true);
        if (hit)
        {
            hit->center = expectedCenter;
            hit->searchStartY = ratioStart;
            hit->searchStopY = ratioStop;
            hit->bottomMarginPx = s.bottomMarginPx;
            hit->trackingHalfHeightPx = trackingHalfHeight;
            hit->brightnessOffset = offset;
            hit->thresholdCrossingCount = thresholdCrossingCount;
            hit->columnMaximumP90 = maximumP90;
            hit->columnMaximumMaximum = maximumMaximum;
        }
        return hit;
    }
}
