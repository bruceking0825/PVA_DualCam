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
    };
    struct CurveHit
    {
        std::vector<cv::Point2d> edges, curve;
        cv::Point2d boundary;
        cv::Point2d center;
        double coverage{};
        double seedY{};
        double fitErrorPx{};
        double fitStrengthMean{};
        double columnStrengthsMean{};
        double columnStrengthsMaximum{};
        double minimumStrength{};
        double residualLimitPx{};
        double sagittaPx{};
        int keptColumnCount{};
        int edgePointCount{};
        int robustInlierCount{};
        int searchStartY{};
        int searchStopY{};
        int thresholdCrossingCount{};
        double bottomMarginPx{};
        double trackingHalfHeightPx{};
        double brightnessOffset{};
        double columnMaximumP90{};
        double columnMaximumMaximum{};
    };
    struct EndconeHit
    {
        double boundaryY{};
        double diameterMm{};
        int x0{}, x1{};
    };

    cv::Mat normalizeGray8(const cv::Mat &source);
    DetectionResult<EllipseHit> findNeckEllipse(const cv::Mat &gray, const cv::Rect &roi, double threshold, double minArea, double startRatio, double stopRatio, std::optional<double> expectedX);
    DetectionResult<CurveHit> findCrownMeniscus(const cv::Mat &gray, const cv::Rect &roi, cv::Point2d expectedCenter, const CrownSettings &settings, std::optional<double> previousY);
    DetectionResult<CurveHit> findBodyMeniscus(const cv::Mat &gray, const cv::Rect &roi, cv::Point2d expectedCenter, const BodySettings &settings, double brightnessOffset, std::optional<double> previousY);
    DetectionResult<EndconeHit> findEndcone(const cv::Mat &gray, cv::Point2d bodyCenter, cv::Vec2i neckSpan, double mmPerPixel, const EndconeSettings &settings);
}
