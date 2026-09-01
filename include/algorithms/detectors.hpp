#pragma once
#include "config.hpp"
#include "models.hpp"
#include <optional>
#include <vector>

namespace pva::algorithms
{
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
    struct ReflectorHit
    {
        pva::ReflectorRoi roi;
        std::vector<cv::Point2d> edgePoints;
        double area{};
    };
    struct EndconeHit
    {
        double boundaryY{};
        double diameterMm{};
        int x0{}, x1{};
    };

    cv::Mat normalizeGray8(const cv::Mat &source);
    std::optional<EllipseHit> findNeckEllipse(const cv::Mat &gray, double threshold, double minArea, double startRatio, double stopRatio, std::optional<double> expectedX);
    std::optional<ReflectorHit> findReflectorBottom(const cv::Mat &gray, double threshold, const NeckSettings &settings);
    std::optional<CurveHit> findCrownMeniscus(const cv::Mat &gray, const ReflectorRoi &roi, cv::Point2d expectedCenter, const CrownSettings &settings, std::optional<double> previousY);
    std::optional<CurveHit> findBodyMeniscus(const cv::Mat &gray, const ReflectorRoi &roi, cv::Point2d expectedCenter, const BodySettings &settings, double brightnessOffset, std::optional<double> previousY);
    std::optional<EndconeHit> findEndcone(const cv::Mat &gray, cv::Point2d bodyCenter, cv::Vec2i neckSpan, double mmPerPixel, const EndconeSettings &settings);
}
