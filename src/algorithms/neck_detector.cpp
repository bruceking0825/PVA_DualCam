#include "algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pva::algorithms
{
    struct FitContour
    {
        std::vector<cv::Point> points;
        bool closed{true};
    };

    static std::vector<int> cyclicIndices(int start, int stop, int count)
    {
        std::vector<int> indices;
        if (start <= stop)
        {
            indices.reserve(stop - start + 1);
            for (int index = start; index <= stop; ++index)
                indices.push_back(index);
        }
        else
        {
            indices.reserve(count - start + stop + 1);
            for (int index = start; index < count; ++index)
                indices.push_back(index);
            for (int index = 0; index <= stop; ++index)
                indices.push_back(index);
        }
        return indices;
    }

    static bool canCalculateConvexityDefects(const std::vector<int> &hullIndices, int contourSize)
    {
        if (hullIndices.size() < 3 || contourSize <= 3)
            return false;

        // OpenCV requires the hull indices to follow the contour monotonically.
        // Self-intersecting contours can make convexHull return a non-monotonic
        // sequence; convexityDefects then throws cv::Exception.  Mirror its
        // validation here so an ordinary rejected contour does not throw once
        // per frame while debugging.
        const bool reverseOrientation =
            ((hullIndices[1] > hullIndices[0]) +
             (hullIndices[2] > hullIndices[1]) +
             (hullIndices[0] > hullIndices[2])) != 2;
        int current = hullIndices[reverseOrientation ? 0 : hullIndices.size() - 1];
        if (current < 0 || current >= contourSize)
            return false;

        std::optional<bool> increasing;
        for (std::size_t index = 0; index < hullIndices.size(); ++index)
        {
            const int next = hullIndices[reverseOrientation ? hullIndices.size() - index - 1 : index];
            if (next < 0 || next >= contourSize)
                return false;

            const bool stepIncreases = current < next;
            if (!increasing)
                increasing = !stepIncreases;
            else if (*increasing != stepIncreases)
                return false;
            current = next;
        }
        return true;
    }

    static FitContour extractOuterConvexArc(const std::vector<cv::Point> &contour)
    {
        if (contour.size() < 5)
            return {contour, true};

        std::vector<int> hullIndices;
        cv::convexHull(contour, hullIndices, false, false);
        if (!canCalculateConvexityDefects(hullIndices, static_cast<int>(contour.size())))
            return {contour, true};

        std::vector<cv::Vec4i> defects;
        cv::convexityDefects(contour, hullIndices, defects);
        if (defects.empty())
            return {contour, true};

        const auto deepest = std::max_element(
            defects.begin(), defects.end(),
            [](const cv::Vec4i &left, const cv::Vec4i &right)
            { return left[3] < right[3]; });
        const cv::Rect bounds = cv::boundingRect(contour);
        const double depthPx = (*deepest)[3] / 256.0;
        const double minimumDepth = std::max(3.0, 0.05 * std::min(bounds.width, bounds.height));
        if (depthPx < minimumDepth)
            return {contour, true};

        const int start = (*deepest)[0];
        const int stop = (*deepest)[1];
        const int farthest = (*deepest)[2];
        auto outerIndices = cyclicIndices(start, stop, static_cast<int>(contour.size()));
        if (std::find(outerIndices.begin(), outerIndices.end(), farthest) != outerIndices.end())
            outerIndices = cyclicIndices(stop, start, static_cast<int>(contour.size()));

        std::vector<cv::Point> outerArc;
        outerArc.reserve(outerIndices.size());
        for (const int index : outerIndices)
            outerArc.push_back(contour[index]);
        if (outerArc.size() < 5)
            return {contour, true};
        return {std::move(outerArc), false};
    }

    static std::optional<cv::RotatedRect> fitAxisAlignedEllipse(const std::vector<cv::Point> &contour)
    {
        if (contour.size() < 5)
            return {};
        cv::Point2d origin{};
        for (auto p : contour)
            origin += cv::Point2d(p);
        origin *= 1.0 / contour.size();
        cv::Point2d scale{};
        for (auto p : contour)
        {
            auto d = cv::Point2d(p) - origin;
            scale.x += d.x * d.x;
            scale.y += d.y * d.y;
        }
        scale.x = std::sqrt(scale.x / contour.size());
        scale.y = std::sqrt(scale.y / contour.size());
        if (scale.x <= 1e-12 || scale.y <= 1e-12)
            return {};
        cv::Mat a(int(contour.size()), 4, CV_64F), b(int(contour.size()), 1, CV_64F, cv::Scalar(1));
        for (int i = 0; i < a.rows; ++i)
        {
            double x = (contour[i].x - origin.x) / scale.x, y = (contour[i].y - origin.y) / scale.y;
            a.at<double>(i, 0) = x * x;
            a.at<double>(i, 1) = y * y;
            a.at<double>(i, 2) = x;
            a.at<double>(i, 3) = y;
        }
        cv::Mat c;
        if (!cv::solve(a, b, c, cv::DECOMP_SVD))
            return {};
        double xx = c.at<double>(0), yy = c.at<double>(1), cx = c.at<double>(2), cy = c.at<double>(3);
        if (xx <= 0 || yy <= 0)
            return {};
        double term = 1 + cx * cx / (4 * xx) + cy * cy / (4 * yy);
        if (term <= 0)
            return {};
        cv::Point2d center(origin.x - cx / (2 * xx) * scale.x, origin.y - cy / (2 * yy) * scale.y);
        cv::Size2f size(float(2 * std::sqrt(term / xx) * scale.x), float(2 * std::sqrt(term / yy) * scale.y));
        return cv::RotatedRect(center, size, 0);
    }
    std::optional<EllipseHit> findNeckEllipse(const cv::Mat &gray, double threshold, double minArea, double startRatio, double stopRatio, std::optional<double> expectedX)
    {
        int y0 = std::clamp(int(std::lround(gray.rows * startRatio)), 0, std::max(gray.rows - 1, 0)), y1 = std::clamp(int(std::lround(gray.rows * stopRatio)), y0 + 1, gray.rows);
        cv::Mat blur, gx, gy, mag, binary;
        cv::GaussianBlur(gray.rowRange(y0, y1), blur, {5, 5}, 0);
        cv::Sobel(blur, gx, CV_32F, 1, 0, 3);
        cv::Sobel(blur, gy, CV_32F, 0, 1, 3);
        cv::magnitude(gx, gy, mag);
        cv::threshold(mag, binary, std::max(threshold, 0.0), 255, cv::THRESH_BINARY);
        binary.convertTo(binary, CV_8U);
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, cv::Mat::ones(3, 3, CV_8U));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);
        std::optional<EllipseHit> best;
        double bestScore = -std::numeric_limits<double>::infinity(), imageArea = double((y1 - y0) * gray.cols);
        for (auto contour : contours)
        {
            if (contour.size() < 5)
                continue;
            double area = std::abs(cv::contourArea(contour));
            if (area < minArea || area > imageArea * .8)
                continue;
            // 与 Python 一致，只使用最大凸缺陷另一侧的外侧弧拟合椭圆。
            auto fitContour = extractOuterConvexArc(contour);
            auto fit = fitAxisAlignedEllipse(fitContour.points);
            if (!fit)
                continue;
            auto ellipse = *fit;
            if (std::min(ellipse.size.width, ellipse.size.height) < 4 || std::max(ellipse.size.width, ellipse.size.height) > std::max(gray.rows, gray.cols) * 1.5)
                continue;
            ellipse.center.y += float(y0);
            for (auto &p : fitContour.points)
                p.y += y0;
            const double perimeter = std::max(cv::arcLength(contour, true), 1e-6);
            const double xPenalty = expectedX ? std::abs(ellipse.center.x - *expectedX) : 0.0;
            const double score = std::sqrt(area) + 400 * CV_PI * area / (perimeter * perimeter) - xPenalty * 0.1;
            if (score > bestScore)
            {
                bestScore = score;
                best = EllipseHit{ellipse, std::move(fitContour.points), area, fitContour.closed};
            }
        }
        return best;
    }
}
