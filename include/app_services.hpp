#pragma once
#include "runtime_controller.hpp"
#include "camera_service.hpp"
#include <QThread>
#include <QPointer>
#include <memory>
namespace pva
{
    class EkzServerStatus;
    // 装配层唯一拥有两个服务线程；页面销毁不改变生产设备生命周期。
    class AppServices final : public QObject
    {
        Q_OBJECT
    public:
        explicit AppServices(const MeasurementConfig &config, QObject *parent = nullptr, bool enableTcp = true);
        ~AppServices() override;
        RuntimeController &runtime() { return *runtime_; }
        CameraService &cameras() { return *cameras_; }
        void start();
        void stop();
    private:
        void publishInitializedStatus();
        std::unique_ptr<EkzServerStatus> ekzStatus_;
        bool runtimeInitialized_{}, cameraInitialized_{};
        QThread runtimeThread_, cameraThread_;
        QPointer<RuntimeController> runtime_;
        QPointer<CameraService> cameras_;
        bool started_{};
    };
}
