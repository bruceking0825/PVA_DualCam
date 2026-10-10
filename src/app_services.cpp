#include "app_services.hpp"
#include "app_signals.hpp"
#include "config_manager.hpp"
namespace pva {
AppServices::AppServices(const MeasurementConfig &config, QObject *parent, bool enableTcp)
    : QObject(parent), runtime_(new RuntimeController(config, this, enableTcp)), cameras_(new CameraService(config, this))
{
    qRegisterMetaType<RuntimeSnapshot>("pva::RuntimeSnapshot");
    qRegisterMetaType<CameraSnapshot>("pva::CameraSnapshot");
    qRegisterMetaType<MeasurementResult>("pva::MeasurementResult");
    qRegisterMetaType<cv::Mat>("cv::Mat");
    connect(&runtimeThread_, &QThread::finished, runtime_, &QObject::deleteLater);
    connect(&cameraThread_, &QThread::finished, cameras_, &QObject::deleteLater);
    connect(&runtimeThread_, &QThread::started, runtime_, &RuntimeController::initialize);
    connect(&cameraThread_, &QThread::started, cameras_, &CameraService::initialize);
    connect(runtime_, &RuntimeController::onlineCameraStartRequested, cameras_, &CameraService::startOnlineCameras);
    connect(runtime_, &RuntimeController::onlineCameraStopRequested, cameras_, &CameraService::stopOnlineCameras);
    connect(runtime_, &RuntimeController::onlineCameraTriggerRequested, cameras_, &CameraService::triggerOnlineCameras);
    connect(runtime_, &RuntimeController::onlineFacetTriggerRequested, cameras_, &CameraService::triggerOnlineFacet);
    connect(runtime_, &RuntimeController::onlineStageChanged, cameras_, &CameraService::setStage);
    connect(runtime_, &RuntimeController::stateChanged, cameras_,
            [this](const RuntimeSnapshot &state) { cameras_->setMeasurementRois(state.rois); });
    connect(runtime_, &RuntimeController::plcCameraExposureRequested, cameras_, &CameraService::applyPlcExposure);
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
    QMetaObject::invokeMethod(runtime_, "shutdown", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(cameras_, "closeAll", Qt::BlockingQueuedConnection);
    runtimeThread_.quit(); cameraThread_.quit();
    runtimeThread_.wait(); cameraThread_.wait();
    started_ = false;
}
AppServices::~AppServices() { stop(); }
}
