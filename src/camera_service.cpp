#include "camera_service.hpp"
#include "camera_state_store.hpp"
#include "dalsa_camera.hpp"
#include <QFileInfo>
#include <QDir>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
namespace
{
    QString cameraStatePath(const pva::MeasurementConfig &config)
    {
        return QFileInfo(config.runtime.stateFile).absoluteDir().absoluteFilePath("camera_state.json");
    }
    cv::Rect clippedRoi(cv::Rect roi, const cv::Size &size)
    {
        return roi & cv::Rect(0, 0, size.width, size.height);
    }
}


namespace pva {
CameraService::CameraService(MeasurementConfig config, QObject *parent)
    : QObject(parent), config_(std::move(config)), cameraManager_(std::make_unique<CameraManager>(this))
{
    captureTimeout_ = new QTimer(this);
    captureTimeout_->setSingleShot(true);
    captureTimeout_->setInterval(5000);
    connect(captureTimeout_, &QTimer::timeout, this, [this] {
        // 超时后关闭本轮采集，阻止迟到帧被归入下一个请求。
        closeAll();
        emit onlineCameraFailed("Camera capture timed out", session_);
    });
    connect(cameraManager_.get(), &CameraManager::frameReady, this, &CameraService::onFrame);
    connect(cameraManager_.get(), &CameraManager::captureFailed, this, &CameraService::onCaptureFailed);
}
CameraService::~CameraService() { closeAll(); }
cv::Mat CameraService::takePreviewFrame()
{
    QMutexLocker lock(&previewMutex_);
    cv::Mat result = std::move(previewFrame_);
    previewFrame_.release();
    return result;
}
void CameraService::initialize() { refreshCameras(); }
void CameraService::reloadConfig(const MeasurementConfig &config)
{
    config_ = config;
    lastExposureAdjustNs_.clear();
    for (auto *value : cameraManager_->getAll()) if (value->isOpen()) {
        QString error;
        if (streamOwner_.isEmpty()) applyConfiguredParameters(*value, config_.camera.offlineCropRoi, false, &error);
        else value->setGain(value->userId() == CameraRole::Cam1 ? config_.camera.gainCamera1 : config_.camera.gainCamera2, &error);
        if (!error.isEmpty()) setStatus(false, error);
    }
    publishState();
}
void CameraService::refreshCameras()
{
    if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
    closeAll();
    current_ = nullptr;
    QString error;
    if (!cameraManager_->initialize(&error)) { setStatus(false, error); return; }
    const auto ids = DalsaCamera::enumerate(&error);
    cameraManager_->reset(ids);
    if (!ids.isEmpty()) current_ = camera(ids.first());
    publishState();
    setStatus(error.isEmpty(), error.isEmpty() ? QString("%1 camera(s) found").arg(ids.size()) : error);
}
void CameraService::selectCamera(const QString &id)
{
    if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
    current_ = camera(id);
    publishState();
}
void CameraService::publishState()
{
    CameraSnapshot s;
    for (auto *value : cameraManager_->getAll()) s.ids << value->userId();
    s.production = !streamOwner_.isEmpty();
    if (current_) {
        s.selected = current_->userId();
        s.open = current_->isOpen();
        s.streaming = current_->isStreaming();
        if (s.open) {
            s.exposure = current_->exposure(); s.gain = current_->gain();
            s.width = current_->width(); s.height = current_->height();
            s.offsetX = current_->offsetX(); s.offsetY = current_->offsetY();
            s.triggerMode = current_->triggerMode(); s.triggerSource = current_->triggerSource(); s.triggerEdge = current_->triggerEdge();
        }
    }
    emit stateChanged(s);
}
    DalsaCamera *CameraService::camera(const QString &userId) const
    {
        return cameraManager_->get(userId);
    }

    bool CameraService::applyConfiguredParameters(DalsaCamera &value, const cv::Rect &roi, bool online, QString *error)
    {
        const bool first = value.userId() != CameraRole::Cam2;
        const double initial = first ? config_.camera.initialExposureCamera1 : config_.camera.initialExposureCamera2;
        const double exposure = online ? loadRememberedExposure(value.userId(), initial) : initial;
        const double gain = first ? config_.camera.gainCamera1 : config_.camera.gainCamera2;
        // GenICam ROI 修改顺序必须先清零 Offset，再缩放尺寸，最后恢复 Offset。
        return value.setOffsetX(0, error) && value.setOffsetY(0, error) &&
               value.setWidth(roi.width, error) && value.setHeight(roi.height, error) &&
               value.setOffsetX(roi.x, error) && value.setOffsetY(roi.y, error) &&
               value.setExposure(exposure, error) && value.setGain(gain, error);
    }

    void CameraService::toggleCamera(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty())
        {
            setStatus(false, !streamOwner_.isEmpty() ? "Cameras are in production use" : "No camera selected");
            publishState();
            return;
        }
        QString error;
        if (checked)
        {
            if (!current_->open(&error) || !applyConfiguredParameters(*current_, config_.camera.offlineCropRoi, false, &error))
                setStatus(false, error);
        }
        else
            current_->close();
        publishState();
    }

    void CameraService::toggleStream(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty())
        {
            setStatus(false, !streamOwner_.isEmpty() ? "Cameras are in production use" : "No camera selected");
            publishState();
            return;
        }
        QString error;
        if (checked)
        {
            if (!current_->startStream(&error))
                setStatus(false, error);
        }
        else
            current_->stopStream();
        publishState();
    }

    void CameraService::softwareTrigger()
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString error;
        if (!current_ || !current_->softwareTrigger(&error))
            setStatus(false, error);
    }

    void CameraService::applyExposure(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setExposure(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyGain(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setGain(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyWidth(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setWidth(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyHeight(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setHeight(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyOffsetX(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setOffsetX(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyOffsetY(double requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setOffsetY(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyTriggerMode(int requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setTriggerMode(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyTriggerSource(int requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setTriggerSource(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::applyTriggerEdge(int requested)
    {
        if (!streamOwner_.isEmpty()) { setStatus(false, "Cameras are in production use"); return; }
        QString e;
        if (current_ && !current_->setTriggerEdge(requested, &e))
            setStatus(false, e);
        publishState();
    }

    void CameraService::startOnlineCameras(quint64 session)
    {
        session_ = session;
        if (!streamOwner_.isEmpty())
        {
            emit onlineCameraFailed("Cameras are already in use", session_);
            return;
        }
        // 生产接管前结束手动预览，避免在采集中修改 ROI；同时撤销旧回调。
        closeAll();
        QString error;
        for (const QString &userId : CameraRole::Stereo)
        {
            if (!error.isEmpty())
                break;
            auto *value = camera(userId);
            if (!value)
            {
                error = "Camera Device User ID not found: " + userId;
                break;
            }
            if (!value->open(&error) || !applyConfiguredParameters(*value, config_.camera.onlineCropRoi, true, &error) ||
                !value->setTriggerSource(0, &error) || !value->setTriggerMode(true, &error) || !value->startStream(&error))
            {
                if (error.isEmpty()) error = "Cannot configure production camera: " + userId;
                break;
            }
        }
        if (!error.isEmpty())
        {
            closeAll();
            setStatus(false, error);
            emit onlineCameraFailed(error, session_);
            return;
        }
        streamOwner_ = "online";
        publishState();
        emit onlineCameraStarted(session_);
        setStatus(true, QString("Online cameras started: %1, %2")
                            .arg(CameraRole::Cam1, CameraRole::Cam2));
    }

    void CameraService::stopOnlineCameras()
    {
        if (streamOwner_ != "online")
            return;
        saveRememberedExposures();
        plcExposureOverrides_.clear();
        closeAll();
        emit onlineCameraStopped(session_);
    }

    void CameraService::triggerOnlineCameras(quint64 generation)
    {
        if (streamOwner_ != "online" || captureQueue_.size() >= 16) {
            emit onlineCameraFailed("Stereo capture unavailable or queue full", session_); return;
        }
        captureQueue_.enqueue({0, generation}); pumpCapture();
    }
    void CameraService::triggerOnlineFacet(int requestId)
    {
        if (streamOwner_ != "online" || captureQueue_.size() >= 16) {
            emit onlineFacetTriggerFailed(requestId, "Camera 2 unavailable or queue full"); return;
        }
        captureQueue_.enqueue({requestId, 0}); pumpCapture();
    }
    void CameraService::pumpCapture()
    {
        if (activeCapture_ || captureQueue_.isEmpty() || streamOwner_ != "online") return;
        activeCapture_ = captureQueue_.dequeue();
        gotCam1_ = activeCapture_->facetId != 0; gotCam2_ = false;
        captureTimeout_->start();
        const auto roles = activeCapture_->facetId ? QStringList{CameraRole::Cam2} : CameraRole::Stereo;
        for (const auto &id : roles) {
            QString error;
            auto *value = camera(id);
            if (!value || !value->softwareTrigger(&error)) {
                closeAll(); emit onlineCameraFailed("Capture trigger failed: " + error, session_); return;
            }
        }
    }

    void CameraService::applyPlcExposure(const QString &userId, double exposureUs)
    {
        auto *value = camera(userId);
        if (!value || !value->isOpen())
        {
            setStatus(false, "PLC exposure target is not open: " + userId);
            return;
        }

        QString error;
        if (!value->setExposure(exposureUs, &error))
        {
            setStatus(false, QString("Apply PLC exposure to %1 failed: %2").arg(userId, error));
            return;
        }

        // PLC的exptme命令具有最高优先级；收到后停止该相机本轮在线自动曝光覆盖。
        plcExposureOverrides_.insert(userId, exposureUs);
        emit cameraExposureChanged(userId, exposureUs);
        saveRememberedExposures();
        publishState();
    }

    void CameraService::onFrame(const QString &userId, const cv::Mat &frame, qint64 timestampNs)
    {
        auto *value = camera(userId);
        if (value)
            adjustAutoExposure(*value, frame, timestampNs);
        // GigE 特征读取是同步操作，不在每个图像回调中执行。
        const bool onlineCamera = CameraRole::Stereo.contains(userId);
        if (value && onlineCamera && timestampNs - lastExposurePublishNs_.value(userId, 0) >= 500000000LL)
        {
            lastExposurePublishNs_[userId] = timestampNs;
            emit cameraExposureChanged(userId, value->exposure());
        }
        // 手动自由运行预览限制为 10 FPS，图像管线不会占满 UI 线程。
        if (streamOwner_.isEmpty() && timestampNs - lastManualPreviewNs_ >= 100000000LL)
        {
            lastManualPreviewNs_ = timestampNs;
            QMutexLocker lock(&previewMutex_);
            previewFrame_ = frame;
        }
        if (onlineCamera && activeCapture_) {
            if (activeCapture_->facetId) {
                if (userId == CameraRole::Cam2) { gotCam2_ = true; emit facetFrameCaptured(activeCapture_->facetId, frame); }
            } else {
                if (userId == CameraRole::Cam1 && !gotCam1_) {
                    gotCam1_ = true; emit cameraFrameCaptured(userId, frame, timestampNs, activeCapture_->generation);
                } else if (userId == CameraRole::Cam2 && !gotCam2_) {
                    gotCam2_ = true; emit cameraFrameCaptured(userId, frame, timestampNs, activeCapture_->generation);
                }
            }
        }
        if (value)
            value->frameConsumed();
        if (activeCapture_ && gotCam1_ && gotCam2_) {
            activeCapture_.reset(); captureTimeout_->stop(); pumpCapture();
        }
    }

    void CameraService::onCaptureFailed(const QString &userId, const QString &message)
    {
        setStatus(false, "Camera " + userId + ": " + message);
        if (streamOwner_ == "online" && CameraRole::Stereo.contains(userId))
            emit onlineCaptureFailed(userId, message, session_);
    }

    void CameraService::adjustAutoExposure(DalsaCamera &value, const cv::Mat &frame, qint64 timestampNs)
    {
        if (plcExposureOverrides_.contains(value.userId()))
            return;
        if (streamOwner_ != "online" || !config_.camera.autoExposureEnabled ||
            (onlineStage_ != MeasurementStage::Idle && onlineStage_ != MeasurementStage::Neck))
            return;
        const qint64 minimumDelta = qint64(std::max(config_.camera.autoExposureIntervalMs, 50)) * 1000000;
        if (timestampNs - lastExposureAdjustNs_.value(value.userId(), 0) < minimumDelta)
            return;
        if (!CameraRole::Stereo.contains(value.userId()))
            return;
        const cv::Rect roi = clippedRoi(value.userId() == CameraRole::Cam1
                                            ? config_.measurement.autoExposureRoiCamera1
                                            : config_.measurement.autoExposureRoiCamera2,
                                        frame.size());
        if (roi.empty())
            return;
        cv::Mat gray;
        if (frame.channels() == 1)
            gray = frame;
        else
            cv::cvtColor(frame, gray, frame.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        const double mean = cv::mean(gray(roi))[0];
        const double target = std::max(config_.camera.autoExposureTarget, 1.0);
        const double error = target - mean;
        lastExposureAdjustNs_[value.userId()] = timestampNs;
        if (std::abs(error) <= config_.camera.autoExposureDeadband)
            return;
        const double next = std::clamp(value.exposure() * (1.0 + std::max(config_.camera.autoExposureGain, 0.0) * error / target),
                                       std::min(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs),
                                       std::max(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs));
        value.setExposure(next);
        saveRememberedExposures();
    }

    double CameraService::loadRememberedExposure(const QString &userId, double fallback) const
    {
        const QString key = "camera:" + userId;
        CameraStateStore store(cameraStatePath(config_));
        return store.loadExposures({{key, fallback}},
                                   config_.camera.autoExposureMinUs,
                                   config_.camera.autoExposureMaxUs)
            .value(key, fallback);
    }

    void CameraService::saveRememberedExposures() const
    {
        QHash<QString, double> exposures;
        for (const QString &userId : CameraRole::Stereo)
        {
            if (auto *value = camera(userId); value && value->isOpen())
                exposures.insert("camera:" + userId, value->exposure());
        }
        if (exposures.isEmpty())
            return;
        CameraStateStore(cameraStatePath(config_)).saveExposures(exposures);
    }

    void CameraService::closeAll()
    {
        captureTimeout_->stop();
        captureQueue_.clear(); activeCapture_.reset();
        cameraManager_->closeAll();
        streamOwner_.clear();
        publishState();
    }

}
