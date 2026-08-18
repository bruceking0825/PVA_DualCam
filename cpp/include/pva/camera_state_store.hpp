#pragma once

#include <QHash>
#include <QString>

namespace pva
{
    class CameraStateStore
    {
    public:
        explicit CameraStateStore(QString path);
        QHash<QString, double> loadExposures(const QHash<QString, double> &defaults,
                                             double minimumUs, double maximumUs) const;
        bool saveExposures(const QHash<QString, double> &exposures, QString *error = nullptr) const;

    private:
        QString path_;
    };
}
