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
        Melt = 1,
        Dip = 2,
        Neck = 3,
        Crown = 4,
        Body = 5,
        Endcone = 6
    };

    struct MeasurementValues
    {
        std::optional<double> diameterMm;
        [[nodiscard]] bool complete() const { return diameterMm.has_value(); }
    };

    struct MeasurementState
    {
        MeasurementValues values;
        cv::Vec2d filteredLight{0.0, 0.0};
        std::optional<std::array<cv::Point2d, 2>> neckCentersPx;
        std::optional<std::array<cv::Vec2i, 2>> neckYSpans;
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

    // 领域输出：字段具有明确含义，PLC 字段顺序由适配器维护。
    struct CameraMeasurement
    {
        double diameter{}, boundaryX{};
        double average{}, maximum{}, minimum{};
        cv::Point2d center;
    };
    struct MeasurementData
    {
        std::array<CameraMeasurement, 2> cameras;
        double meltCount{}, dipAverage{};
        bool hasDiameter{}, hasMelt{}, hasDip{};
    };

    enum class MeasurementSource { Offline, Online };
    struct MeasurementTaskInfo
    {
        quint64 runId{}, requestId{}, configurationVersion{};
        MeasurementSource source{MeasurementSource::Offline};
    };

    struct MeasurementResult
    {
        quint64 generation{};
        MeasurementTaskInfo task;
        bool valid{false};
        MeasurementStage stage{MeasurementStage::Idle};
        MeasurementValues values;
        MeasurementData data;
        std::unordered_map<std::string, QVariant> diagnostics;
        cv::Mat preview1;
        cv::Mat preview2;
        std::vector<OverlayElement> overlay1;
        std::vector<OverlayElement> overlay2;
        std::string message;
    };

} // namespace pva

Q_DECLARE_METATYPE(pva::MeasurementResult)
