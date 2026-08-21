#pragma once

#include "pva/config.hpp"
#include <QObject>

namespace pva
{
    // C++ counterpart of modules/app_config.py. The application has one
    // authoritative configuration object and pages observe its notifications.
    class ConfigManager final : public QObject
    {
        Q_OBJECT
    public:
        static ConfigManager &instance();

        void load(const QString &path = {}, bool emitChanges = true);
        bool setEntry(const QString &group, const QString &key, const QString &value, QString *error = nullptr);
        const MeasurementConfig &config() const { return config_; }
        const QString &path() const { return path_; }

    signals:
        void entryChanged(const QString &group, const QString &key);
        void batchChanged();

    private:
        ConfigManager() = default;
        QString path_;
        MeasurementConfig config_;
    };
}
