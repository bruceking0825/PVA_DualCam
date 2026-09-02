#include "config.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
    using Values = QHash<QString, QString>;

    struct ConversionOptions
    {
        double minimum{-std::numeric_limits<double>::infinity()};
        double maximum{std::numeric_limits<double>::infinity()};
        bool resolveRelativePath{false};
    };

    struct ConfigEntry
    {
        const char *group;
        const char *key;
        std::function<bool(pva::MeasurementConfig &, const QString &, const QString &, bool &, QString &)> apply;
    };

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
            if (split >= 0)
                values.insert(group + '/' + raw.left(split).trimmed(), clean(raw.mid(split + 1)));
        }
        return values;
    }

    template <typename T>
    bool convertValue(const QString &raw, const QString &configPath,
                      const ConversionOptions &options, T &result, QString &error)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            const QString normalized = raw.toLower();
            if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
                result = true;
            else if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
                result = false;
            else
            {
                error = "invalid boolean";
                return false;
            }
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            bool ok = false;
            const double parsed = raw.toDouble(&ok);
            if (!ok || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
            {
                error = "invalid integer";
                return false;
            }
            const int low = options.minimum == -std::numeric_limits<double>::infinity()
                                ? std::numeric_limits<int>::min()
                                : static_cast<int>(options.minimum);
            const int high = options.maximum == std::numeric_limits<double>::infinity()
                                 ? std::numeric_limits<int>::max()
                                 : static_cast<int>(options.maximum);
            result = std::clamp(static_cast<int>(parsed), low, high);
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            bool ok = false;
            const double parsed = raw.toDouble(&ok);
            if (!ok)
            {
                error = "invalid number";
                return false;
            }
            result = std::clamp(parsed, options.minimum, options.maximum);
        }
        else if constexpr (std::is_same_v<T, QString>)
        {
            result = raw;
            if (options.resolveRelativePath && QDir::isRelativePath(result))
                result = QDir::cleanPath(QFileInfo(configPath).absoluteDir().absoluteFilePath(result));
        }
        else if constexpr (std::is_same_v<T, cv::Rect>)
        {
            const QStringList parts = raw.split(',', Qt::KeepEmptyParts);
            if (parts.size() != 4)
            {
                error = "ROI requires x,y,width,height";
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
                error = "invalid ROI";
                return false;
            }
            if (number[2] < 1 || number[3] < 1)
            {
                error = "ROI width and height must be at least 1";
                return false;
            }
            result = cv::Rect(number[0], number[1], number[2], number[3]);
        }
        return true;
    }

    template <typename Section, typename T>
    ConfigEntry setting(const char *group, const char *key,
                        Section pva::MeasurementConfig::*section, T Section::*member,
                        ConversionOptions options = {})
    {
        return {group, key,
                [section, member, options](pva::MeasurementConfig &config, const QString &configPath,
                                           const QString &raw, bool &changed, QString &error)
                {
                    T updated{};
                    if (!convertValue(raw, configPath, options, updated, error))
                        return false;
                    T &target = (config.*section).*member;
                    changed = target != updated;
                    if (changed)
                        target = std::move(updated);
                    return true;
                }};
    }

    ConversionOptions minimum(double value)
    {
        ConversionOptions options;
        options.minimum = value;
        return options;
    }

    ConversionOptions range(double low, double high) { return {low, high, false}; }

    ConversionOptions path()
    {
        ConversionOptions options;
        options.resolveRelativePath = true;
        return options;
    }

    const std::vector<ConfigEntry> &configEntries()
    {
        // The only INI-to-C++ mapping table. Initial loading and live edits
        // share it, so each new typed parameter is registered once.
        static const std::vector<ConfigEntry> entries{
            setting("Runtime", "disable_camera_for_plc_test", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::disableCameraForPlcTest),
            setting("Runtime", "connect_plc_in_offline", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::connectPlcInOffline),
            setting("Runtime", "idle_sample_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::idleSampleIntervalMs, minimum(50)),
            setting("Runtime", "neck_sample_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::neckSampleIntervalMs, minimum(50)),
            setting("Runtime", "crown_sample_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::crownSampleIntervalMs, minimum(50)),
            setting("Runtime", "body_sample_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::bodySampleIntervalMs, minimum(50)),
            setting("Runtime", "endcone_sample_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::endconeSampleIntervalMs, minimum(50)),
            setting("Runtime", "offline_image_dir", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::offlineImageDir, path()),
            setting("Runtime", "loop_interval_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::loopIntervalMs, minimum(50)),
            setting("Runtime", "state_file", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::stateFile, path()),
            setting("Runtime", "stereo_pair_max_delta_ms", &pva::MeasurementConfig::runtime, &pva::RuntimeSettings::stereoPairMaxDeltaMs, minimum(1)),

            setting("Camera", "initial_exposure_camera1", &pva::MeasurementConfig::camera, &pva::CameraSettings::initialExposureCamera1),
            setting("Camera", "gain_camera1", &pva::MeasurementConfig::camera, &pva::CameraSettings::gainCamera1),
            setting("Camera", "initial_exposure_camera2", &pva::MeasurementConfig::camera, &pva::CameraSettings::initialExposureCamera2),
            setting("Camera", "gain_camera2", &pva::MeasurementConfig::camera, &pva::CameraSettings::gainCamera2),
            setting("Camera", "offline_crop_roi", &pva::MeasurementConfig::camera, &pva::CameraSettings::offlineCropRoi),
            setting("Camera", "online_crop_roi", &pva::MeasurementConfig::camera, &pva::CameraSettings::onlineCropRoi),
            setting("Camera", "auto_exposure_enabled", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureEnabled),
            setting("Camera", "auto_exposure_target", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureTarget, range(1, 254)),
            setting("Camera", "auto_exposure_min_us", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureMinUs, minimum(1)),
            setting("Camera", "auto_exposure_max_us", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureMaxUs, minimum(1)),
            setting("Camera", "auto_exposure_gain", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureGain, minimum(0)),
            setting("Camera", "auto_exposure_deadband", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureDeadband, minimum(0)),
            setting("Camera", "auto_exposure_interval_ms", &pva::MeasurementConfig::camera, &pva::CameraSettings::autoExposureIntervalMs, minimum(50)),

            setting("Measurement", "brightness_min", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::brightnessMin),
            setting("Measurement", "brightness_max", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::brightnessMax),
            setting("Measurement", "diameter_min_mm", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::diameterMinMm),
            setting("Measurement", "diameter_max_mm", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::diameterMaxMm),
            setting("Measurement", "light_alpha", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::lightAlpha),
            setting("Measurement", "mm_per_pixel_alpha", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::mmPerPixelAlpha),
            setting("Measurement", "auto_exposure_roi_camera1", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::autoExposureRoiCamera1),
            setting("Measurement", "auto_exposure_roi_camera2", &pva::MeasurementConfig::measurement, &pva::MeasurementSettings::autoExposureRoiCamera2),

            setting("Neck", "min_contour_area_px", &pva::MeasurementConfig::neck, &pva::NeckSettings::minContourAreaPx),
            setting("Neck", "neck_min_edge_points", &pva::MeasurementConfig::neck, &pva::NeckSettings::minEdgePoints),
            setting("Neck", "reflector_threshold_cam1", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorThresholdCamera1),
            setting("Neck", "reflector_threshold_cam2", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorThresholdCamera2),
            setting("Neck", "reflector_bottom_search_top_ratio", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorBottomSearchTopRatio),
            setting("Neck", "reflector_bottom_search_bottom_ratio", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorBottomSearchBottomRatio),
            setting("Neck", "reflector_side_score_max_factor", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorSideScoreMaxFactor),
            setting("Neck", "reflector_bottom_min_points", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorBottomMinPoints),
            setting("Neck", "reflector_flat_max_sag_px", &pva::MeasurementConfig::neck, &pva::NeckSettings::reflectorFlatMaxSagPx),
            setting("Neck", "neck_gradient_threshold_cam1", &pva::MeasurementConfig::neck, &pva::NeckSettings::gradientThresholdCamera1),
            setting("Neck", "neck_gradient_threshold_cam2", &pva::MeasurementConfig::neck, &pva::NeckSettings::gradientThresholdCamera2),
            setting("Neck", "neck_start_search_ratio", &pva::MeasurementConfig::neck, &pva::NeckSettings::startSearchRatio),
            setting("Neck", "neck_stop_search_ratio", &pva::MeasurementConfig::neck, &pva::NeckSettings::stopSearchRatio),
            setting("Neck", "neck_pixels_per_mm", &pva::MeasurementConfig::neck, &pva::NeckSettings::pixelsPerMm),
            setting("Neck", "neck_diameter_alpha", &pva::MeasurementConfig::neck, &pva::NeckSettings::diameterAlpha),

            setting("Crown", "crown_min_edge_points", &pva::MeasurementConfig::crown, &pva::CrownSettings::minEdgePoints),
            setting("Crown", "crown_edge_column_max_factor", &pva::MeasurementConfig::crown, &pva::CrownSettings::columnMaxFactor),
            setting("Crown", "crown_edge_use_previous_boundary_y", &pva::MeasurementConfig::crown, &pva::CrownSettings::usePreviousBoundaryY),
            setting("Crown", "crown_edge_search_half_height_px", &pva::MeasurementConfig::crown, &pva::CrownSettings::searchHalfHeightPx),
            setting("Crown", "crown_edge_horizontal_margin_px", &pva::MeasurementConfig::crown, &pva::CrownSettings::horizontalMarginPx),
            setting("Crown", "crown_edge_bottom_margin_px", &pva::MeasurementConfig::crown, &pva::CrownSettings::bottomMarginPx),
            setting("Crown", "crown_edge_fit_residual_px", &pva::MeasurementConfig::crown, &pva::CrownSettings::fitResidualPx),

            setting("Body", "body_min_edge_points", &pva::MeasurementConfig::body, &pva::BodySettings::minEdgePoints),
            setting("Body", "body_brightness_offset_cam1", &pva::MeasurementConfig::body, &pva::BodySettings::brightnessOffsetCamera1),
            setting("Body", "body_brightness_offset_cam2", &pva::MeasurementConfig::body, &pva::BodySettings::brightnessOffsetCamera2),
            setting("Body", "body_start_search_ratio", &pva::MeasurementConfig::body, &pva::BodySettings::startSearchRatio),
            setting("Body", "body_stop_search_ratio", &pva::MeasurementConfig::body, &pva::BodySettings::stopSearchRatio),
            setting("Body", "body_edge_use_previous_boundary_y", &pva::MeasurementConfig::body, &pva::BodySettings::usePreviousBoundaryY),
            setting("Body", "body_edge_search_half_height_px", &pva::MeasurementConfig::body, &pva::BodySettings::searchHalfHeightPx),
            setting("Body", "body_edge_horizontal_margin_px", &pva::MeasurementConfig::body, &pva::BodySettings::horizontalMarginPx),
            setting("Body", "body_edge_bottom_margin_px", &pva::MeasurementConfig::body, &pva::BodySettings::bottomMarginPx),
            setting("Body", "body_edge_min_coverage_ratio", &pva::MeasurementConfig::body, &pva::BodySettings::minCoverageRatio),
            setting("Body", "body_edge_fit_residual_px", &pva::MeasurementConfig::body, &pva::BodySettings::fitResidualPx),

            setting("Endcone", "endcone_diameter_alpha", &pva::MeasurementConfig::endcone, &pva::EndconeSettings::diameterAlpha),
            setting("Endcone", "endcone_boundary_offset_px", &pva::MeasurementConfig::endcone, &pva::EndconeSettings::boundaryOffsetPx),
        };
        return entries;
    }
}

namespace pva
{
    bool applyConfigEntry(MeasurementConfig &config, const QString &configPath,
                          const QString &group, const QString &key, const QString &value,
                          ConfigEntryUpdate *update, QString *error)
    {
        if (update)
            *update = {};
        const auto &entries = configEntries();
        const auto entry = std::find_if(entries.cbegin(), entries.cend(),
                                        [&group, &key](const ConfigEntry &candidate)
                                        { return group == candidate.group && key == candidate.key; });
        if (entry == entries.cend())
            return true;

        bool changed = false;
        QString detail;
        if (!entry->apply(config, configPath, clean(value), changed, detail))
        {
            if (error)
                *error = group + '.' + key + ": " + detail;
            return false;
        }
        if (update)
            *update = {true, changed};
        return true;
    }

    MeasurementConfig MeasurementConfig::loadIni(const QString &path)
    {
        MeasurementConfig config;
        const Values values = readIni(path);
        for (auto value = values.cbegin(); value != values.cend(); ++value)
        {
            const qsizetype split = value.key().indexOf('/');
            if (split < 0)
                continue;
            // As before, malformed known values leave their defaults unchanged.
            applyConfigEntry(config, path, value.key().left(split),
                             value.key().mid(split + 1), value.value());
        }
        return config;
    }
} // namespace pva
