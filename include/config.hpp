#pragma once

#include <QString>
#include <opencv2/core.hpp>

namespace pva
{

    struct RuntimeSettings
    {
        bool disableCameraForPlcTest{false};
        int neckSampleIntervalMs{300};
        int crownSampleIntervalMs{1000};
        int bodySampleIntervalMs{1000};
        QString offlineImageDir{"../live_img"};
        QString facetteImageDir{"D:/data/EKZData/FACETTES"};
        int loopIntervalMs{500};
        QString stateFile{"measurement_state.json"};
        int stereoPairMaxDeltaMs{1000};
    };

    struct CameraSettings
    {
        double initialExposureCamera1{10000.0};
        double gainCamera1{1.0};
        double initialExposureCamera2{10000.0};
        double gainCamera2{1.0};
        cv::Rect offlineCropRoi{0, 0, 5120, 5120};
        cv::Rect onlineCropRoi{1700, 0, 1600, 5120};
        bool autoExposureEnabled{true};
        double autoExposureTarget{120.0};
        double autoExposureMinUs{1000.0};
        double autoExposureMaxUs{50000.0};
        double autoExposureGain{0.2};
        double autoExposureDeadband{3.0};
        int autoExposureIntervalMs{500};
    };

    struct MeasurementSettings
    {
        double diaRectHeightPx{80.0};
        double crownBodyInnerRadiusPx{400.0};
        double brightnessMin{100.0};
        double brightnessMax{255.0};
        double diameterMinMm{0.0};
        double diameterMaxMm{350.0};
        double lightAlpha{0.2};
        cv::Rect autoExposureRoiCamera1{0, 0, 512, 512};
        cv::Rect autoExposureRoiCamera2{0, 0, 512, 512};
        cv::Rect reflectorRoiCamera1{0, 0, 512, 512};
        cv::Rect reflectorRoiCamera2{0, 0, 512, 512};
    };

    struct NeckSettings
    {
        double minContourAreaPx{80.0};
        int minEdgePoints{24};
        double gradientThresholdPercentCamera1{50.0};
        double gradientThresholdPercentCamera2{50.0};
        double ellipseWidthHeightRatioCamera2{1.0}; // 相机 2 椭圆宽/高，由配置独立指定。
        double startSearchRatio{0.0};
        double stopSearchRatio{0.65};
        double pixelsPerMm{24.0};
        double diameterAlpha{0.5};
    };

    struct CrownSettings
    {
        int minEdgePoints{24};
        double gradientThresholdRatio{0.5};
        bool usePreviousBoundaryX{true};
        int searchHalfWidthPx{300};
        int verticalMarginPx{40};
        int leftMarginPx{100};
        double fitResidualPx{10.0};
    };

    struct BodySettings
    {
        int minEdgePoints{24};
        double brightnessThresholdPercentCamera1{80.0};
        double brightnessThresholdPercentCamera2{80.0};
        double startSearchRatio{0.0};
        double stopSearchRatio{1.0};
        bool usePreviousBoundaryX{true};
        int searchHalfWidthPx{300};
        int verticalMarginPx{40};
        int leftMarginPx{100};
        double minCoverageRatio{0.55};
        double fitResidualPx{10.0};
    };

    struct MeasurementConfig
    {
        RuntimeSettings runtime;
        CameraSettings camera;
        MeasurementSettings measurement;
        NeckSettings neck;
        CrownSettings crown;
        BodySettings body;

        static MeasurementConfig loadIni(const QString &path);
    };

    // Result metadata returned by the shared configuration registry.
    // Unknown entries remain valid INI entries, but are not copied into the
    // typed runtime model until they are registered.
    struct ConfigEntryUpdate
    {
        bool recognized{false};
        bool changed{false};
    };

    // Apply one textual INI value through the same registry used by loadIni().
    // Returns false only when a registered entry contains an invalid value.
    bool applyConfigEntry(MeasurementConfig &config,
                          const QString &configPath,
                          const QString &group,
                          const QString &key,
                          const QString &value,
                          ConfigEntryUpdate *update = nullptr,
                          QString *error = nullptr);

} // namespace pva
