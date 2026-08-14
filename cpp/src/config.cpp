#include "pva/config.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDir>
#include <QTextStream>
#include <stdexcept>

namespace
{
    using Values = QHash<QString, QString>;

    QString clean(QString value)
    {
        value = value.trimmed();
        if (value.startsWith('[') && value.endsWith(']'))
            value = value.mid(1, value.size() - 2);
        return value.trimmed();
    }

    Values readIni(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            throw std::runtime_error(("Cannot open cnf: " + path).toStdString());
        Values values;
        QString group;
        QTextStream stream(&file);
        while (!stream.atEnd())
        {
            const QString raw = stream.readLine().trimmed();
            if (raw.isEmpty() || raw.startsWith('#') || raw.startsWith(';'))
                continue;
            if (raw.startsWith('[') && raw.endsWith(']'))
            {
                group = raw.mid(1, raw.size() - 2);
                continue;
            }
            const qsizetype split = raw.indexOf('=');
            if (split < 0)
                continue;
            values.insert(group + '/' + raw.left(split).trimmed(), clean(raw.mid(split + 1)));
        }
        return values;
    }

    double number(const Values &v, const QString &key, double fallback)
    {
        bool ok = false;
        const double result = v.value(key).toDouble(&ok);
        return ok ? result : fallback;
    }
    int integer(const Values &v, const QString &key, int fallback)
    {
        bool ok = false;
        const int result = v.value(key).toInt(&ok);
        return ok ? result : fallback;
    }
    bool boolean(const Values &v, const QString &key, bool fallback) { return integer(v, key, fallback ? 1 : 0) != 0; }
    cv::Rect roi(const Values &v, const QString &key, cv::Rect fallback)
    {
        const auto parts = v.value(key).split(',', Qt::SkipEmptyParts);
        if (parts.size() != 4)
            return fallback;
        bool ok[4]{};
        int n[4]{};
        for (int i = 0; i < 4; ++i)
            n[i] = parts[i].trimmed().toInt(&ok[i]);
        return ok[0] && ok[1] && ok[2] && ok[3] ? cv::Rect(n[0], n[1], n[2], n[3]) : fallback;
    }
}

namespace pva
{
    MeasurementConfig MeasurementConfig::loadIni(const QString &path)
    {
        MeasurementConfig c;
        const Values v = readIni(path);
        c.runtime.disableCameraForPlcTest = boolean(v, "Runtime/disable_camera_for_plc_test", c.runtime.disableCameraForPlcTest);
        c.runtime.connectPlcInOffline = boolean(v, "Runtime/connect_plc_in_offline", c.runtime.connectPlcInOffline);
        c.runtime.idleSampleIntervalMs = integer(v, "Runtime/idle_sample_interval_ms", c.runtime.idleSampleIntervalMs);
        c.runtime.neckSampleIntervalMs = integer(v, "Runtime/neck_sample_interval_ms", c.runtime.neckSampleIntervalMs);
        c.runtime.crownSampleIntervalMs = integer(v, "Runtime/crown_sample_interval_ms", c.runtime.crownSampleIntervalMs);
        c.runtime.bodySampleIntervalMs = integer(v, "Runtime/body_sample_interval_ms", c.runtime.bodySampleIntervalMs);
        c.runtime.endconeSampleIntervalMs = integer(v, "Runtime/endcone_sample_interval_ms", c.runtime.endconeSampleIntervalMs);
        c.runtime.offlineImageDir = v.value("Runtime/offline_image_dir", c.runtime.offlineImageDir);
        c.runtime.loopIntervalMs = integer(v, "Runtime/loop_interval_ms", c.runtime.loopIntervalMs);
        c.runtime.stateFile = v.value("Runtime/state_file", c.runtime.stateFile);
        c.runtime.stereoPairMaxDeltaMs = integer(v, "Runtime/stereo_pair_max_delta_ms", c.runtime.stereoPairMaxDeltaMs);
        c.camera.initialExposureCamera1 = number(v, "Camera/initial_exposure_camera1", c.camera.initialExposureCamera1);
        c.camera.gainCamera1 = number(v, "Camera/gain_camera1", c.camera.gainCamera1);
        c.camera.initialExposureCamera2 = number(v, "Camera/initial_exposure_camera2", c.camera.initialExposureCamera2);
        c.camera.gainCamera2 = number(v, "Camera/gain_camera2", c.camera.gainCamera2);
        c.camera.offlineCropRoi = roi(v, "Camera/offline_crop_roi", c.camera.offlineCropRoi);
        c.camera.onlineCropRoi = roi(v, "Camera/online_crop_roi", c.camera.onlineCropRoi);
        c.camera.autoExposureEnabled = boolean(v, "Camera/auto_exposure_enabled", c.camera.autoExposureEnabled);
        c.camera.autoExposureTarget = number(v, "Camera/auto_exposure_target", c.camera.autoExposureTarget);
        c.camera.autoExposureMinUs = number(v, "Camera/auto_exposure_min_us", c.camera.autoExposureMinUs);
        c.camera.autoExposureMaxUs = number(v, "Camera/auto_exposure_max_us", c.camera.autoExposureMaxUs);
        c.camera.autoExposureGain = number(v, "Camera/auto_exposure_gain", c.camera.autoExposureGain);
        c.camera.autoExposureDeadband = number(v, "Camera/auto_exposure_deadband", c.camera.autoExposureDeadband);
        c.camera.autoExposureIntervalMs = integer(v, "Camera/auto_exposure_interval_ms", c.camera.autoExposureIntervalMs);
        c.measurement.brightnessMin = number(v, "Measurement/brightness_min", c.measurement.brightnessMin);
        c.measurement.brightnessMax = number(v, "Measurement/brightness_max", c.measurement.brightnessMax);
        c.measurement.diameterMinMm = number(v, "Measurement/diameter_min_mm", c.measurement.diameterMinMm);
        c.measurement.diameterMaxMm = number(v, "Measurement/diameter_max_mm", c.measurement.diameterMaxMm);
        c.measurement.lightAlpha = number(v, "Measurement/light_alpha", c.measurement.lightAlpha);
        c.measurement.mmPerPixelAlpha = number(v, "Measurement/mm_per_pixel_alpha", c.measurement.mmPerPixelAlpha);
        c.measurement.autoExposureRoiCamera1 = roi(v, "Measurement/auto_exposure_roi_camera1", c.measurement.autoExposureRoiCamera1);
        c.measurement.autoExposureRoiCamera2 = roi(v, "Measurement/auto_exposure_roi_camera2", c.measurement.autoExposureRoiCamera2);
        c.neck.minContourAreaPx = number(v, "Neck/min_contour_area_px", c.neck.minContourAreaPx);
        c.neck.minEdgePoints = integer(v, "Neck/neck_min_edge_points", c.neck.minEdgePoints);
        c.neck.reflectorThresholdCamera1 = number(v, "Neck/reflector_threshold_cam1", c.neck.reflectorThresholdCamera1);
        c.neck.reflectorThresholdCamera2 = number(v, "Neck/reflector_threshold_cam2", c.neck.reflectorThresholdCamera2);
        c.neck.reflectorBottomSearchTopRatio = number(v, "Neck/reflector_bottom_search_top_ratio", c.neck.reflectorBottomSearchTopRatio);
        c.neck.reflectorBottomSearchBottomRatio = number(v, "Neck/reflector_bottom_search_bottom_ratio", c.neck.reflectorBottomSearchBottomRatio);
        c.neck.reflectorSideScoreMaxFactor = number(v, "Neck/reflector_side_score_max_factor", c.neck.reflectorSideScoreMaxFactor);
        c.neck.reflectorBottomMinPoints = integer(v, "Neck/reflector_bottom_min_points", c.neck.reflectorBottomMinPoints);
        c.neck.reflectorFlatMaxSagPx = number(v, "Neck/reflector_flat_max_sag_px", c.neck.reflectorFlatMaxSagPx);
        c.neck.gradientThresholdCamera1 = number(v, "Neck/neck_gradient_threshold_cam1", c.neck.gradientThresholdCamera1);
        c.neck.gradientThresholdCamera2 = number(v, "Neck/neck_gradient_threshold_cam2", c.neck.gradientThresholdCamera2);
        c.neck.startSearchRatio = number(v, "Neck/neck_start_search_ratio", c.neck.startSearchRatio);
        c.neck.stopSearchRatio = number(v, "Neck/neck_stop_search_ratio", c.neck.stopSearchRatio);
        c.neck.pixelsPerMm = number(v, "Neck/neck_pixels_per_mm", c.neck.pixelsPerMm);
        c.neck.diameterAlpha = number(v, "Neck/neck_diameter_alpha", c.neck.diameterAlpha);
        c.crown.minEdgePoints = integer(v, "Crown/crown_min_edge_points", c.crown.minEdgePoints);
        c.crown.columnMaxFactor = number(v, "Crown/crown_edge_column_max_factor", c.crown.columnMaxFactor);
        c.crown.usePreviousBoundaryY = boolean(v, "Crown/crown_edge_use_previous_boundary_y", c.crown.usePreviousBoundaryY);
        c.crown.searchHalfHeightPx = integer(v, "Crown/crown_edge_search_half_height_px", c.crown.searchHalfHeightPx);
        c.crown.horizontalMarginPx = integer(v, "Crown/crown_edge_horizontal_margin_px", c.crown.horizontalMarginPx);
        c.crown.bottomMarginPx = integer(v, "Crown/crown_edge_bottom_margin_px", c.crown.bottomMarginPx);
        c.crown.fitResidualPx = number(v, "Crown/crown_edge_fit_residual_px", c.crown.fitResidualPx);
        c.body.minEdgePoints = integer(v, "Body/body_min_edge_points", c.body.minEdgePoints);
        c.body.brightnessOffsetCamera1 = number(v, "Body/body_brightness_offset_cam1", c.body.brightnessOffsetCamera1);
        c.body.brightnessOffsetCamera2 = number(v, "Body/body_brightness_offset_cam2", c.body.brightnessOffsetCamera2);
        c.body.startSearchRatio = number(v, "Body/body_start_search_ratio", c.body.startSearchRatio);
        c.body.stopSearchRatio = number(v, "Body/body_stop_search_ratio", c.body.stopSearchRatio);
        c.body.usePreviousBoundaryY = boolean(v, "Body/body_edge_use_previous_boundary_y", c.body.usePreviousBoundaryY);
        c.body.searchHalfHeightPx = integer(v, "Body/body_edge_search_half_height_px", c.body.searchHalfHeightPx);
        c.body.horizontalMarginPx = integer(v, "Body/body_edge_horizontal_margin_px", c.body.horizontalMarginPx);
        c.body.bottomMarginPx = integer(v, "Body/body_edge_bottom_margin_px", c.body.bottomMarginPx);
        c.body.minCoverageRatio = number(v, "Body/body_edge_min_coverage_ratio", c.body.minCoverageRatio);
        c.body.fitResidualPx = number(v, "Body/body_edge_fit_residual_px", c.body.fitResidualPx);
        c.endcone.diameterAlpha = number(v, "Endcone/endcone_diameter_alpha", c.endcone.diameterAlpha);
        c.endcone.boundaryOffsetPx = integer(v, "Endcone/endcone_boundary_offset_px", c.endcone.boundaryOffsetPx);

        // 与 Python ConfigManager 保持一致：配置中的相对路径以 cnf.ini 所在目录为基准，
        // 而不是以 VS Code 的 cwd 或 exe 所在目录为基准。
        const QDir configDirectory = QFileInfo(path).absoluteDir();
        if (QDir::isRelativePath(c.runtime.offlineImageDir))
            c.runtime.offlineImageDir = QDir::cleanPath(configDirectory.absoluteFilePath(c.runtime.offlineImageDir));
        if (QDir::isRelativePath(c.runtime.stateFile))
            c.runtime.stateFile = QDir::cleanPath(configDirectory.absoluteFilePath(c.runtime.stateFile));
        return c;
    }
} // namespace pva
