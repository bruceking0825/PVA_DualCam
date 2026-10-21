#include "camera_service.hpp"
#include "camera_state_store.hpp"
#include "dalsa_camera.hpp"
#include <QFileInfo>
#include <QDir>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <exception>
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

namespace pva
{
    CameraService::CameraService(MeasurementConfig config, QObject *parent)
        : QObject(parent), config_(std::move(config))
    {
        captureTimeout_ = new QTimer(this);
        captureTimeout_->setSingleShot(true);
        captureTimeout_->setInterval(5000);
        connect(captureTimeout_, &QTimer::timeout, this, [this]
                {
            // 超时后关闭本轮采集，阻止迟到帧被归入下一个请求。
            if (activeCapture_) {
                if (!gotCam1_) markCameraFault(CameraRole::Cam1, "Capture timed out");
                if (!gotCam2_) markCameraFault(CameraRole::Cam2, "Capture timed out");
            }
            closeAll();
            emit onlineCameraFailed("Camera capture timed out", session_); });
    }
    CameraService::~CameraService()
    {
        closeAll();
        if (sdkInitialized_)
            DalsaCamera::shutdown();
    }

    void CameraService::resetDevices(const QStringList &ids)
    {
        closeAll();
        current_ = nullptr;
        cameras_.clear();
        for (const auto &rawId : ids)
        {
            const auto id = rawId.trimmed();
            if (id.isEmpty() || cameras_.contains(id))
                continue;
            auto device = std::make_unique<DalsaCamera>(id);
            connect(device.get(), &DalsaCamera::frameReady, this, [this](const QString &id, const cv::Mat &frame, qint64 timestamp)
                    {
                    const auto epoch = deliveryEpoch_.load();
                    QMetaObject::invokeMethod(this, [this, epoch, id, frame, timestamp] {
                        if (epoch == deliveryEpoch_.load()) onFrame(id, frame, timestamp);
                    }, Qt::QueuedConnection); }, Qt::DirectConnection);
            connect(device.get(), &DalsaCamera::captureFailed, this, [this](const QString &id, const QString &error)
                    {
                    const auto epoch = deliveryEpoch_.load();
                    QMetaObject::invokeMethod(this, [this, epoch, id, error] {
                        if (epoch == deliveryEpoch_.load()) onCaptureFailed(id, error);
                    }, Qt::QueuedConnection); }, Qt::DirectConnection);
            cameras_.emplace(id, std::move(device));
        }
    }
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
        for (const auto &[id, value] : cameras_)
            if (value->isOpen())
            {
                QString error;
                if (streamOwner_.isEmpty())
                    applyConfiguredParameters(*value, config_.camera.offlineCropRoi, false, &error);
                else if (onlineStage_ == MeasurementStage::Melt)
                {
                    const bool first = value->userId() != CameraRole::Cam2;
                    const double exposure = first ? config_.camera.initialExposureCamera1 : config_.camera.initialExposureCamera2;
                    const double gain = first ? config_.camera.gainCamera1 : config_.camera.gainCamera2;
                    value->setExposure(exposure, &error) && value->setGain(gain, &error);
                }
                else
                    value->setGain(value->userId() == CameraRole::Cam1 ? config_.camera.gainCamera1 : config_.camera.gainCamera2, &error);
                if (!error.isEmpty())
                    markCameraFault(value->userId(), error);
            }
        publishState();
    }
    void CameraService::refreshCameras()
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        closeAll();
        current_ = nullptr;
        QString error;
        if (!sdkInitialized_)
            sdkInitialized_ = DalsaCamera::initialize(&error);
        if (!sdkInitialized_)
        {
            resetDevices({});
            publishState();
            emit statusChanged(false, error);
            return;
        }
        const auto ids = DalsaCamera::enumerate(&error);
        resetDevices(ids);
        if (error.isEmpty())
            faultedIds_.clear();
        if (!ids.isEmpty())
            current_ = camera(ids.first());
        publishState();
        emit statusChanged(error.isEmpty(), error.isEmpty() ? QString("%1 camera(s) found").arg(ids.size()) : error);
    }
    void CameraService::selectCamera(const QString &id)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        current_ = camera(id);
        publishState();
    }
    void CameraService::publishState()
    {
        CameraSnapshot s;
        for (const auto &[id, value] : cameras_)
        {
            s.ids << value->userId();
            if (!faultedIds_.contains(value->userId()))
                s.healthyIds << value->userId();
        }
        s.production = !streamOwner_.isEmpty();
        if (current_)
        {
            s.selected = current_->userId();
            s.open = current_->isOpen();
            s.streaming = current_->isStreaming();
            if (s.open)
            {
                s.exposure = current_->exposure();
                s.gain = current_->gain();
                s.width = current_->width();
                s.height = current_->height();
                s.offsetX = current_->offsetX();
                s.offsetY = current_->offsetY();
                s.triggerMode = current_->triggerMode();
                s.triggerSource = current_->triggerSource();
                s.triggerEdge = current_->triggerEdge();
            }
        }
        emit stateChanged(s);
    }
    void CameraService::markCameraFault(const QString &id, const QString &message)
    {
        faultedIds_.insert(id);
        emit statusChanged(false, "Camera " + id + ": " + message);
        publishState();
    }
    DalsaCamera *CameraService::camera(const QString &userId) const
    {
        const auto found = cameras_.find(userId);
        return found == cameras_.end() ? nullptr : found->second.get();
    }

    double CameraService::configuredExposure(const QString &userId, bool online) const
    {
        const bool first = userId != CameraRole::Cam2;
        const double initial = first ? config_.camera.initialExposureCamera1 : config_.camera.initialExposureCamera2;
        // Melt 必须从 cnf 初始参数开始；其余在线阶段允许延续上次自动曝光结果。
        return online && onlineStage_ != MeasurementStage::Melt
            ? loadRememberedExposure(userId, initial)
            : initial;
    }

    bool CameraService::applyConfiguredParameters(DalsaCamera &value, const cv::Rect &roi, bool online, QString *error)
    {
        const bool first = value.userId() != CameraRole::Cam2;
        const double exposure = configuredExposure(value.userId(), online);
        const double gain = first ? config_.camera.gainCamera1 : config_.camera.gainCamera2;
        // GenICam ROI 修改顺序必须先清零 Offset，再缩放尺寸，最后恢复 Offset。
        return value.setOffsetX(0, error) && value.setOffsetY(0, error) &&
               value.setWidth(roi.width, error) && value.setHeight(roi.height, error) &&
               value.setOffsetX(roi.x, error) && value.setOffsetY(roi.y, error) &&
               value.setExposure(exposure, error) && value.setGain(gain, error);
    }

    void CameraService::applyMeltParameters()
    {
        if (streamOwner_ != "online")
            return;
        for (const QString &userId : CameraRole::Stereo)
        {
            auto *value = camera(userId);
            if (!value || !value->isOpen())
                continue;
            const bool first = userId == CameraRole::Cam1;
            const double exposure = first ? config_.camera.initialExposureCamera1 : config_.camera.initialExposureCamera2;
            const double gain = first ? config_.camera.gainCamera1 : config_.camera.gainCamera2;
            QString error;
            if (!value->setExposure(exposure, &error) || !value->setGain(gain, &error))
                markCameraFault(userId, "Apply Melt camera parameters failed: " + error);
        }
        publishState();
    }

    void CameraService::setStage(int stage)
    {
        const auto next = MeasurementStage(stage);
        const bool enteringMelt = onlineStage_ != MeasurementStage::Melt && next == MeasurementStage::Melt;
        onlineStage_ = next;
        if (next == MeasurementStage::Dip || next == MeasurementStage::Neck)
            lastExposureAdjustNs_.clear();
        if (enteringMelt)
            applyMeltParameters();
    }

    void CameraService::toggleCamera(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty())
        {
            emit statusChanged(false, !streamOwner_.isEmpty() ? "Cameras are in production use" : "No camera selected");
            publishState();
            return;
        }
        QString error;
        if (checked)
        {
            if (!current_->open(&error) || !applyConfiguredParameters(*current_, config_.camera.offlineCropRoi, false, &error))
                markCameraFault(current_->userId(), error);
        }
        else
            current_->close();
        publishState();
    }

    void CameraService::toggleStream(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty())
        {
            emit statusChanged(false, !streamOwner_.isEmpty() ? "Cameras are in production use" : "No camera selected");
            publishState();
            return;
        }
        QString error;
        if (checked)
        {
            if (!current_->startStream(&error))
                markCameraFault(current_->userId(), error);
        }
        else
            current_->stopStream();
        publishState();
    }

    void CameraService::softwareTrigger()
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString error;
        if (!current_)
        {
            emit statusChanged(false, "No camera selected");
            return;
        }
        if (current_ && !current_->softwareTrigger(&error))
            markCameraFault(current_->userId(), error);
    }

    void CameraService::applyExposure(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setExposure(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyGain(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setGain(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyWidth(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setWidth(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyHeight(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setHeight(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyOffsetX(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setOffsetX(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyOffsetY(double requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setOffsetY(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyTriggerMode(int requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setTriggerMode(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyTriggerSource(int requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setTriggerSource(requested, &e))
            markCameraFault(current_->userId(), e);
        publishState();
    }

    void CameraService::applyTriggerEdge(int requested)
    {
        if (!streamOwner_.isEmpty())
        {
            emit statusChanged(false, "Cameras are in production use");
            return;
        }
        QString e;
        if (current_ && !current_->setTriggerEdge(requested, &e))
            markCameraFault(current_->userId(), e);
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
                markCameraFault(userId, error);
                break;
            }
            if (!value->open(&error) || !applyConfiguredParameters(*value, config_.camera.onlineCropRoi, true, &error) ||
                !value->setTriggerSource(0, &error) || !value->setTriggerMode(true, &error) || !value->startStream(&error))
            {
                if (error.isEmpty())
                    error = "Cannot configure production camera: " + userId;
                markCameraFault(userId, error);
                break;
            }
            faultedIds_.remove(userId);
            publishState();
        }
        if (!error.isEmpty())
        {
            closeAll();
            emit statusChanged(false, error);
            emit onlineCameraFailed(error, session_);
            return;
        }
        streamOwner_ = "online";
        publishState();
        emit onlineCameraStarted(session_);
        emit statusChanged(true, QString("Online cameras started: %1, %2")
                                     .arg(CameraRole::Cam1, CameraRole::Cam2));
    }

    void CameraService::stopOnlineCameras(quint64 requestId)
    {
        try
        {
            if (streamOwner_ == "online")
                saveRememberedExposures();
            closeAll();
            for (const auto &id : CameraRole::Stereo)
            {
                auto *value = camera(id);
                if (value && (value->isOpen() || value->isStreaming()))
                {
                    const QString error = "Camera failed to close: " + id;
                    markCameraFault(id, error);
                    emit onlineCameraStopFailed(requestId, error);
                    return;
                }
            }
            // 无在线流也确认此次请求，编号不使用旧生产会话编号。
            emit onlineCameraStopped(requestId);
        }
        catch (const std::exception &error)
        {
            emit onlineCameraStopFailed(requestId, QString::fromUtf8(error.what()));
        }
    }

    void CameraService::triggerOnlineCameras(quint64 generation)
    {
        if (streamOwner_ != "online" || captureQueue_.size() >= 16)
        {
            emit onlineCameraFailed("Stereo capture unavailable or queue full", session_);
            return;
        }
        captureQueue_.enqueue({0, generation});
        pumpCapture();
    }
    void CameraService::triggerOnlineFacet(int requestId)
    {
        if (streamOwner_ != "online" || captureQueue_.size() >= 16)
        {
            emit onlineFacetTriggerFailed(requestId, "Camera 2 unavailable or queue full");
            return;
        }
        captureQueue_.enqueue({requestId, 0});
        pumpCapture();
    }
    void CameraService::pumpCapture()
    {
        if (activeCapture_ || captureQueue_.isEmpty() || streamOwner_ != "online")
            return;
        activeCapture_ = captureQueue_.dequeue();
        gotCam1_ = activeCapture_->facetId != 0;
        gotCam2_ = false;
        captureTimeout_->start();
        const auto roles = activeCapture_->facetId ? QStringList{CameraRole::Cam2} : CameraRole::Stereo;
        for (const auto &id : roles)
        {
            QString error;
            auto *value = camera(id);
            if (!value || !value->softwareTrigger(&error))
            {
                markCameraFault(id, "Capture trigger failed: " + error);
                closeAll();
                emit onlineCameraFailed("Capture trigger failed: " + error, session_);
                return;
            }
        }
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
        if (onlineCamera && activeCapture_)
        {
            if (activeCapture_->facetId)
            {
                if (userId == CameraRole::Cam2)
                {
                    gotCam2_ = true;
                    emit facetFrameCaptured(activeCapture_->facetId, frame);
                }
            }
            else
            {
                if (userId == CameraRole::Cam1 && !gotCam1_)
                {
                    gotCam1_ = true;
                    emit cameraFrameCaptured(userId, frame, timestampNs, activeCapture_->generation);
                }
                else if (userId == CameraRole::Cam2 && !gotCam2_)
                {
                    gotCam2_ = true;
                    emit cameraFrameCaptured(userId, frame, timestampNs, activeCapture_->generation);
                }
            }
        }
        if (value)
            value->frameConsumed();
        if (activeCapture_ && gotCam1_ && gotCam2_)
        {
            activeCapture_.reset();
            captureTimeout_->stop();
            pumpCapture();
        }
    }

    void CameraService::onCaptureFailed(const QString &userId, const QString &message)
    {
        markCameraFault(userId, message);
        if (streamOwner_ == "online" && CameraRole::Stereo.contains(userId))
            emit onlineCaptureFailed(userId, message, session_);
    }

    std::optional<double> CameraService::autoExposureMean(const QString &userId, const cv::Mat &frame) const
    {
        if (frame.empty() || streamOwner_ != "online" || !config_.camera.autoExposureEnabled ||
            !CameraRole::Stereo.contains(userId) ||
            (onlineStage_ != MeasurementStage::Dip && onlineStage_ != MeasurementStage::Neck))
            return {};
        cv::Mat gray;
        if (frame.channels() == 1)
            gray = frame;
        else
            cv::cvtColor(frame, gray, frame.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        const cv::Rect roi = effectiveAutoExposureRoi(measurementRois_, userId == CameraRole::Cam1 ? 1 : 2,
                                                      userId == CameraRole::Cam1
                                                          ? config_.measurement.autoExposureRoiCamera1
                                                          : config_.measurement.autoExposureRoiCamera2,
                                                      gray.size());
        if (roi.empty())
            return {};
        return cv::mean(gray(roi))[0];
    }

    void CameraService::adjustAutoExposure(DalsaCamera &value, const cv::Mat &frame, qint64 timestampNs)
    {
        const qint64 minimumDelta = qint64(std::max(config_.camera.autoExposureIntervalMs, 50)) * 1000000;
        if (timestampNs - lastExposureAdjustNs_.value(value.userId(), 0) < minimumDelta)
            return;
        const auto mean = autoExposureMean(value.userId(), frame);
        if (!mean)
            return;
        const double target = std::max(config_.camera.autoExposureTarget, 1.0);
        const double error = target - *mean;
        lastExposureAdjustNs_[value.userId()] = timestampNs;
        if (std::abs(error) <= config_.camera.autoExposureDeadband)
            return;
        const double next = std::clamp(value.exposure() * (1.0 + std::max(config_.camera.autoExposureGain, 0.0) * error / target),
                                       std::min(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs),
                                       std::max(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs));
        QString exposureError;
        if (!value.setExposure(next, &exposureError))
        {
            markCameraFault(value.userId(), "Auto exposure failed: " + exposureError);
            return;
        }
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
        captureQueue_.clear();
        activeCapture_.reset();
        for (const auto &[id, device] : cameras_)
            device->close();
        // SDK 已停止回调；关闭前已排队的帧和错误不能进入下一次采集。
        ++deliveryEpoch_;
        streamOwner_.clear();
        publishState();
    }

}
