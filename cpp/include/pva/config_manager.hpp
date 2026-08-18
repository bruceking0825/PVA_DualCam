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
        const MeasurementConfig &config() const { return config_; }
        const QString &path() const { return path_; }

    signals:
        void batchChanged();

    private:
        ConfigManager() = default;
        QString path_;
        MeasurementConfig config_;
    };
}
