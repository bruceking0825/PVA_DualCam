#include "runtime_controller.hpp"
#include "state_store.hpp"
#include "image_storage.hpp"
#include "measurement_payload.hpp"
#include "sherlock_tcp_server.hpp"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QDebug>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace pva
{
    namespace
    {
        constexpr int PlcMeasurementTimeoutMs = 5000;

        bool runtimeSettingsEqual(const RuntimeSettings &left, const RuntimeSettings &right)
        {
            return left.disableCameraForPlcTest == right.disableCameraForPlcTest &&
                   left.connectPlcInOffline == right.connectPlcInOffline &&
                   left.idleSampleIntervalMs == right.idleSampleIntervalMs &&
                   left.neckSampleIntervalMs == right.neckSampleIntervalMs &&
                   left.crownSampleIntervalMs == right.crownSampleIntervalMs &&
                   left.bodySampleIntervalMs == right.bodySampleIntervalMs &&
                   left.endconeSampleIntervalMs == right.endconeSampleIntervalMs &&
                   left.offlineImageDir == right.offlineImageDir &&
                   left.loopIntervalMs == right.loopIntervalMs &&
                   left.stateFile == right.stateFile &&
                   left.stereoPairMaxDeltaMs == right.stereoPairMaxDeltaMs;
        }
    }


    RuntimeController::RuntimeController(MeasurementConfig config, QObject *parent, bool enableTcp)
        : QObject(parent), baseConfig_(config), config_(std::move(config)), networkEnabled_(enableTcp)
    {
        images_ = new ImageStorage(this);
        connect(images_, &ImageStorage::ready, this, [this](const QString &directory, int index, const cv::Mat &image) {
            if (directory == config_.runtime.facetteImageDir) emit facetteReady(index, image);
        });
        connect(images_, &ImageStorage::saved, this, [this](const QString &path) { log("Facette saved to " + path); });
        connect(images_, &ImageStorage::failed, this, [this](const QString &message) { log(message); setStatus(message, false); });
        offlineTimer_ = new QTimer(this);
        plc_ = new PlcSession(this);
        facetTimeoutTimer_ = new QTimer(this);
        facetTimeoutTimer_->setInterval(250);
        connect(offlineTimer_, &QTimer::timeout, this, &RuntimeController::submitOfflineFrame);
        connect(facetTimeoutTimer_, &QTimer::timeout, this, &RuntimeController::expireFacetRequests);
        connect(plc_, &PlcSession::commandReceived, this, &RuntimeController::onSherlockCommand);
        connect(plc_, &PlcSession::payloadReady, this, &RuntimeController::payloadReady);
        connect(plc_, &PlcSession::connectionChanged, this, &RuntimeController::plcConnectionChanged);
        connect(plc_, &PlcSession::logMessage, this, &RuntimeController::log);
        connect(plc_, &PlcSession::configurationChanged, this, [this](MeasurementStage stage) {
            invalidateTasks();
            ++configVersion_;
            stage_ = stage;
            applyPlcConfigOverrides();
            if (worker_) {
                worker_->updateConfig(config_);
                worker_->updateMeasurementRois(plc_->rois());
            }
            emit onlineStageChanged(int(stage_));
            publishState();
        });
        connect(plc_, &PlcSession::cameraExposureRequested, this, &RuntimeController::plcCameraExposureRequested);
        connect(plc_, &PlcSession::timedOut, this, [this] {
            invalidateTasks();
            onlineFrames_.clear();
            log("PLC measurement timed out after 5000 ms");
        });
    }
    RuntimeController::~RuntimeController() { stopRuntime(); images_->flush(); }
    std::optional<MeasurementResult> RuntimeController::takeLatestResult()
    {
        QMutexLocker lock(&displayMutex_);
        auto result = std::move(displayResult_);
        displayResult_.reset();
        return result;
    }
    MeasurementTaskInfo RuntimeController::taskInfo()
    {
        return {runId_, plc_->busy() ? currentRequestId_ : ++nextRequestId_, configVersion_,
                activeOnline_ ? MeasurementSource::Online : MeasurementSource::Offline};
    }
    void RuntimeController::initialize()
    {
        restorePlcState();
        reloadImages();
        for (int i = 1; i <= 4; ++i) loadFacette(i);
        publishState();
    }
    void RuntimeController::publishState()
    {
        emit stateChanged({state_, activeOnline_, stage_, imagePaths_, imageIndex_, config_, plc_->rois(), generation_});
    }
    void RuntimeController::invalidateTasks()
    {
        ++generation_;
        { QMutexLocker lock(&displayMutex_); displayResult_.reset(); }
        if (worker_) worker_->invalidate(generation_);
        if (plc_->busy()) {
            sendPlcPayload("exe_err=Commn err");
            plc_->cancel();
        }
    }
    void RuntimeController::selectStage(MeasurementStage stage)
    {
        const auto previous = stage_;
        stage_ = stage;
        QString error;
        if (!persistPlcState(&error)) { stage_ = previous; log(error); }
        invalidateTasks();
        emit onlineStageChanged(int(stage_));
        publishState();
        if (running() && !activeOnline_) submitOfflineFrame();
    }
    void RuntimeController::submitImages(const cv::Mat &a, const cv::Mat &b, MeasurementStage stage)
    {
        if (!worker_) return;
        worker_->submit(a, b, stage, generation_, taskInfo());
    }
    void RuntimeController::submitOfflineFrame()
    {
        if (!running() || !worker_ || imageIndex_ < 0) return;
        worker_->submitOffline(imagePaths_[imageIndex_], stage_, generation_, taskInfo());
    }
    void RuntimeController::acceptResult(const MeasurementResult &r)
    {
        if (!running() || r.generation != generation_ || r.stage != stage_ ||
            r.task.runId != runId_ || r.task.configurationVersion != configVersion_) return;
        plc_->complete(r);

        { QMutexLocker lock(&displayMutex_); displayResult_ = r; }
        emit resultReady(r);
        if (!r.valid) log(QString::fromStdString(r.message));
        setStatus(QString::fromStdString(r.message), r.valid);
    }
    void RuntimeController::loadFacette(int index)
    {
        images_->load(config_.runtime.facetteImageDir, index);
    }
    void RuntimeController::log(const QString &message)
    {
        dailyLog_.setDirectory(QFileInfo(config_.runtime.stateFile).absoluteDir().filePath("Log"));
        const auto entry = dailyLog_.append(QDateTime::currentDateTime(), message);
        emit logReady(entry.line, entry.merged);
        if (!entry.written) emit status("Log write failed: " + entry.error, false);
    }
    void RuntimeController::setStatus(const QString &message, bool ok) { emit status(message, ok); }
    void RuntimeController::reloadConfig(const MeasurementConfig &config)
    {
        // 与 Python 版本一致：算法、曝光控制等普通参数直接送入运行中的
        // worker；只有运行框架参数或在线相机 ROI 改变时才重启采集链路。
        const bool restart = running() &&
                             (!runtimeSettingsEqual(config_.runtime, config.runtime) ||
                              (activeOnline_ && config_.camera.onlineCropRoi != config.camera.onlineCropRoi));
        const bool online = activeOnline_;
        const bool offlineDirectoryChanged = config_.runtime.offlineImageDir != config.runtime.offlineImageDir;
        const bool facetDirectoryChanged = config_.runtime.facetteImageDir != config.runtime.facetteImageDir;
        if (restart)
            stopRuntime();
        baseConfig_ = config;
        config_ = config;
        plc_->setStatePath(plcStatePath());
        ++configVersion_;
        invalidateTasks();
        if (facetDirectoryChanged)
            for (int index = 1; index <= 4; ++index)
                loadFacette(index);
        applyPlcConfigOverrides();
        if (plc_->pointFitSelected() &&
            (stage_ == MeasurementStage::Crown || stage_ == MeasurementStage::Body))
        {
            const auto next = diameterStage(true, plc_->rois(),
                                            config_.measurement.crownBodyInnerRadiusPx);
            if (next != stage_)
            {
                stage_ = next;
                QString error;
                if (!persistPlcState(&error))
                    log("PLC state save failed: " + error);
                publishState();
                emit onlineStageChanged(int(stage_));
            }
        }
        if (worker_)
            worker_->updateConfig(config_);
        publishState();
        if (offlineDirectoryChanged)
            reloadImages(true);
        else
            publishState();
        if (!restart)
        {
            if (running() && offlineTimer_->isActive())
                offlineTimer_->start(std::max(50, config_.runtime.loopIntervalMs));
        }
        if (restart)
            QTimer::singleShot(0, this, [this, online]
                               { startRuntime(online); });
    }

    void RuntimeController::startRuntime(bool online)
    {
        if (state_ == RunState::Running || state_ == RunState::Starting || state_ == RunState::Stopping)
            return;
        ++runId_;
        invalidateTasks();
        activeOnline_ = online;
        state_ = RunState::Starting;
        publishState();
        reloadImages(true);
        if (!online && imagePaths_.isEmpty())
        {
            state_ = RunState::Faulted;
            publishState();
            setStatus(QString("No offline images found: %1").arg(config_.runtime.offlineImageDir), false);
            return;
        }
        const bool plcOnly = online && config_.runtime.disableCameraForPlcTest;
        if (!plcOnly)
        {
            QString stateWarning;
            auto state = StateStore(config_.runtime.stateFile).load(&stateWarning);
            if (!stateWarning.isEmpty())
                log("State snapshot ignored: " + stateWarning);
            worker_ = std::make_unique<MeasurementWorker>(MeasurementEngine(config_, state), config_.runtime.stateFile);
            worker_->updateMeasurementRois(plc_->rois());
            connect(worker_.get(), &MeasurementWorker::resultReady, this, &RuntimeController::acceptResult);
            connect(worker_.get(), &MeasurementWorker::persistenceFailed, this, &RuntimeController::log);
            connect(worker_.get(), &MeasurementWorker::failed, this, [this](quint64 generation, const QString &m)
                    {
                        if (generation != generation_ || !running()) return;
                        setStatus(m, false);
                        log(m);
                        if (plc_->busy())
                        {
                                                sendPlcPayload("exe_err=Commn err");
                            plc_->cancel();
                        } });
            worker_->start();
        }
        state_ = online && !plcOnly ? RunState::Starting : RunState::Running;


        if (online || config_.runtime.connectPlcInOffline)
            startPlc();
        if (online)
        {
            emit onlineStageChanged(int(stage_));
            publishState();
            if (!plcOnly)
                emit onlineCameraStartRequested(runId_);
            log(plcOnly ? "Runtime started: PLC test (cameras disabled)" : "Runtime started: online");
        }
        else
        {
            offlineTimer_->start(std::max(50, config_.runtime.loopIntervalMs));
            submitOfflineFrame();
            log("Runtime started: offline");
        }
        publishState();
    }

    void RuntimeController::stopRuntime()
    {
        if (state_ == RunState::Stopped && !worker_)
            return;
        const bool wasOnline = activeOnline_;
        state_ = RunState::Stopping;
        invalidateTasks();
        publishState();
        state_ = RunState::Stopped;
        offlineTimer_->stop();
        facetTimeoutTimer_->stop();
        onlineFrames_.clear();
        for (const auto &request : camera2Requests_)
            log(QString("Facette%1 capture cancelled: runtime stopped").arg(request.facetIndex));
        camera2Requests_.clear();
        plc_->cancel();
        stopPlc();
        if (wasOnline)
            emit onlineCameraStopRequested();
        if (worker_)
        {
            // 先停止接收结果，再由后台控制线程完成引擎收尾。
            auto *retiringWorker = worker_.release();
            disconnect(retiringWorker, nullptr, this, nullptr);
            retiringWorker->stop();
            // 仅后台控制线程等待收尾，避免旧引擎与新引擎同时保存状态。
            retiringWorker->wait();
            delete retiringWorker;
        }
        activeOnline_ = false;


        emit cameraConnectionChanged(1, false);
        emit cameraConnectionChanged(2, false);
        log("Runtime stopped");
        publishState();
    }

    void RuntimeController::reloadImages(bool preserve)
    {
        QString old = imageIndex_ >= 0 && imageIndex_ < imagePaths_.size() ? imagePaths_[imageIndex_] : QString();
        QDir dir(config_.runtime.offlineImageDir);
        QStringList filters{"*.bmp", "*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"};
        imagePaths_.clear();
        for (const auto &f : dir.entryInfoList(filters, QDir::Files, QDir::Name))
            imagePaths_ << f.absoluteFilePath();
        imageIndex_ = preserve ? imagePaths_.indexOf(old) : -1;
        if (imageIndex_ < 0 && !imagePaths_.isEmpty())
            imageIndex_ = 0;
        publishState();
    }

    void RuntimeController::setImageIndex(int index)
    {
        if (imagePaths_.isEmpty())
            return;
        imageIndex_ = std::clamp(index, 0, static_cast<int>(imagePaths_.size()) - 1);
        invalidateTasks();
        publishState();
        if (running() && !activeOnline_)
            submitOfflineFrame();
    }

    void RuntimeController::stepImage(int delta)
    {
        // 相对导航在状态所有者内求值，快速连续点击不会使用过期页面索引。
        setImageIndex(imageIndex_ + delta);
    }

    void RuntimeController::triggerOnlineCapture()
    {
        if (!running() || !plc_->busy())
            return;
        if (!activeOnline_) { submitOfflineFrame(); return; }
        onlineFrames_.clear();
        emit onlineCameraTriggerRequested(generation_);
    }

    void RuntimeController::onCameraFrame(const QString &userId, const cv::Mat &image, qint64 timestampNs, quint64 generation)
    {
        if (!running() || !activeOnline_ || !CameraRole::Stereo.contains(userId))
            return;
        if (generation != generation_) return;
        if (!plc_->busy() || !worker_)
            return;
        onlineFrames_[userId] = {timestampNs, image};
        if (!onlineFrames_.contains(CameraRole::Cam1) || !onlineFrames_.contains(CameraRole::Cam2))
            return;
        const auto first = onlineFrames_.value(CameraRole::Cam1);
        const auto second = onlineFrames_.value(CameraRole::Cam2);
        const double deltaMs = std::abs(first.timestampNs - second.timestampNs) / 1.0e6;
        emit frameDeltaChanged(deltaMs);
        if (deltaMs > config_.runtime.stereoPairMaxDeltaMs)
        {
            onlineFrames_.clear();
            const QString message = QString("Online stereo pair dropped: frame delta %1 ms > %2 ms").arg(deltaMs, 0, 'f', 1).arg(config_.runtime.stereoPairMaxDeltaMs);
            setStatus(message, false);
            log(message);
            if (plc_->busy())
            {
                        sendPlcPayload("exe_err=Acq fails");
                plc_->cancel();
            }
            return;
        }
        onlineFrames_.clear();
        submitImages(first.image, second.image, stage_);
    }

    void RuntimeController::onFacetFrame(int requestId, const cv::Mat &image)
    {
        for (int i = 0; i < camera2Requests_.size(); ++i) if (camera2Requests_[i].id == requestId) {
            const int facet = camera2Requests_[i].facetIndex;
            camera2Requests_.removeAt(i); images_->save(config_.runtime.facetteImageDir, facet, image); break;
        }
        if (camera2Requests_.isEmpty()) facetTimeoutTimer_->stop();
    }
    void RuntimeController::onOnlineCaptureFailed(const QString &id, const QString &message, quint64 session)
    {
        onOnlineCameraFailed("Camera " + id + ": " + message, session);
    }
    void RuntimeController::onFacetTriggerFailed(int requestId, const QString &message)
    {
        for (int index = 0; index < camera2Requests_.size(); ++index)
            if (camera2Requests_.at(index).id == requestId)
            {
                const int facetIndex = camera2Requests_.at(index).facetIndex;
                camera2Requests_.removeAt(index);
                const QString detail = QString("Facette%1 trigger failed: %2")
                                           .arg(facetIndex).arg(message);
                log(detail);
                setStatus(detail, false);
                break;
            }
        if (camera2Requests_.isEmpty())
            facetTimeoutTimer_->stop();
    }

    void RuntimeController::expireFacetRequests()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int index = camera2Requests_.size() - 1; index >= 0; --index)
        {
            const auto request = camera2Requests_.at(index);
            if (request.facetIndex == 0 || request.deadlineMs > now)
                continue;
            camera2Requests_.removeAt(index);
            const QString detail = QString("Facette%1 capture timed out").arg(request.facetIndex);
            log(detail);
            setStatus(detail, false);
        }
        if (camera2Requests_.isEmpty())
            facetTimeoutTimer_->stop();
    }

    void RuntimeController::onOnlineCameraStarted(quint64 session)
    {
        if (session && session != runId_) return;
        if (state_ != RunState::Starting || !activeOnline_)
            return;
        state_ = RunState::Running;
        publishState();
        emit cameraConnectionChanged(1, true);
        emit cameraConnectionChanged(2, true);
        plc_->replayExposures();
        log("Online cameras started; waiting for Sherlock TCP command");
    }

    void RuntimeController::onOnlineCameraStopped(quint64 session)
    {
        if (session && session != runId_) return;
        emit cameraConnectionChanged(1, false);
        emit cameraConnectionChanged(2, false);
        log("Online cameras stopped");
    }

    void RuntimeController::onOnlineCameraFailed(const QString &message, quint64 session)
    {
        if (session && session != runId_) return;
        if (!activeOnline_ || state_ == RunState::Stopped) return;
        log("Online camera failed: " + message);
        if (plc_->busy())
        {
                sendPlcPayload("exe_err=Acq fails");
            plc_->cancel();
        }
        stopRuntime();
        state_ = RunState::Faulted;
        setStatus("Online camera failed: " + message, false);
        publishState();
    }

    void RuntimeController::startPlc()
    {
        if (!networkEnabled_) return;
        QString error;
        if (!plc_->start(&error)) { log(error); setStatus(error, false); }
    }
    void RuntimeController::stopPlc() { plc_->stop(); emit plcConnectionChanged(false); }
    void RuntimeController::sendPlcPayload(const QByteArray &payload) { plc_->send(payload); }

    void RuntimeController::onSherlockCommand(const SherlockCommand &command)
    {
        const QString name = command.name;
        log("PLC -> " + QString::fromLatin1(command.raw));

        if (name == "pic_fac1" || name == "pic_fac2" ||
            name == "pic_fac3" || name == "pic_fac4")
        {
            if (!command.parameters.isEmpty())
            {
                sendPlcPayload("err_prm=" + name.toLatin1() + " requires no parameters");
                return;
            }
            const int facetIndex = name.back().digitValue();
            // 旧 PLC 协议立即确认命令；拍照或写盘失败仅在本机报警。
            sendPlcPayload(name.toLatin1() + "=ok");
            if (activeOnline_ && running())
            {
                if (camera2Requests_.size() >= 16) { log("Facette capture rejected: queue full"); return; }
                const int requestId = nextFacetRequestId_++;
                camera2Requests_.enqueue(
                    {requestId, facetIndex,
                     QDateTime::currentMSecsSinceEpoch() + PlcMeasurementTimeoutMs});
                facetTimeoutTimer_->start();
                emit onlineFacetTriggerRequested(requestId);
            }
            else if (!activeOnline_)
                captureOfflineFacette(facetIndex);
            else
                log(QString("Facette%1 capture failed: Camera 2 is not online").arg(facetIndex));
            return;
        }

        if (plc_->handleControlCommand(command, stage_, config_))
            return;

        if (name != "dia_msr" && name != "dia_rec" && name != "dip_msr" && name != "mlt_msr")
        {
            sendPlcPayload("err_unk=" + name.toLatin1());
            return;
        }
        if (!plc_->acquisitionEnabled() || !running() /*|| !activeOnline_*/ || !worker_)
        {
            sendPlcPayload("err_lck=Measurement are disabled");
            return;
        }
        if (plc_->busy())
        {
            sendPlcPayload("exe_err=measurement busy");
            return;
        }

        const auto oldStage = stage_;
        stage_ = stageForPlcCommand(name, stage_, plc_->pointFitSelected(), plc_->rois(),
                                    config_.measurement.crownBodyInnerRadiusPx);
        QString stateError;
        if (!persistPlcState(&stateError))
        {
            stage_ = oldStage;
            sendPlcPayload("err_exc=state save failed");
            log("PLC state save failed: " + stateError);
            return;
        }
        invalidateTasks();
        currentRequestId_ = ++nextRequestId_;
        plc_->begin(name, generation_);
        publishState();
        emit onlineStageChanged(int(stage_));
        triggerOnlineCapture();
    }

    QString RuntimeController::plcStatePath() const
    {
        return QFileInfo(config_.runtime.stateFile).absoluteDir().filePath("plc_runtime_state.json");
    }

    bool RuntimeController::persistPlcState(QString *error) const { return plc_->saveStage(stage_, error); }
    void RuntimeController::applyPlcConfigOverrides()
    {
        config_ = baseConfig_;
        plc_->applyConfigOverrides(config_);
    }
    void RuntimeController::restorePlcState()
    {
        stage_ = plc_->restoreSettings(plcStatePath());
        applyPlcConfigOverrides();
    }

    void RuntimeController::captureOfflineFacette(int index)
    {
        if (imageIndex_ < 0 || imageIndex_ >= imagePaths_.size())
        {
            const QString detail = QString("Facette%1 offline capture failed: no selected image").arg(index);
            log(detail);
            setStatus(detail, false);
            return;
        }
        images_->captureOffline(config_.runtime.facetteImageDir, index, imagePaths_[imageIndex_]);
    }

}
