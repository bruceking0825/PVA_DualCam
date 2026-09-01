#include "dalsa_camera.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <algorithm>
#include <array>
#include <string>

#ifdef PVA_HAS_SAPERA
#include <SapClassBasic.h>
#endif

namespace
{
    std::atomic_int saperaUsers{0};
#ifdef PVA_HAS_SAPERA
    SapManager::StatusMode previousStatusMode{SapManager::StatusNotify};
#endif

    struct CameraDescriptor
    {
        std::string server;
        int resource{};
        QString model;
        QString deviceUserId;
    };

    QHash<QString, CameraDescriptor> descriptors;

    qint64 nowNs()
    {
        static QElapsedTimer timer = []
        { QElapsedTimer value; value.start(); return value; }();
        return timer.nsecsElapsed();
    }

    QString featureError(const char *operation, const char *feature)
    {
        return QString("Sapera %1 failed: %2").arg(operation, feature);
    }

}

namespace pva
{
    struct DalsaCamera::Impl
    {
#ifdef PVA_HAS_SAPERA
        std::unique_ptr<SapAcqDevice> device;
        std::unique_ptr<SapBufferWithTrash> buffers;
        std::unique_ptr<SapAcqDeviceToBuf> transfer;
#endif
    };

    DalsaCamera::DalsaCamera(QString role, QObject *parent)
        : QObject(parent), impl_(std::make_unique<Impl>()), role_(std::move(role)) {}

    DalsaCamera::~DalsaCamera() { close(); }

    bool DalsaCamera::initialize(QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (saperaUsers.fetch_add(1) == 0)
        {
            if (!SapManager::Open())
            {
                saperaUsers.store(0);
                if (error)
                    *error = "SapManager::Open failed";
                return false;
            }
            // SDK 错误由程序状态栏和日志统一显示，禁止 Sapera 弹出阻塞式错误窗口。
            previousStatusMode = SapManager::GetDisplayStatusMode();
            SapManager::SetDisplayStatusMode(SapManager::StatusLog);
        }
        return true;
#else
        if (error)
            *error = "Sapera LT SDK was not found when C++ was built";
        return false;
#endif
    }

    void DalsaCamera::shutdown()
    {
        descriptors.clear();
#ifdef PVA_HAS_SAPERA
        if (saperaUsers.load() > 0 && saperaUsers.fetch_sub(1) == 1)
        {
            SapManager::SetDisplayStatusMode(previousStatusMode);
            SapManager::Close();
        }
#endif
    }

    QStringList DalsaCamera::enumerate(QString *error)
    {
        descriptors.clear();
        QStringList roles;
#ifdef PVA_HAS_SAPERA
        // 先枚举 Acquisition Device，再按资源标签（CamExpert User Name）建立角色映射。
        // 避免按不存在的用户名查询服务器时由 Sapera 弹出模态错误框。
        for (const auto &role : {QString("1"), QString("2")})
        {
            bool found = false;
            const int serverCount = SapManager::GetServerCount();
            for (int serverIndex = 0; serverIndex < serverCount && !found; ++serverIndex)
            {
                std::array<char, CORSERVER_MAX_STRLEN> serverName{};
                if (!SapManager::GetServerName(serverIndex, serverName.data(), int(serverName.size())))
                    continue;
                const int resourceCount = SapManager::GetResourceCount(serverIndex, SapManager::ResourceAcqDevice);
                for (int resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
                {
                    std::array<char, SapManager::MaxLabelSize> userName{};
                    if (!SapManager::GetResourceName(serverIndex, SapManager::ResourceAcqDevice,
                                                     resourceIndex, userName.data(), int(userName.size())))
                        continue;
                    if (QString::fromLocal8Bit(userName.data()).trimmed() != role)
                        continue;
                    descriptors.insert(role, {serverName.data(), resourceIndex, {}, role});
                    roles.append(role);
                    found = true;
                    break;
                }
            }
        }
        if (error && roles.size() < 2)
        {
            QStringList missing;
            for (const auto &role : {QString("1"), QString("2")})
                if (!descriptors.contains(role))
                    missing.append(role);
            *error = "Camera User Name not found: " + missing.join(", ");
        }
#else
        if (error)
            *error = "Sapera LT SDK was not found when C++ was built";
#endif
        return roles;
    }

    bool DalsaCamera::open(QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (open_)
            return true;
        if (!descriptors.contains(role_))
            enumerate(error);
        if (!descriptors.contains(role_))
        {
            if (error && error->isEmpty())
                *error = "Missing Nano-M2020 with User Name: " + role_;
            return false;
        }
        const auto descriptor = descriptors.value(role_);
        impl_->device = std::make_unique<SapAcqDevice>(SapLocation(descriptor.server.c_str(), descriptor.resource), FALSE);
        if (!impl_->device->Create())
        {
            if (error)
                *error = "Sapera failed to open CAM" + role_ + " (" + descriptor.model + ")";
            impl_->device.reset();
            return false;
        }
        std::array<char, 256> model{};
        impl_->device->GetFeatureValue("DeviceModelName", model.data(), int(model.size()));
        const QString modelName = QString::fromLocal8Bit(model.data());
        if (!modelName.contains("Nano-M2020", Qt::CaseInsensitive) &&
            !modelName.contains("M2020", Qt::CaseInsensitive))
        {
            if (error)
                *error = "Camera User Name " + role_ + " is not Nano-M2020: " + modelName;
            impl_->device->Destroy();
            impl_->device.reset();
            return false;
        }
        open_ = true;
        // Nano-M2020 的 AcquisitionMode 可能是只读的 Continuous。
        // Sapera 的 SapAcqDeviceToBuf::Grab() 不要求先写这个特征。
        const QString acquisitionMode = getEnum("AcquisitionMode");
        if (!acquisitionMode.isEmpty() && acquisitionMode.compare("Continuous", Qt::CaseInsensitive) != 0)
        {
            if (error)
                *error = "Unsupported AcquisitionMode: " + acquisitionMode;
            close();
            return false;
        }
        return true;
#else
        if (error)
            *error = "Sapera LT SDK was not found when C++ was built";
        return false;
#endif
    }

    void DalsaCamera::close()
    {
#ifdef PVA_HAS_SAPERA
        stopStream();
        if (impl_->device)
        {
            impl_->device->Destroy();
            impl_->device.reset();
        }
#endif
        open_ = false;
    }

#ifdef PVA_HAS_SAPERA
    void DalsaCamera::captureCallback(SapXferCallbackInfo *info)
    {
        if (info && info->GetContext())
            static_cast<DalsaCamera *>(info->GetContext())->handleFrame(info->IsTrash() != FALSE);
    }
#endif

    bool DalsaCamera::startStream(QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (streaming_)
            return true;
        if (!open(error))
            return false;
        impl_->buffers = std::make_unique<SapBufferWithTrash>(2, impl_->device.get());
        if (!impl_->buffers->Create())
        {
            impl_->buffers.reset();
            if (error)
                *error = "Sapera failed to create acquisition buffers";
            return false;
        }
        impl_->transfer = std::make_unique<SapAcqDeviceToBuf>(impl_->device.get(), impl_->buffers.get(), &DalsaCamera::captureCallback, this);
        if (!impl_->transfer->Create() || !impl_->transfer->Grab())
        {
            if (impl_->transfer)
                impl_->transfer->Destroy();
            impl_->transfer.reset();
            impl_->buffers->Destroy();
            impl_->buffers.reset();
            if (error)
                *error = "Sapera failed to start acquisition";
            return false;
        }
        streaming_ = true;
        return true;
#else
        if (error)
            *error = "Sapera LT SDK was not found when C++ was built";
        return false;
#endif
    }

    void DalsaCamera::stopStream()
    {
#ifdef PVA_HAS_SAPERA
        if (impl_->transfer)
        {
            // 软件触发模式下 Freeze 会等待下一帧完成；没有待触发帧时，
            // Wait(5000) 会直接卡住界面。模式切换不需要保留最后一帧，
            // Sapera 官方示例也使用 Abort 取消活动采集。
            impl_->transfer->Abort();
            impl_->transfer->Destroy();
            impl_->transfer.reset();
        }
        if (impl_->buffers)
        {
            impl_->buffers->Destroy();
            impl_->buffers.reset();
        }
#endif
        streaming_ = false;
        framePending_.store(false);
    }

    void DalsaCamera::frameConsumed() { framePending_.store(false); }

    bool DalsaCamera::softwareTrigger(QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (!impl_->device || !impl_->device->SetFeatureValue("TriggerSoftware", TRUE))
        {
            if (error)
                *error = featureError("command", "TriggerSoftware");
            return false;
        }
        return true;
#else
        if (error)
            *error = "Sapera LT SDK unavailable";
        return false;
#endif
    }

    bool DalsaCamera::setEnum(const char *feature, const char *value, QString *error)
    {
#ifdef PVA_HAS_SAPERA
        // 避免向相机中值已正确但被标记为只读的枚举特征重复写入。
        const QString current = getEnum(feature);
        if (!current.isEmpty() && current.compare(QString::fromLatin1(value), Qt::CaseInsensitive) == 0)
            return true;
        if (impl_->device && impl_->device->SetFeatureValue(feature, value))
            return true;
#endif
        if (error)
            *error = featureError("set", feature);
        return false;
    }

    bool DalsaCamera::setDouble(const char *feature, double value, QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (impl_->device && impl_->device->SetFeatureValue(feature, value))
            return true;
#endif
        if (error)
            *error = featureError("set", feature);
        return false;
    }

    bool DalsaCamera::setInteger(const char *feature, qint64 value, QString *error)
    {
#ifdef PVA_HAS_SAPERA
        if (impl_->device && impl_->device->SetFeatureValue(feature, INT64(value)))
            return true;
#endif
        if (error)
            *error = featureError("set", feature);
        return false;
    }

    QString DalsaCamera::getEnum(const char *feature) const
    {
#ifdef PVA_HAS_SAPERA
        std::array<char, 128> value{};
        if (impl_->device)
            impl_->device->GetFeatureValue(feature, value.data(), int(value.size()));
        return QString::fromLocal8Bit(value.data());
#else
        return {};
#endif
    }

    double DalsaCamera::getDouble(const char *feature) const
    {
#ifdef PVA_HAS_SAPERA
        double value = 0;
        if (impl_->device)
            impl_->device->GetFeatureValue(feature, &value);
        return value;
#else
        return 0;
#endif
    }

    qint64 DalsaCamera::getInteger(const char *feature) const
    {
#ifdef PVA_HAS_SAPERA
        INT64 value = 0;
        if (impl_->device)
            impl_->device->GetFeatureValue(feature, &value);
        return qint64(value);
#else
        return 0;
#endif
    }

    bool DalsaCamera::setTriggerMode(bool value, QString *error) { return setEnum("TriggerMode", value ? "On" : "Off", error); }
    bool DalsaCamera::setTriggerSource(qint64 value, QString *error) { return setEnum("TriggerSource", value == 0 ? "Software" : "Line1", error); }
    bool DalsaCamera::setTriggerEdge(qint64 value, QString *error) { return setEnum("TriggerActivation", value ? "RisingEdge" : "FallingEdge", error); }
    bool DalsaCamera::setExposure(double value, QString *error) { return setDouble("ExposureTime", value, error); }
    bool DalsaCamera::setGain(double value, QString *error) { return setDouble("Gain", value, error); }
    bool DalsaCamera::setWidth(qint64 value, QString *error) { return setInteger("Width", value, error); }
    bool DalsaCamera::setHeight(qint64 value, QString *error) { return setInteger("Height", value, error); }
    bool DalsaCamera::setOffsetX(qint64 value, QString *error) { return setInteger("OffsetX", value, error); }
    bool DalsaCamera::setOffsetY(qint64 value, QString *error) { return setInteger("OffsetY", value, error); }
    qint64 DalsaCamera::triggerMode() const { return getEnum("TriggerMode").compare("On", Qt::CaseInsensitive) == 0; }
    qint64 DalsaCamera::triggerSource() const { return getEnum("TriggerSource").compare("Software", Qt::CaseInsensitive) == 0 ? 0 : 1; }
    qint64 DalsaCamera::triggerEdge() const { return getEnum("TriggerActivation").compare("RisingEdge", Qt::CaseInsensitive) == 0; }
    double DalsaCamera::exposure() const { return getDouble("ExposureTime"); }
    double DalsaCamera::gain() const { return getDouble("Gain"); }
    qint64 DalsaCamera::width() const { return getInteger("Width"); }
    qint64 DalsaCamera::height() const { return getInteger("Height"); }
    qint64 DalsaCamera::offsetX() const { return getInteger("OffsetX"); }
    qint64 DalsaCamera::offsetY() const { return getInteger("OffsetY"); }

    void DalsaCamera::handleFrame(bool trash)
    {
#ifdef PVA_HAS_SAPERA
        if (trash || !impl_->buffers)
        {
            emit captureFailed(role_, "Sapera returned an incomplete frame");
            return;
        }
        // UI 尚未消费上一帧时直接丢弃本帧，避免自由运行模式淹没 Qt 事件队列。
        if (framePending_.exchange(true))
            return;
        const int index = impl_->buffers->GetIndex();
        void *address = nullptr;
        if (!impl_->buffers->GetAddress(index, &address) || !address)
        {
            framePending_.store(false);
            emit captureFailed(role_, "Sapera buffer address is unavailable");
            return;
        }
        const int width = impl_->buffers->GetWidth();
        const int height = impl_->buffers->GetHeight();
        const int pitch = impl_->buffers->GetPitch();
        const int depth = impl_->buffers->GetPixelDepth();
        cv::Mat image;
        if (depth <= 8)
            image = cv::Mat(height, width, CV_8UC1, address, pitch).clone();
        else if (depth <= 16)
        {
            cv::Mat raw(height, width, CV_16UC1, address, pitch);
            raw.convertTo(image, CV_8UC1, 255.0 / double((1u << depth) - 1u));
        }
        else
            emit captureFailed(role_, QString("Unsupported Nano-M2020 pixel depth: %1").arg(depth));
        impl_->buffers->ReleaseAddress(index, address);
        if (!image.empty())
            emit frameReady(role_, image, nowNs());
        else
            framePending_.store(false);
#endif
    }
}
