#include "config_manager.hpp"

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

    bool ConfigManager::setEntry(const QString &group, const QString &key,
                                 const QString &value, QString *error)
    {
        ConfigEntryUpdate update;
        if (!applyConfigEntry(config_, path_, group, key, value, &update, error))
            return false;
        if (update.changed)
            emit entryChanged(group, key);
        // Preserve unknown INI entries even when the runtime does not use them.
        return true;
    }
} // namespace pva
