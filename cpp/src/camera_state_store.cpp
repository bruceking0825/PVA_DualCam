#include "pva/camera_state_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace pva
{
    CameraStateStore::CameraStateStore(QString path) : path_(std::move(path)) {}

    QHash<QString, double> CameraStateStore::loadExposures(
        const QHash<QString, double> &defaults, double minimumUs, double maximumUs) const
    {
        const double lower = std::min(minimumUs, maximumUs);
        const double upper = std::max(minimumUs, maximumUs);
        QHash<QString, double> result;
        for (auto iterator = defaults.cbegin(); iterator != defaults.cend(); ++iterator)
            result.insert(iterator.key(), std::clamp(iterator.value(), lower, upper));

        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly))
            return result;
        const QJsonObject values = QJsonDocument::fromJson(file.readAll()).object()
                                       .value("exposure_us").toObject();
        for (auto iterator = result.begin(); iterator != result.end(); ++iterator)
            if (values.contains(iterator.key()))
                iterator.value() = std::clamp(values.value(iterator.key()).toDouble(iterator.value()), lower, upper);
        return result;
    }

    bool CameraStateStore::saveExposures(const QHash<QString, double> &exposures, QString *error) const
    {
        QJsonObject values;
        for (auto iterator = exposures.cbegin(); iterator != exposures.cend(); ++iterator)
            if (std::isfinite(iterator.value()))
                values.insert(iterator.key(), iterator.value());
        QDir().mkpath(QFileInfo(path_).absolutePath());
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly))
        {
            if (error) *error = file.errorString();
            return false;
        }
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"exposure_us", values}})
                       .toJson(QJsonDocument::Indented));
        if (file.commit())
            return true;
        if (error) *error = file.errorString();
        return false;
    }
}
