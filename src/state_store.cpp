#include "state_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
    constexpr int schemaVersion = 4;

    QJsonArray point(cv::Point2d value) 
    { 
        return {value.x, value.y}; 
    }
    cv::Point2d point(const QJsonValue &value)
    {
        const auto values = value.toArray();
        return values.size() >= 2 ? cv::Point2d(values[0].toDouble(), values[1].toDouble()) : cv::Point2d{};
    }
    QJsonArray points(const std::array<cv::Point2d, 2> &values) 
    { 
        return {point(values[0]), point(values[1])}; 
    }
    std::array<cv::Point2d, 2> points(const QJsonValue &value)
    {
        const auto values = value.toArray();
        return {point(values.size() > 0 ? values.at(0) : QJsonValue{}), point(values.size() > 1 ? values.at(1) : QJsonValue{})};
    }
    QJsonObject reflector(const pva::ReflectorRoi &roi)
    {
        QJsonArray curve;
        for (const auto &value : roi.bottomCurve)
            curve.append(point(value));
        return {{"center", point(roi.center)}, {"left", point(roi.leftBoundary)}, {"right", point(roi.rightBoundary)}, {"bottom_curve", curve}};
    }
    pva::ReflectorRoi reflector(const QJsonValue &value)
    {
        const auto object = value.toObject();
        pva::ReflectorRoi roi;
        roi.center = point(object.value("center"));
        roi.leftBoundary = point(object.value("left"));
        roi.rightBoundary = point(object.value("right"));
        for (const auto &entry : object.value("bottom_curve").toArray())
            roi.bottomCurve.push_back(point(entry));
        return roi;
    }
}

namespace pva
{
    MeasurementState StateStore::load(QString *warning) const
    {
        QFile file(path_);
        if (!file.exists())
            return {};
        if (!file.open(QIODevice::ReadOnly))
        {
            if (warning)
                *warning = file.errorString();
            return {};
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            if (warning)
                *warning = parseError.errorString();
            return {};
        }
        const auto root = document.object();
        if (root.value("schema_version").toInt() != schemaVersion)
            return {};
        const auto object = root.value("state").toObject();
        MeasurementState state;
        const auto values = object.value("values").toObject();
        if (values.value("diameter_mm").isDouble())
            state.values.diameterMm = values.value("diameter_mm").toDouble();
        const auto light = object.value("filtered_light").toArray();
        if (light.size() >= 2)
            state.filteredLight = {light[0].toDouble(), light[1].toDouble()};
        if (object.contains("neck_centers_px"))
            state.neckCentersPx = points(object.value("neck_centers_px"));
        if (object.contains("neck_x_spans"))
        {
            const auto spans = object.value("neck_x_spans").toArray();
            const auto a = spans.size() > 0 ? spans.at(0).toArray() : QJsonArray{};
            const auto b = spans.size() > 1 ? spans.at(1).toArray() : QJsonArray{};
            state.neckXSpans = std::array<cv::Vec2i, 2>{cv::Vec2i(a.size() > 0 ? a.at(0).toInt() : 0, a.size() > 1 ? a.at(1).toInt() : 0), cv::Vec2i(b.size() > 0 ? b.at(0).toInt() : 0, b.size() > 1 ? b.at(1).toInt() : 0)};
        }
        if (object.contains("neck_reflector_rois"))
        {
            const auto rois = object.value("neck_reflector_rois").toArray();
            if (rois.size() >= 2)
                state.neckReflectorRois = std::array<ReflectorRoi, 2>{reflector(rois.at(0)), reflector(rois.at(1))};
        }
        if (object.contains("crown_boundary_points_px"))
            state.crownBoundaryPointsPx = points(object.value("crown_boundary_points_px"));
        if (object.contains("body_centers_px"))
            state.bodyCentersPx = points(object.value("body_centers_px"));
        if (object.contains("body_boundary_points_px"))
            state.bodyBoundaryPointsPx = points(object.value("body_boundary_points_px"));
        if (object.value("mm_per_pixel").isDouble())
            state.mmPerPixel = object.value("mm_per_pixel").toDouble();
        state.validNeck = object.value("valid_neck").toBool(false) && state.neckReflectorRois.has_value();
        return state;
    }

    bool StateStore::save(const MeasurementState &state, QString *error) const
    {
        const QFileInfo info(path_);
        if (!QDir().mkpath(info.absolutePath()))
        {
            if (error)
                *error = "Cannot create state directory";
            return false;
        }
        QJsonObject object;
        object["values"] = QJsonObject{{"diameter_mm", state.values.diameterMm ? QJsonValue(*state.values.diameterMm) : QJsonValue()}};
        object["filtered_light"] = QJsonArray{state.filteredLight[0], state.filteredLight[1]};
        if (state.neckCentersPx)
            object["neck_centers_px"] = points(*state.neckCentersPx);
        if (state.neckXSpans)
            object["neck_x_spans"] = QJsonArray{QJsonArray{(*state.neckXSpans)[0][0], (*state.neckXSpans)[0][1]}, QJsonArray{(*state.neckXSpans)[1][0], (*state.neckXSpans)[1][1]}};
        if (state.neckReflectorRois)
            object["neck_reflector_rois"] = QJsonArray{reflector((*state.neckReflectorRois)[0]), reflector((*state.neckReflectorRois)[1])};
        if (state.crownBoundaryPointsPx)
            object["crown_boundary_points_px"] = points(*state.crownBoundaryPointsPx);
        if (state.bodyCentersPx)
            object["body_centers_px"] = points(*state.bodyCentersPx);
        if (state.bodyBoundaryPointsPx)
            object["body_boundary_points_px"] = points(*state.bodyBoundaryPointsPx);
        object["mm_per_pixel"] = state.mmPerPixel ? QJsonValue(*state.mmPerPixel) : QJsonValue();
        object["valid_neck"] = state.validNeck;
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject{{"schema_version", schemaVersion}, {"state", object}}).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
    }
}
