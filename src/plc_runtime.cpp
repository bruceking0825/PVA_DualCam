#include "plc_runtime.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int SchemaVersion = 1;
    using pva::OverlayElement;
    using pva::OverlayType;

    bool positive(double value) { return std::isfinite(value) && value > 0.0; }
    cv::Point2d sectorPoint(double cx, double cy, double radius, double degrees)
    {
        const double angle = degrees * CV_PI / 180.0;
        return {cx - radius * std::sin(angle), cy + radius * std::cos(angle)};
    }
    void rectangle(std::vector<OverlayElement> &out, double left, double top,
                   double right, double bottom, cv::Scalar color)
    {
        out.push_back({OverlayType::Polyline,
                       {{left, top}, {right, top}, {right, bottom}, {left, bottom}},
                       color, 2, true});
    }
    std::optional<pva::PlcGrayStats> imageStats(const cv::Mat &image, cv::Rect roi)
    {
        if (image.empty())
            return {};
        roi &= cv::Rect(0, 0, image.cols, image.rows);
        if (roi.empty())
            return {};
        const cv::Mat pixels = image(roi);
        double minimum = 0.0, maximum = 0.0;
        cv::minMaxLoc(pixels, &minimum, &maximum);
        return pva::PlcGrayStats{cv::mean(pixels)[0], maximum, minimum};
    }
}

namespace pva
{
    bool setPlcRoi(PlcRois &rois, const QString &name,
                   const std::vector<double> &values, QString *error)
    {
        const auto reject = [error](const QString &message)
        {
            if (error) *error = message;
            return false;
        };
        if (!std::all_of(values.begin(), values.end(), [](double n) { return std::isfinite(n); }))
            return reject(name + " contains a non-finite coordinate");
        if (name == "dia_crd")
        {
            if (values.size() != 8 || values[4] < 0.0 || !positive(values[5]) ||
                values[5] <= values[4])
                return reject("dia_crd requires eight values and outer_radius > inner_radius >= 0");
            double stop = values[3];
            while (stop < values[2]) stop += 360.0;
            if (stop - values[2] <= 0.0 || stop - values[2] > 360.0)
                return reject("dia_crd angle sweep must be within (0, 360] degrees");
            std::array<double, 8> copy{};
            std::copy(values.begin(), values.end(), copy.begin());
            rois.diameter = copy;
        }
        else if (name == "mlt_crd")
        {
            if (values.size() != 6 || !positive(values[2]) || !positive(values[3]))
                return reject("mlt_crd requires six values and positive width/height");
            std::array<double, 6> copy{};
            std::copy(values.begin(), values.end(), copy.begin());
            rois.melt = copy;
        }
        else if (name == "dip_crd")
        {
            if (values.size() != 3 || !positive(values[2]))
                return reject("dip_crd requires three values and positive length");
            std::array<double, 3> copy{};
            std::copy(values.begin(), values.end(), copy.begin());
            rois.dip = copy;
        }
        else
            return reject("unsupported PLC ROI: " + name);
        return true;
    }

    MeasurementStage diameterStage(bool pointFitSelected, const PlcRois &rois,
                                   double bodyRadiusThreshold)
    {
        if (!pointFitSelected)
            return MeasurementStage::Neck;
        if (rois.diameter && (*rois.diameter)[4] > bodyRadiusThreshold)
            return MeasurementStage::Body;
        return MeasurementStage::Crown;
    }

    MeasurementStage stageForPlcCommand(const QString &name, MeasurementStage current,
                                        bool pointFitSelected, const PlcRois &rois,
                                        double bodyRadiusThreshold)
    {
        if (name == "cfit_ne")
            return MeasurementStage::Neck;
        if (name == "pfit_sb")
            return diameterStage(true, rois, bodyRadiusThreshold);
        if (name == "dia_crd")
        {
            if (current == MeasurementStage::Idle ||
                current == MeasurementStage::Melt ||
                current == MeasurementStage::Dip)
                return diameterStage(pointFitSelected, rois, bodyRadiusThreshold);
            if (pointFitSelected &&
                (current == MeasurementStage::Crown || current == MeasurementStage::Body))
                return diameterStage(true, rois, bodyRadiusThreshold);
        }
        if (name == "dip_msr")
            return MeasurementStage::Dip;
        if (name == "mlt_msr")
            return MeasurementStage::Melt;
        if (name == "dip_crd")
            return MeasurementStage::Dip;
        if (name == "mlt_crd")
            return MeasurementStage::Melt;
        if ((name == "dia_msr" || name == "dia_rec") &&
            (current == MeasurementStage::Idle ||
             current == MeasurementStage::Melt ||
             current == MeasurementStage::Dip))
            return diameterStage(pointFitSelected, rois, bodyRadiusThreshold);
        return current;
    }

    void appendPlcRoiOverlays(const PlcRois &rois, MeasurementStage stage,
                              double diameterRectHeight,
                              std::vector<OverlayElement> &camera1,
                              std::vector<OverlayElement> &camera2)
    {
        if ((stage == MeasurementStage::Neck || stage == MeasurementStage::Crown ||
             stage == MeasurementStage::Body || stage == MeasurementStage::Endcone) &&
            rois.diameter)
        {
            const auto &v = *rois.diameter;
            const auto add = [&](std::vector<OverlayElement> &out, double dx, double dy)
            {
                const double cx = v[0] + dx, cy = v[1] + dy;
                double stop = v[3];
                while (stop < v[2]) stop += 360.0;
                const int segments = std::max(12, int(std::ceil((stop - v[2]) / 3.0)));
                std::vector<cv::Point2d> outline;
                outline.reserve(size_t(2 * segments + 2));
                for (int i = 0; i <= segments; ++i)
                    outline.push_back(sectorPoint(cx, cy, v[5], v[2] + (stop - v[2]) * i / segments));
                for (int i = segments; i >= 0; --i)
                    outline.push_back(sectorPoint(cx, cy, v[4], v[2] + (stop - v[2]) * i / segments));
                out.push_back({OverlayType::Polyline, std::move(outline), {255, 255, 0}, 2, true});
                // 当前图像比旧版顺时针旋转 90°；直径矩形在当前图像坐标中保持轴对齐。
                rectangle(out, cx - v[5], cy - diameterRectHeight / 2.0,
                          cx - v[4], cy + diameterRectHeight / 2.0, {0, 255, 255});
            };
            add(camera1, 0.0, 0.0);
            add(camera2, v[6], v[7]);
        }
        else if (stage == MeasurementStage::Melt && rois.melt)
        {
            const auto &v = *rois.melt;
            const auto add = [&](std::vector<OverlayElement> &out, double dx, double dy)
            {
                rectangle(out, v[0] + dx - v[2] / 2.0, v[1] + dy - v[3] / 2.0,
                          v[0] + dx + v[2] / 2.0, v[1] + dy + v[3] / 2.0,
                          {255, 0, 255});
            };
            add(camera1, 0.0, 0.0);
            add(camera2, v[4], v[5]);
        }
        else if (stage == MeasurementStage::Dip && rois.dip)
        {
            const auto &v = *rois.dip;
            camera1.push_back({OverlayType::Line,
                               {{v[0], v[1] - v[2] / 2.0},
                                {v[0], v[1] + v[2] / 2.0}},
                               {255, 0, 255}, 2, false});
        }
    }

    std::optional<PlcGrayStats> meltRoiStats(const cv::Mat &image,
                                            const std::array<double, 6> &v, int camera)
    {
        const double cx = v[0] + (camera == 2 ? v[4] : 0.0);
        const double cy = v[1] + (camera == 2 ? v[5] : 0.0);
        const int left = cvRound(cx - v[2] / 2.0), right = cvRound(cx + v[2] / 2.0);
        const int top = cvRound(cy - v[3] / 2.0), bottom = cvRound(cy + v[3] / 2.0);
        return imageStats(image, cv::Rect(left, top, right - left, bottom - top));
    }

    std::optional<double> dipLineMean(const cv::Mat &image,
                                      const std::array<double, 3> &v)
    {
        if (image.empty())
            return {};
        const int x = cvRound(v[0]);
        const int top = std::max(0, cvRound(v[1] - v[2] / 2.0));
        const int bottom = std::min(image.rows, cvRound(v[1] + v[2] / 2.0) + 1);
        if (x < 0 || x >= image.cols || top >= bottom)
            return {};
        return cv::mean(image(cv::Rect(x, top, 1, bottom - top)))[0];
    }

    bool PlcRuntimeStore::load(PlcRuntimeState *state, QString *error) const
    {
        QFile file(path_);
        if (!file.exists()) return true;
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error) *error = file.errorString();
            return false;
        }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
            doc.object().value("schema_version").toInt() != SchemaVersion)
        {
            if (error) *error = "Invalid PLC runtime state file";
            return false;
        }
        const auto root = doc.object();
        const int stage = root.value("stage").toInt(-1);
        if (stage < int(MeasurementStage::Idle) || stage > int(MeasurementStage::Endcone))
        {
            if (error) *error = "Invalid saved PLC stage";
            return false;
        }
        PlcRuntimeState next;
        next.stage = MeasurementStage(stage);
        next.pointFitSelected = root.value("point_fit_selected").toBool(false);
        next.acquisitionEnabled = root.value("acquisition_enabled").toBool(true);
        next.relativeThreshold = root.value("relative_threshold").toBool(false);
        next.refreshRate = root.value("refresh_rate").toDouble(1.0);
        if (!std::isfinite(next.refreshRate) || next.refreshRate <= 0.0)
        {
            if (error) *error = "Invalid saved PLC refresh rate";
            return false;
        }
        const auto parameters = root.value("parameters").toObject();
        for (auto it = parameters.begin(); it != parameters.end(); ++it)
        {
            std::vector<double> values;
            for (const auto &item : it.value().toArray())
            {
                if (!item.isDouble() || !std::isfinite(item.toDouble()))
                {
                    if (error) *error = "Invalid saved PLC parameter: " + it.key();
                    return false;
                }
                values.push_back(item.toDouble());
            }
            next.parameters.insert(it.key(), std::move(values));
        }
        *state = std::move(next);
        return true;
    }

    bool PlcRuntimeStore::save(const PlcRuntimeState &state, QString *error) const
    {
        if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        {
            if (error) *error = "Cannot create PLC state directory";
            return false;
        }
        QJsonObject parameters;
        for (auto it = state.parameters.cbegin(); it != state.parameters.cend(); ++it)
        {
            QJsonArray values;
            for (double value : it.value()) values.append(value);
            parameters.insert(it.key(), values);
        }
        const QJsonObject root{
            {"schema_version", SchemaVersion},
            {"stage", int(state.stage)},
            {"point_fit_selected", state.pointFitSelected},
            {"acquisition_enabled", state.acquisitionEnabled},
            {"relative_threshold", state.relativeThreshold},
            {"refresh_rate", state.refreshRate},
            {"parameters", parameters}
        };
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
            !file.commit())
        {
            if (error) *error = file.errorString();
            return false;
        }
        return true;
    }
}
