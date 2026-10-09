#pragma once
#include "config.hpp"
#include "models.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pva::algorithms
{
    template <typename T>
    struct DetectionResult
    {
        std::optional<T> hit;
        std::string error;

        [[nodiscard]] static DetectionResult success(T value)
        {
            return {std::move(value), {}};
        }

        [[nodiscard]] static DetectionResult failure(std::string message)
        {
            return {std::nullopt, std::move(message)};
        }

        [[nodiscard]] bool has_value() const noexcept { return hit.has_value(); }
        [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
        [[nodiscard]] T &operator*() { return *hit; }
        [[nodiscard]] const T &operator*() const { return *hit; }
        [[nodiscard]] T *operator->() { return &*hit; }
        [[nodiscard]] const T *operator->() const { return &*hit; }
    };

    struct EllipseHit
    {
        cv::RotatedRect ellipse;
        std::vector<cv::Point> contour;
        double area{};
        bool contourClosed{true};
        double maximumGradient{};
    };
    struct CurveHit
    {
        std::vector<cv::Point2d> edges, curve;
        cv::Point2d boundary;
        cv::Point2d center;
        double coverage{};
        double seedX{};
        double fitErrorPx{};
        double fitStrengthMean{};
        double rowStrengthsMean{};
        double rowStrengthsMaximum{};
        double minimumStrength{};
        double residualLimitPx{};
        double sagittaPx{};
        int keptRowCount{};
        int edgePointCount{};
        int robustInlierCount{};
        int searchStartX{};
        int searchStopX{};
        int thresholdCrossingCount{};
        double leftMarginPx{};
        double trackingHalfWidthPx{};
        double brightnessThresholdPercent{};
        double rowMaximumP90{};
        double rowMaximumMaximum{};
    };

    cv::Mat normalizeGray8(const cv::Mat &source);
    DetectionResult<EllipseHit> findNeckEllipse(const cv::Mat &gray, const cv::Rect &roi, double thresholdPercent, double minArea, double startRatio, double stopRatio, std::optional<double> expectedY, std::optional<double> widthToHeightRatio = {});
    DetectionResult<CurveHit> findCrownMeniscus(const cv::Mat &gray, const cv::Rect &roi, cv::Point2d expectedCenter, const CrownSettings &settings, std::optional<double> previousX);
    DetectionResult<CurveHit> findBodyMeniscus(const cv::Mat &gray, const cv::Rect &roi, cv::Point2d expectedCenter, const BodySettings &settings, double brightnessThresholdPercent, std::optional<double> previousX);
}
