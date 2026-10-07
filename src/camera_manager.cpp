#include "camera_manager.hpp"
#include "dalsa_camera.hpp"

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
        for (const QString &rawId : userIds)
        {
            const QString id = rawId.trimmed();
            if (id.isEmpty() || cameras_.contains(id))
                continue;
            auto camera = std::make_unique<DalsaCamera>(id);
            connect(camera.get(), &DalsaCamera::frameReady, this,
                [this](const QString &id, const cv::Mat &frame, qint64 timestamp) {
                    const auto epoch = deliveryEpoch_.load();
                    QMetaObject::invokeMethod(this, [this, epoch, id, frame, timestamp] {
                        if (epoch == deliveryEpoch_.load()) emit frameReady(id, frame, timestamp);
                    }, Qt::QueuedConnection);
                }, Qt::DirectConnection);
            connect(camera.get(), &DalsaCamera::captureFailed, this,
                [this](const QString &id, const QString &error) {
                    const auto epoch = deliveryEpoch_.load();
                    QMetaObject::invokeMethod(this, [this, epoch, id, error] {
                        if (epoch == deliveryEpoch_.load()) emit captureFailed(id, error);
                    }, Qt::QueuedConnection);
                }, Qt::DirectConnection);
            cameras_.insert_or_assign(id, std::move(camera));
        }
    }

    DalsaCamera *CameraManager::get(const QString &userId) const
    {
        const auto iterator = cameras_.find(userId);
        return iterator == cameras_.end() ? nullptr : iterator->second.get();
    }

    QList<DalsaCamera *> CameraManager::getAll() const
    {
        QList<DalsaCamera *> result;
        for (const auto &[userId, camera] : cameras_)
        {
            Q_UNUSED(userId);
            result.append(camera.get());
        }
        return result;
    }

    void CameraManager::closeAll()
    {
        for (const auto &[userId, camera] : cameras_)
        {
            Q_UNUSED(userId);
            camera->close();
        }
        // SDK 已停止回调；使关闭前尚未投递的帧和错误事件失效。
        ++deliveryEpoch_;
    }
}
