#include "pva/camera_manager.hpp"
#include "pva/dalsa_camera.hpp"

namespace pva
{
    CameraManager::CameraManager(QObject *parent) : QObject(parent) {}

    CameraManager::~CameraManager()
    {
        closeAll();
        if (initialized_)
            DalsaCamera::shutdown();
    }

    bool CameraManager::initialize(QString *error)
    {
        if (initialized_)
            return true;
        initialized_ = DalsaCamera::initialize(error);
        return initialized_;
    }

    void CameraManager::reset(const QStringList &userIds)
    {
        closeAll();
        cameras_.clear();
        for (const QString &id : userIds)
        {
            if (id != "1" && id != "2")
                continue;
            auto camera = std::make_unique<DalsaCamera>(id);
            connect(camera.get(), &DalsaCamera::frameReady, this, &CameraManager::frameReady,
                    Qt::QueuedConnection);
            connect(camera.get(), &DalsaCamera::captureFailed, this, &CameraManager::captureFailed,
                    Qt::QueuedConnection);
            cameras_.insert_or_assign(id, std::move(camera));
        }
    }

    DalsaCamera *CameraManager::getByRole(const QString &role) const
    {
        const auto iterator = cameras_.find(role);
        return iterator == cameras_.end() ? nullptr : iterator->second.get();
    }

    QList<DalsaCamera *> CameraManager::getAll() const
    {
        QList<DalsaCamera *> result;
        for (const auto &[role, camera] : cameras_)
        {
            Q_UNUSED(role);
            result.append(camera.get());
        }
        return result;
    }

    void CameraManager::closeAll()
    {
        for (const auto &[role, camera] : cameras_)
        {
            Q_UNUSED(role);
            camera->close();
        }
    }
}
