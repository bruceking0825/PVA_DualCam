#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <opencv2/core.hpp>
#include <QMetaType>
#include <QVariant>
#include <QStringList>

namespace pva
{
    // 相机角色是共用数据，不依赖设备服务或 SDK。
    namespace CameraRole
    {
        inline const QString Cam1 = QStringLiteral("cam1");
        inline const QString Cam2 = QStringLiteral("cam2");
        inline const QStringList Stereo{Cam1, Cam2};
    }

    enum class MeasurementStage : int
    {        Melt = 1,
        Dip = 2,
        Neck = 3,
        Crown = 4,
        Body = 5
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
        std::optional<std::array<cv::Point2d, 2>> crownBoundaryPointsPx;
        std::optional<std::array<cv::Point2d, 2>> bodyBoundaryPointsPx;
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
        bool dashed{false};
    };

    // 领域输出：字段具有明确含义，PLC 字段顺序由适配器维护。
    struct GrayStats { double average{}, maximum{}, minimum{}; };
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
        std::optional<std::chrono::steady_clock::time_point> triggerStarted;
    };

    struct MeasurementResult
    {
        quint64 generation{};
        MeasurementTaskInfo task;
        bool valid{false};
        MeasurementStage stage{MeasurementStage::Melt};
        MeasurementValues values;
        MeasurementData data;
        std::array<std::optional<GrayStats>, 2> regionStatistics;
        std::unordered_map<std::string, QVariant> diagnostics;
        cv::Mat preview1;
        cv::Mat preview2;
        std::vector<OverlayElement> overlay1;
        std::vector<OverlayElement> overlay2;
        std::string message;
    };

} // namespace pva

Q_DECLARE_METATYPE(pva::MeasurementResult)
Q_DECLARE_METATYPE(pva::MeasurementStage)
