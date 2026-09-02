#pragma once

#include <QString>
#include <opencv2/core.hpp>

namespace pva
{

    struct RuntimeSettings
    {
        bool disableCameraForPlcTest{false};
        bool connectPlcInOffline{false};
        int idleSampleIntervalMs{1000};
        int neckSampleIntervalMs{300};
        int crownSampleIntervalMs{1000};
        int bodySampleIntervalMs{1000};
        int endconeSampleIntervalMs{1000};
        QString offlineImageDir{"../live_img"};
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
        double brightnessMin{100.0};
        double brightnessMax{255.0};
        double diameterMinMm{0.0};
        double diameterMaxMm{350.0};
        double lightAlpha{0.2};
        double mmPerPixelAlpha{0.5};
        cv::Rect autoExposureRoiCamera1{0, 0, 512, 512};
        cv::Rect autoExposureRoiCamera2{0, 0, 512, 512};
    };

    struct NeckSettings
    {
        double minContourAreaPx{80.0};
        int minEdgePoints{24};
        double reflectorThresholdCamera1{150.0};
        double reflectorThresholdCamera2{150.0};
        double reflectorBottomSearchTopRatio{0.55};
        double reflectorBottomSearchBottomRatio{0.95};
        double reflectorSideScoreMaxFactor{0.5};
        int reflectorBottomMinPoints{40};
        double reflectorFlatMaxSagPx{25.0};
        double gradientThresholdCamera1{70.0};
        double gradientThresholdCamera2{70.0};
        double startSearchRatio{0.0};
        double stopSearchRatio{0.65};
        double pixelsPerMm{24.0};
        double diameterAlpha{0.5};
    };

    struct CrownSettings
    {
        int minEdgePoints{24};
        double columnMaxFactor{0.5};
        bool usePreviousBoundaryY{true};
        int searchHalfHeightPx{300};
        int horizontalMarginPx{40};
        int bottomMarginPx{100};
        double fitResidualPx{10.0};
    };

    struct BodySettings
    {
        int minEdgePoints{24};
        double brightnessOffsetCamera1{6.0};
        double brightnessOffsetCamera2{15.0};
        double startSearchRatio{0.0};
        double stopSearchRatio{1.0};
        bool usePreviousBoundaryY{true};
        int searchHalfHeightPx{300};
        int horizontalMarginPx{40};
        int bottomMarginPx{100};
        double minCoverageRatio{0.55};
        double fitResidualPx{10.0};
    };

    struct EndconeSettings
    {
        double diameterAlpha{0.2};
        int boundaryOffsetPx{0};
    };

    struct MeasurementConfig
    {
        RuntimeSettings runtime;
        CameraSettings camera;
        MeasurementSettings measurement;
        NeckSettings neck;
        CrownSettings crown;
        BodySettings body;
        EndconeSettings endcone;

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
