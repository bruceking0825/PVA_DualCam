#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <opencv2/core.hpp>
#include <QMetaType>
#include <QVariant>

namespace pva
{

    enum class MeasurementStage : int
    {
        Idle = 0,
        Neck = 1,
        Crown = 2,
        Endcone = 3,
        Body = 4
    };

    struct MeasurementValues
    {
        std::optional<double> diameterMm;
        [[nodiscard]] bool complete() const { return diameterMm.has_value(); }
    };

    struct ReflectorRoi
    {
        cv::Point2d center;
        cv::Point2d leftBoundary;
        cv::Point2d rightBoundary;
        std::vector<cv::Point2d> bottomCurve;
    };

    struct MeasurementState
    {
        MeasurementValues values;
        cv::Vec2d filteredLight{0.0, 0.0};
        std::optional<std::array<cv::Point2d, 2>> neckCentersPx;
        std::optional<std::array<cv::Vec2i, 2>> neckXSpans;
        std::optional<std::array<ReflectorRoi, 2>> neckReflectorRois;
        std::optional<std::array<cv::Point2d, 2>> crownBoundaryPointsPx;
        std::optional<std::array<cv::Point2d, 2>> bodyCentersPx;
        std::optional<std::array<cv::Point2d, 2>> bodyBoundaryPointsPx;
        std::optional<double> mmPerPixel;
        bool validNeck{false};
    };

    enum class OverlayType
    {
        Polyline,
        Cross,
        Line
    };

    struct OverlayElement
    {
        OverlayType type{OverlayType::Polyline};
        std::vector<cv::Point2d> points;
        cv::Scalar colorBgr{0, 255, 0};
        int width{2};
        bool closed{false};
    };

    struct MeasurementResult
    {
        bool valid{false};
        MeasurementStage stage{MeasurementStage::Idle};
        MeasurementValues values;
        std::unordered_map<std::string, QVariant> diagnostics;
        cv::Mat preview1;
        cv::Mat preview2;
        std::vector<OverlayElement> overlay1;
        std::vector<OverlayElement> overlay2;
        std::string message;
    };

} // namespace pva

Q_DECLARE_METATYPE(pva::MeasurementResult)
