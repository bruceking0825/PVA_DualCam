#include "config_manager.hpp"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace
{
    QString clean(QString value)
    {
        value = value.trimmed();
        if (value.startsWith('[') && value.endsWith(']'))
            value = value.mid(1, value.size() - 2).trimmed();
        return value;
    }
}

namespace pva
{
    ConfigManager &ConfigManager::instance()
    {
        static ConfigManager manager;
        return manager;
    }

    void ConfigManager::load(const QString &path, bool emitChanges)
    {
        if (!path.isEmpty())
            path_ = path;
        if (path_.isEmpty())
            throw std::runtime_error("Configuration path is empty");
        config_ = MeasurementConfig::loadIni(path_);
        if (emitChanges)
            emit batchChanged();
    }

    bool ConfigManager::setEntry(const QString &group, const QString &key, const QString &value, QString *error)
    {
        const QString raw = clean(value);
        const auto publish = [this, &group, &key](auto &target, const auto &updated)
        {
            if (target == updated)
                return true;
            target = updated;
            emit entryChanged(group, key);
            return true;
        };
        const auto applyDouble = [&](double &target,
                                     double minimum = -std::numeric_limits<double>::infinity(),
                                     double maximum = std::numeric_limits<double>::infinity())
        {
            bool ok = false;
            double updated = raw.toDouble(&ok);
            if (!ok)
            {
                if (error)
                    *error = group + '.' + key + ": invalid number";
                return false;
            }
            return publish(target, std::clamp(updated, minimum, maximum));
        };
        const auto applyInt = [&](int &target, int minimum = std::numeric_limits<int>::min())
        {
            bool ok = false;
            const double parsed = raw.toDouble(&ok);
            if (!ok || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
            {
                if (error)
                    *error = group + '.' + key + ": invalid integer";
                return false;
            }
            return publish(target, std::max(static_cast<int>(parsed), minimum));
        };
        const auto applyBool = [&](bool &target)
        {
            const QString normalized = raw.toLower();
            if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
                return publish(target, true);
            if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
                return publish(target, false);
            if (error)
                *error = group + '.' + key + ": invalid boolean";
            return false;
        };
        const auto applyRoi = [&](cv::Rect &target)
        {
            const QStringList parts = raw.split(',', Qt::KeepEmptyParts);
            if (parts.size() != 4)
            {
                if (error)
                    *error = group + '.' + key + ": ROI requires x,y,width,height";
                return false;
            }
            bool ok[4]{};
            int number[4]{};
            for (int index = 0; index < 4; ++index)
            {
                const double parsed = parts[index].trimmed().toDouble(&ok[index]);
                if (ok[index] && parsed >= std::numeric_limits<int>::min() &&
                    parsed <= std::numeric_limits<int>::max())
                    number[index] = static_cast<int>(parsed);
                else
                    ok[index] = false;
            }
            if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
            {
                if (error)
                    *error = group + '.' + key + ": invalid ROI";
                return false;
            }
            if (number[2] < 1 || number[3] < 1)
            {
                if (error)
                    *error = group + '.' + key + ": ROI width and height must be at least 1";
                return false;
            }
            return publish(target, cv::Rect(number[0], number[1], number[2], number[3]));
        };

        if (group == "Runtime")
        {
            if (key == "disable_camera_for_plc_test")
                return applyBool(config_.runtime.disableCameraForPlcTest);
            if (key == "connect_plc_in_offline")
                return applyBool(config_.runtime.connectPlcInOffline);
            if (key == "idle_sample_interval_ms")
                return applyInt(config_.runtime.idleSampleIntervalMs, 50);
            if (key == "neck_sample_interval_ms")
                return applyInt(config_.runtime.neckSampleIntervalMs, 50);
            if (key == "crown_sample_interval_ms")
                return applyInt(config_.runtime.crownSampleIntervalMs, 50);
            if (key == "body_sample_interval_ms")
                return applyInt(config_.runtime.bodySampleIntervalMs, 50);
            if (key == "endcone_sample_interval_ms")
                return applyInt(config_.runtime.endconeSampleIntervalMs, 50);
            if (key == "loop_interval_ms")
                return applyInt(config_.runtime.loopIntervalMs, 50);
            if (key == "stereo_pair_max_delta_ms")
                return applyInt(config_.runtime.stereoPairMaxDeltaMs, 1);
            if (key == "offline_image_dir")
            {
                QString updated = raw;
                if (QDir::isRelativePath(updated))
                    updated = QDir::cleanPath(QFileInfo(path_).absoluteDir().absoluteFilePath(updated));
                return publish(config_.runtime.offlineImageDir, updated);
            }
            if (key == "state_file")
            {
                QString updated = raw;
                if (QDir::isRelativePath(updated))
                    updated = QDir::cleanPath(QFileInfo(path_).absoluteDir().absoluteFilePath(updated));
                return publish(config_.runtime.stateFile, updated);
            }
        }
        else if (group == "Camera")
        {
            if (key == "initial_exposure_camera1")
                return applyDouble(config_.camera.initialExposureCamera1);
            if (key == "gain_camera1")
                return applyDouble(config_.camera.gainCamera1);
            if (key == "initial_exposure_camera2")
                return applyDouble(config_.camera.initialExposureCamera2);
            if (key == "gain_camera2")
                return applyDouble(config_.camera.gainCamera2);
            if (key == "offline_crop_roi")
                return applyRoi(config_.camera.offlineCropRoi);
            if (key == "online_crop_roi")
                return applyRoi(config_.camera.onlineCropRoi);
            if (key == "auto_exposure_enabled")
                return applyBool(config_.camera.autoExposureEnabled);
            if (key == "auto_exposure_target")
                return applyDouble(config_.camera.autoExposureTarget, 1.0, 254.0);
            if (key == "auto_exposure_min_us")
                return applyDouble(config_.camera.autoExposureMinUs, 1.0);
            if (key == "auto_exposure_max_us")
                return applyDouble(config_.camera.autoExposureMaxUs, 1.0);
            if (key == "auto_exposure_gain")
                return applyDouble(config_.camera.autoExposureGain, 0.0);
            if (key == "auto_exposure_deadband")
                return applyDouble(config_.camera.autoExposureDeadband, 0.0);
            if (key == "auto_exposure_interval_ms")
                return applyInt(config_.camera.autoExposureIntervalMs, 50);
        }
        else if (group == "Measurement")
        {
            if (key == "brightness_min")
                return applyDouble(config_.measurement.brightnessMin);
            if (key == "brightness_max")
                return applyDouble(config_.measurement.brightnessMax);
            if (key == "diameter_min_mm")
                return applyDouble(config_.measurement.diameterMinMm);
            if (key == "diameter_max_mm")
                return applyDouble(config_.measurement.diameterMaxMm);
            if (key == "light_alpha")
                return applyDouble(config_.measurement.lightAlpha);
            if (key == "mm_per_pixel_alpha")
                return applyDouble(config_.measurement.mmPerPixelAlpha);
            if (key == "auto_exposure_roi_camera1")
                return applyRoi(config_.measurement.autoExposureRoiCamera1);
            if (key == "auto_exposure_roi_camera2")
                return applyRoi(config_.measurement.autoExposureRoiCamera2);
        }
        else if (group == "Neck")
        {
            if (key == "min_contour_area_px")
                return applyDouble(config_.neck.minContourAreaPx);
            if (key == "neck_min_edge_points")
                return applyInt(config_.neck.minEdgePoints);
            if (key == "reflector_threshold_cam1")
                return applyDouble(config_.neck.reflectorThresholdCamera1);
            if (key == "reflector_threshold_cam2")
                return applyDouble(config_.neck.reflectorThresholdCamera2);
            if (key == "reflector_bottom_search_top_ratio")
                return applyDouble(config_.neck.reflectorBottomSearchTopRatio);
            if (key == "reflector_bottom_search_bottom_ratio")
                return applyDouble(config_.neck.reflectorBottomSearchBottomRatio);
            if (key == "reflector_side_score_max_factor")
                return applyDouble(config_.neck.reflectorSideScoreMaxFactor);
            if (key == "reflector_bottom_min_points")
                return applyInt(config_.neck.reflectorBottomMinPoints);
            if (key == "reflector_flat_max_sag_px")
                return applyDouble(config_.neck.reflectorFlatMaxSagPx);
            if (key == "neck_gradient_threshold_cam1")
                return applyDouble(config_.neck.gradientThresholdCamera1);
            if (key == "neck_gradient_threshold_cam2")
                return applyDouble(config_.neck.gradientThresholdCamera2);
            if (key == "neck_start_search_ratio")
                return applyDouble(config_.neck.startSearchRatio);
            if (key == "neck_stop_search_ratio")
                return applyDouble(config_.neck.stopSearchRatio);
            if (key == "neck_pixels_per_mm")
                return applyDouble(config_.neck.pixelsPerMm);
            if (key == "neck_diameter_alpha")
                return applyDouble(config_.neck.diameterAlpha);
        }
        else if (group == "Crown")
        {
            if (key == "crown_min_edge_points")
                return applyInt(config_.crown.minEdgePoints);
            if (key == "crown_edge_column_max_factor")
                return applyDouble(config_.crown.columnMaxFactor);
            if (key == "crown_edge_use_previous_boundary_y")
                return applyBool(config_.crown.usePreviousBoundaryY);
            if (key == "crown_edge_search_half_height_px")
                return applyInt(config_.crown.searchHalfHeightPx);
            if (key == "crown_edge_horizontal_margin_px")
                return applyInt(config_.crown.horizontalMarginPx);
            if (key == "crown_edge_bottom_margin_px")
                return applyInt(config_.crown.bottomMarginPx);
            if (key == "crown_edge_fit_residual_px")
                return applyDouble(config_.crown.fitResidualPx);
        }
        else if (group == "Body")
        {
            if (key == "body_min_edge_points")
                return applyInt(config_.body.minEdgePoints);
            if (key == "body_brightness_offset_cam1")
                return applyDouble(config_.body.brightnessOffsetCamera1);
            if (key == "body_brightness_offset_cam2")
                return applyDouble(config_.body.brightnessOffsetCamera2);
            if (key == "body_start_search_ratio")
                return applyDouble(config_.body.startSearchRatio);
            if (key == "body_stop_search_ratio")
                return applyDouble(config_.body.stopSearchRatio);
            if (key == "body_edge_use_previous_boundary_y")
                return applyBool(config_.body.usePreviousBoundaryY);
            if (key == "body_edge_search_half_height_px")
                return applyInt(config_.body.searchHalfHeightPx);
            if (key == "body_edge_horizontal_margin_px")
                return applyInt(config_.body.horizontalMarginPx);
            if (key == "body_edge_bottom_margin_px")
                return applyInt(config_.body.bottomMarginPx);
            if (key == "body_edge_min_coverage_ratio")
                return applyDouble(config_.body.minCoverageRatio);
            if (key == "body_edge_fit_residual_px")
                return applyDouble(config_.body.fitResidualPx);
        }
        else if (group == "Endcone")
        {
            if (key == "endcone_diameter_alpha")
                return applyDouble(config_.endcone.diameterAlpha);
            if (key == "endcone_boundary_offset_px")
                return applyInt(config_.endcone.boundaryOffsetPx);
        }

        // 与 Python ConfigManager 一致：自定义/未知项仍允许保存在 cnf 中，
        // 只是当前运行模型没有对应字段可实时应用。
        return true;
    }
}
