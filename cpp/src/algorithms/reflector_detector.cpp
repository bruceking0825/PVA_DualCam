#include "pva/algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace pva::algorithms
{
    namespace
    {
        double percentile(std::vector<double> values, double fraction)
        {
            if (values.empty())
                return 0.0;
            const size_t index = std::min(values.size() - 1,
                                          static_cast<size_t>(std::floor(fraction * (values.size() - 1))));
            std::nth_element(values.begin(), values.begin() + index, values.end());
            return values[index];
        }

        bool fitPolynomial(const std::vector<cv::Point2d> &points, int degree, cv::Mat &coefficients)
        {
            if (points.size() < static_cast<size_t>(degree + 1))
                return false;
            cv::Mat design(static_cast<int>(points.size()), degree + 1, CV_64F);
            cv::Mat values(static_cast<int>(points.size()), 1, CV_64F);
            for (int row = 0; row < design.rows; ++row)
            {
                double power = 1.0;
                for (int column = degree; column >= 0; --column)
                {
                    design.at<double>(row, column) = power;
                    power *= points[row].x;
                }
                values.at<double>(row) = points[row].y;
            }
            return cv::solve(design, values, coefficients, cv::DECOMP_SVD);
        }

        double polynomialValue(const cv::Mat &coefficients, double x)
        {
            double result = 0.0;
            for (int index = 0; index < coefficients.rows; ++index)
                result = result * x + coefficients.at<double>(index);
            return result;
        }

        std::vector<cv::Point2d> robustBottomCurve(std::vector<cv::Point2d> points, cv::Mat &quadratic)
        {
            fitPolynomial(points, 2, quadratic);
            for (int iteration = 0; iteration < 3; ++iteration)
            {
                std::vector<double> residuals;
                residuals.reserve(points.size());
                for (const auto &point : points)
                    residuals.push_back(point.y - polynomialValue(quadratic, point.x));
                const double center = percentile(residuals, 0.5);
                std::vector<double> deviations;
                deviations.reserve(residuals.size());
                for (const double residual : residuals)
                    deviations.push_back(std::abs(residual - center));
                const double limit = std::max(20.0, 3.0 * 1.4826 * percentile(deviations, 0.5));
                std::vector<cv::Point2d> kept;
                for (size_t index = 0; index < points.size(); ++index)
                    if (std::abs(residuals[index] - center) <= limit)
                        kept.push_back(points[index]);
                if (kept.size() < 5 || kept.size() == points.size())
                    break;
                points = std::move(kept);
                fitPolynomial(points, 2, quadratic);
            }
            return points;
        }

        std::vector<cv::Point2d> ellipsePoints(const cv::RotatedRect &ellipse)
        {
            std::vector<cv::Point2d> points;
            points.reserve(181);
            const double angle = ellipse.angle * CV_PI / 180.0;
            const double cosine = std::cos(angle), sine = std::sin(angle);
            for (int index = 0; index < 181; ++index)
            {
                const double parameter = 2.0 * CV_PI * index / 181.0;
                const double x = ellipse.size.width * 0.5 * std::cos(parameter);
                const double y = ellipse.size.height * 0.5 * std::sin(parameter);
                points.emplace_back(ellipse.center.x + x * cosine - y * sine,
                                    ellipse.center.y + x * sine + y * cosine);
            }
            return points;
        }

        size_t nearestIndex(const std::vector<cv::Point2d> &points, cv::Point2d target)
        {
            size_t best = 0;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (size_t index = 0; index < points.size(); ++index)
            {
                const double distance = cv::norm(points[index] - target);
                if (distance < bestDistance)
                {
                    best = index;
                    bestDistance = distance;
                }
            }
            return best;
        }

        std::vector<cv::Point2d> cyclicSlice(const std::vector<cv::Point2d> &points, size_t start, size_t stop)
        {
            std::vector<cv::Point2d> result;
            for (size_t index = start;; index = (index + 1) % points.size())
            {
                result.push_back(points[index]);
                if (index == stop)
                    break;
            }
            return result;
        }
    }

    std::optional<ReflectorHit> findReflectorBottom(const cv::Mat &gray, double threshold, const NeckSettings &settings)
    {
        if (gray.empty())
            return {};
        const double topRatio = std::clamp(settings.reflectorBottomSearchTopRatio, 0.05, 0.9);
        const double bottomRatio = std::clamp(settings.reflectorBottomSearchBottomRatio, topRatio + 0.05, 0.99);
        const int y0 = std::clamp(cvRound(gray.rows * topRatio), 0, gray.rows - 1);
        const int y1 = std::clamp(cvRound(gray.rows * bottomRatio), y0 + 1, gray.rows);
        if (y1 - y0 < 20 || gray.cols < 40)
            return {};

        cv::Mat blurred, gradientX;
        cv::GaussianBlur(gray.rowRange(y0, y1), blurred, {9, 9}, 2.0);
        cv::Sobel(blurred, gradientX, CV_32F, 1, 0, 3);
        gradientX = cv::abs(gradientX);
        cv::Mat sideMean;
        cv::reduce(gradientX, sideMean, 0, cv::REDUCE_AVG, CV_32F);
        double sideMaximum = 0.0;
        cv::minMaxLoc(sideMean, nullptr, &sideMaximum);
        const double sideThreshold = sideMaximum * std::max(settings.reflectorSideScoreMaxFactor, 0.0);
        const int middle = gray.cols / 2;
        int leftX = -1, rightX = -1;
        for (int x = middle - 1; x >= 0; --x)
            if (sideMean.at<float>(x) > sideThreshold)
            {
                leftX = x;
                break;
            }
        for (int x = middle + 1; x < gray.cols; ++x)
            if (sideMean.at<float>(x) > sideThreshold)
            {
                rightX = x;
                break;
            }
        const int span = rightX - leftX;
        if (leftX < 0 || rightX < 0 || span < std::max(20, cvRound(gray.cols * 0.2)))
            return {};

        const int sideMargin = std::max(4, cvRound(span * 0.03));
        cv::Mat gradientY;
        cv::Sobel(blurred, gradientY, CV_32F, 0, 1, 3);
        struct Candidate
        {
            cv::Point2d point;
            double strength;
        };
        std::vector<Candidate> candidates;
        std::vector<double> strengths;
        const int sampleOffset = std::max(2, cvRound(gray.rows * 0.002));
        for (int x = leftX + sideMargin; x <= rightX - sideMargin; ++x)
        {
            double minimum = 0.0;
            cv::Point location;
            cv::minMaxLoc(gradientY.col(x), &minimum, nullptr, &location, nullptr);
            const int localY = location.y;
            const double strength = -minimum;
            const int aboveY = std::max(0, localY - sampleOffset);
            const int belowY = std::min(blurred.rows - 1, localY + sampleOffset);
            if (blurred.at<uchar>(aboveY, x) > blurred.at<uchar>(belowY, x) &&
                blurred.at<uchar>(aboveY, x) >= threshold * 0.75)
            {
                candidates.push_back({cv::Point2d(x, y0 + localY), strength});
                strengths.push_back(strength);
            }
        }
        const double minimumStrength = std::max(2.0, percentile(strengths, 0.1) * 0.5);
        std::vector<cv::Point2d> points;
        for (const auto &candidate : candidates)
            if (candidate.strength >= minimumStrength)
                points.push_back(candidate.point);
        if (points.size() < static_cast<size_t>(settings.reflectorBottomMinPoints))
            return {};

        cv::Mat quadratic;
        points = robustBottomCurve(std::move(points), quadratic);
        if (points.size() < static_cast<size_t>(settings.reflectorBottomMinPoints))
            return {};
        cv::Mat line;
        if (!fitPolynomial(points, 1, line))
            return {};
        const double centerX = (leftX + rightX) * 0.5;
        const double sagitta = polynomialValue(quadratic, centerX) -
                                0.5 * (polynomialValue(quadratic, leftX) + polynomialValue(quadratic, rightX));

        ReflectorHit hit;
        hit.edgePoints = points;
        hit.area = span;
        if (sagitta <= settings.reflectorFlatMaxSagPx)
        {
            hit.roi.leftBoundary = {double(leftX), polynomialValue(line, leftX)};
            hit.roi.rightBoundary = {double(rightX), polynomialValue(line, rightX)};
            hit.roi.bottomCurve = {hit.roi.leftBoundary, hit.roi.rightBoundary};
            hit.roi.center = (hit.roi.leftBoundary + hit.roi.rightBoundary) * 0.5;
            return hit;
        }

        std::vector<cv::Point2f> floatPoints;
        floatPoints.reserve(points.size());
        for (const auto &point : points)
            floatPoints.emplace_back(point);
        if (floatPoints.size() < 5)
            return {};
        const cv::RotatedRect ellipse = cv::fitEllipse(floatPoints);
        const auto sampled = ellipsePoints(ellipse);
        const cv::Point2d leftTarget(leftX, polynomialValue(quadratic, leftX));
        const cv::Point2d rightTarget(rightX, polynomialValue(quadratic, rightX));
        const size_t leftIndex = nearestIndex(sampled, leftTarget);
        const size_t rightIndex = nearestIndex(sampled, rightTarget);
        auto forward = cyclicSlice(sampled, leftIndex, rightIndex);
        auto backward = cyclicSlice(sampled, rightIndex, leftIndex);
        std::reverse(backward.begin(), backward.end());
        auto maximumY = [](const auto &curve)
        { return std::max_element(curve.begin(), curve.end(), [](auto a, auto b) { return a.y < b.y; })->y; };
        hit.roi.bottomCurve = maximumY(forward) >= maximumY(backward) ? std::move(forward) : std::move(backward);
        if (hit.roi.bottomCurve.front().x > hit.roi.bottomCurve.back().x)
            std::reverse(hit.roi.bottomCurve.begin(), hit.roi.bottomCurve.end());
        hit.roi.leftBoundary = hit.roi.bottomCurve.front();
        hit.roi.rightBoundary = hit.roi.bottomCurve.back();
        hit.roi.center = *std::max_element(hit.roi.bottomCurve.begin(), hit.roi.bottomCurve.end(),
                                          [](auto a, auto b) { return a.y < b.y; });
        return hit;
    }
}
