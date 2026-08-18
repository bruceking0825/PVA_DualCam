#include "pva/config_manager.hpp"
#include <stdexcept>

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
}
