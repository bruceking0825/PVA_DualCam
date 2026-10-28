#include "app_services.hpp"
#include "app_signals.hpp"
#include "config_manager.hpp"
#include "ekz_server_status.hpp"
namespace pva
{
    AppServices::AppServices(const MeasurementConfig &config, QObject *parent, bool enableTcp)
        : QObject(parent), runtime_(new RuntimeController(config, this, enableTcp)), cameras_(new CameraService(config, this))
    {
        // 禁用外部通信的测试实例不发布现场 EKZ 状态。
        if (enableTcp) ekzStatus_ = std::make_unique<EkzServerStatus>();
        qRegisterMetaType<RuntimeSnapshot>("pva::RuntimeSnapshot");
        qRegisterMetaType<CameraSnapshot>("pva::CameraSnapshot");
        qRegisterMetaType<MeasurementResult>("pva::MeasurementResult");
        qRegisterMetaType<cv::Mat>("cv::Mat");
        connect(&runtimeThread_, &QThread::finished, runtime_, &QObject::deleteLater);
        connect(&cameraThread_, &QThread::finished, cameras_, &QObject::deleteLater);
        connect(&runtimeThread_, &QThread::started, runtime_, [this] {
            runtime_->initialize();
            QMetaObject::invokeMethod(this, [this] {
                runtimeInitialized_ = true;
                publishInitializedStatus();
            }, Qt::QueuedConnection);
        });
        connect(&cameraThread_, &QThread::started, cameras_, [this] {
            cameras_->initialize();
            QMetaObject::invokeMethod(this, [this] {
                cameraInitialized_ = true;
                publishInitializedStatus();
            }, Qt::QueuedConnection);
        });
        connect(runtime_, &RuntimeController::onlineCameraStartRequested, cameras_, &CameraService::startOnlineCameras);
        connect(runtime_, &RuntimeController::onlineCameraStopRequested, cameras_, &CameraService::stopOnlineCameras);
        connect(runtime_, &RuntimeController::onlineCameraTriggerRequested, cameras_, &CameraService::triggerOnlineCameras);
        connect(runtime_, &RuntimeController::onlineFacetTriggerRequested, cameras_, &CameraService::triggerOnlineFacet);
        connect(runtime_, &RuntimeController::onlineStageChanged, cameras_, &CameraService::setStage);
        connect(runtime_, &RuntimeController::stateChanged, cameras_,
                [this](const RuntimeSnapshot &state) { cameras_->setMeasurementRois(state.rois); });
        connect(cameras_, &CameraService::onlineCameraStarted, runtime_, &RuntimeController::onOnlineCameraStarted);
        connect(cameras_, &CameraService::onlineCameraStopped, runtime_, &RuntimeController::onOnlineCameraStopped);
        connect(cameras_, &CameraService::onlineCameraStopFailed, runtime_, &RuntimeController::onOnlineCameraStopFailed);
        connect(cameras_, &CameraService::onlineCameraFailed, runtime_, &RuntimeController::onOnlineCameraFailed);
        connect(cameras_, &CameraService::onlineCaptureFailed, runtime_, &RuntimeController::onOnlineCaptureFailed);
        connect(cameras_, &CameraService::onlineFacetTriggerFailed, runtime_, &RuntimeController::onFacetTriggerFailed);
        connect(cameras_, &CameraService::facetFrameCaptured, runtime_, &RuntimeController::onFacetFrame);
        connect(cameras_, &CameraService::cameraFrameCaptured, runtime_, &RuntimeController::onCameraFrame);
        connect(runtime_, &RuntimeController::status, this, [](const QString &text, bool ok) {
            emit AppSignals::instance().status("Measurement", ok ? "OK" : "NG", ok ? "info" : "error", text);
        });
        auto update = [this] {
            if (!runtime_ || !cameras_) return;
            const auto config = ConfigManager::instance().config();
            QMetaObject::invokeMethod(cameras_, [this, config] { cameras_->reloadConfig(config); }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(runtime_, [this, config] { runtime_->reloadConfig(config); }, Qt::QueuedConnection);
        };
        connect(&ConfigManager::instance(), &ConfigManager::batchChanged, this, update);
        connect(&ConfigManager::instance(), &ConfigManager::entryChanged, this, [update](const QString &, const QString &) { update(); });
    }
    void AppServices::start()
    {
        if (started_ || !runtime_ || !cameras_) return;
        if (ekzStatus_) {
            QString error;
            if (!ekzStatus_->start(&error))
                emit AppSignals::instance().status("EKZExplorer", "NG", "error", error);
        }
        runtimeInitialized_ = cameraInitialized_ = false;
        started_ = true;
        runtime_->setParent(nullptr);
        cameras_->setParent(nullptr);
        runtime_->moveToThread(&runtimeThread_);
        cameras_->moveToThread(&cameraThread_);
        cameraThread_.start();
        runtimeThread_.start();
    }
    void AppServices::stop()
    {
        if (!started_) return;
        started_ = false;
        if (ekzStatus_) ekzStatus_->setReady(false);
        QMetaObject::invokeMethod(runtime_, &RuntimeController::shutdown, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(cameras_, &CameraService::closeAll, Qt::BlockingQueuedConnection);
        runtimeThread_.quit(); cameraThread_.quit();
        runtimeThread_.wait(); cameraThread_.wait();
        if (ekzStatus_) ekzStatus_->stop();
    }
    void AppServices::publishInitializedStatus()
    {
        // Ready 表示视觉服务完成初始化；不等同于正在采集，离线/等待 PLC 时仍可就绪。
        if (started_ && runtimeInitialized_ && cameraInitialized_ && ekzStatus_)
            ekzStatus_->setReady(true);
    }
    AppServices::~AppServices() { stop(); }
}
