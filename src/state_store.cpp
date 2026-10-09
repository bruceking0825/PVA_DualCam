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
    // 旧版本保存的是旋转后图像坐标，不能当作原始相机坐标恢复。
    constexpr int schemaVersion = 5;

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
        if (object.contains("crown_boundary_points_px"))
            state.crownBoundaryPointsPx = points(object.value("crown_boundary_points_px"));
        if (object.contains("body_boundary_points_px"))
            state.bodyBoundaryPointsPx = points(object.value("body_boundary_points_px"));
        state.validNeck = object.value("valid_neck").toBool(false) && state.neckCentersPx.has_value();
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
        if (state.crownBoundaryPointsPx)
            object["crown_boundary_points_px"] = points(*state.crownBoundaryPointsPx);
        if (state.bodyBoundaryPointsPx)
            object["body_boundary_points_px"] = points(*state.bodyBoundaryPointsPx);
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
