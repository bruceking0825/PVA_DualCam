#include "page_home.hpp"
#include "state_store.hpp"
#include "app_signals.hpp"
#include "config_manager.hpp"
#include "sherlock_protocol.hpp"
#include "sherlock_tcp_server.hpp"
#include "ui_PageHome.h"
#include <QButtonGroup>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTextCursor>
#include <QTimer>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <QDebug>

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

    PageHome::PageHome(MeasurementConfig config, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageHome>()), config_(std::move(config))
    {
        initializePage([this]
                       { ui_->setupUi(this); });
    }

    void PageHome::initializeState()
    {
        offlineTimer_ = new QTimer(this);
        plcMeasurementTimeout_ = new QTimer(this);
        plcMeasurementTimeout_->setSingleShot(true);
        facetTimeoutTimer_ = new QTimer(this);
        facetTimeoutTimer_->setInterval(250);
        stage_ = MeasurementStage::Neck;
    }

    void PageHome::setupPageUi()
    {
        ui_->cam1GraphicsView->setViewId(1);
        ui_->cam2GraphicsView->setViewId(2);
        ui_->cam1GraphicsView->setText("Camera 1");
        ui_->cam2GraphicsView->setText("Camera 2");
        ui_->viewSplitter->setSizes({300, 300});
        ui_->mainSplitter->setSizes({680, 170, 300});
        auto *group = new QButtonGroup(this);
        group->setExclusive(true);
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
            group->addButton(b);
        applyStageToUi();
    }

    void PageHome::bindEvents()
    {
        connect(offlineTimer_, &QTimer::timeout, this, &PageHome::submitOfflineFrame);
        connect(plcMeasurementTimeout_, &QTimer::timeout, this, [this]
                {
                    if (pendingPlcCommand_.isEmpty())
                        return;
                    sendPlcPayload("exe_err=Commn err");
                    log("PLC measurement timed out after 5000 ms");
                    pendingPlcCommand_.clear();
                    onlineFrames_.clear();
                    awaitingMeasurementCam1_ = false;
                    for (int index = camera2Requests_.size() - 1; index >= 0; --index)
                        if (camera2Requests_.at(index).facetIndex == 0)
                            camera2Requests_.removeAt(index);
                });
        connect(facetTimeoutTimer_, &QTimer::timeout, this, &PageHome::expireFacetRequests);
        connect(ui_->btnStart, &QPushButton::clicked, this, &PageHome::toggleRuntime);
        connect(ui_->btnOnline, &QPushButton::toggled, this, &PageHome::toggleOnline);
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
            connect(b, &QPushButton::clicked, this, &PageHome::selectStage);
        connect(ui_->btnFirstImage, &QPushButton::clicked, this, &PageHome::firstImage);
        connect(ui_->btnPreviousImage, &QPushButton::clicked, this, &PageHome::previousImage);
        connect(ui_->btnNextImage, &QPushButton::clicked, this, &PageHome::nextImage);
        connect(ui_->btnLastImage, &QPushButton::clicked, this, &PageHome::lastImage);
        connect(ui_->cam1GraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                {
                    viewInfo_[0].x = x;
                    viewInfo_[0].y = y;
                    viewInfo_[0].gray = gray;
                    updateViewInfo(1);
                });
        connect(ui_->cam2GraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                {
                    viewInfo_[1].x = x;
                    viewInfo_[1].y = y;
                    viewInfo_[1].gray = gray;
                    updateViewInfo(2);
                });
    }

    void PageHome::bindSignals()
    {
        auto &appSignals = AppSignals::instance();
        connect(&appSignals, &AppSignals::cameraFrameCaptured, this, &PageHome::onCameraFrame);
        connect(&appSignals, &AppSignals::cameraExposureChanged, this, &PageHome::onCameraExposure);
        connect(&appSignals, &AppSignals::onlineCameraStarted, this, &PageHome::onOnlineCameraStarted);
        connect(&appSignals, &AppSignals::onlineCameraStopped, this, &PageHome::onOnlineCameraStopped);
        connect(&appSignals, &AppSignals::onlineCameraFailed, this, &PageHome::onOnlineCameraFailed);
        connect(&appSignals, &AppSignals::onlineCaptureFailed, this, &PageHome::onOnlineCaptureFailed);
        connect(&appSignals, &AppSignals::onlineFacetTriggerFailed, this, &PageHome::onFacetTriggerFailed);
        connect(&appSignals, &AppSignals::appClose, this, &PageHome::stopRuntime);
        connect(&ConfigManager::instance(), &ConfigManager::batchChanged, this, [this]
                { reloadConfig(ConfigManager::instance().config()); });
        connect(&ConfigManager::instance(), &ConfigManager::entryChanged, this,
                [this](const QString &, const QString &)
                { reloadConfig(ConfigManager::instance().config()); });
    }

    void PageHome::onReady()
    {
        updateViewInfo(1);
        updateViewInfo(2);
        restorePlcState();
        for (int index = 1; index <= 4; ++index)
            loadFacette(index);
        applyStageToUi();
        reloadImages();
        setConnectionLed(ui_->lblCamera1Status, false);
        setConnectionLed(ui_->lblCamera2Status, false);
        setConnectionLed(ui_->lblPlcStatus, false);
        refreshControls();
        if (imagePaths_.isEmpty())
            setStatus(QString("No offline images found: %1").arg(config_.runtime.offlineImageDir), false);
        else
            setStatus(QString("Ready: %1 offline images").arg(imagePaths_.size()), true);
    }
    PageHome::~PageHome()
    {
        stopRuntime();
    }
    void PageHome::reloadConfig(const MeasurementConfig &config)
    {
        // 与 Python 版本一致：算法、曝光控制等普通参数直接送入运行中的
        // worker；只有运行框架参数或在线相机 ROI 改变时才重启采集链路。
        const bool restart = running_ &&
                             (!runtimeSettingsEqual(config_.runtime, config.runtime) ||
                              (activeOnline_ && config_.camera.onlineCropRoi != config.camera.onlineCropRoi));
        const bool online = activeOnline_;
        const bool offlineDirectoryChanged = config_.runtime.offlineImageDir != config.runtime.offlineImageDir;
        const bool facetDirectoryChanged = config_.runtime.facetteImageDir != config.runtime.facetteImageDir;
        if (restart)
            stopRuntime();
        config_ = config;
        if (facetDirectoryChanged)
            for (int index = 1; index <= 4; ++index)
                loadFacette(index);
        applyPlcConfigOverrides();
        if (pointFitSelected_ &&
            (stage_ == MeasurementStage::Crown || stage_ == MeasurementStage::Body))
        {
            const auto next = diameterStage(true, plcRois_,
                                            config_.measurement.crownBodyInnerRadiusPx);
            if (next != stage_)
            {
                stage_ = next;
                QString error;
                if (!persistPlcState(&error))
                    log("PLC state save failed: " + error);
                applyStageToUi();
                emit AppSignals::instance().onlineStageChanged(int(stage_));
            }
        }
        if (worker_)
            worker_->updateConfig(config_);
        updatePlcOverlayViews();
        if (offlineDirectoryChanged)
            reloadImages(true);
        else
            refreshControls();
        if (!restart)
        {
            if (running_ && offlineTimer_->isActive())
                offlineTimer_->start(std::max(50, config_.runtime.loopIntervalMs));
        }
        if (restart)
            QTimer::singleShot(0, this, [this, online]
                               { startRuntime(online); });
    }
    void PageHome::toggleRuntime()
    {
        if (running_)
        {
            stopRuntime();
            return;
        }
        startRuntime(ui_->btnOnline->isChecked());
    }
    void PageHome::startRuntime(bool online)
    {
        if (running_)
            return;
        reloadImages(true);
        if (!online && imagePaths_.isEmpty())
        {
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
            worker_->updatePlcRois(plcRois_);
            connect(worker_.get(), &MeasurementWorker::resultReady, this, &PageHome::showResult);
            connect(worker_.get(), &MeasurementWorker::failed, this, [this](const QString &m)
                    {
                        setStatus(m, false);
                        log(m);
                        if (!pendingPlcCommand_.isEmpty())
                        {
                            plcMeasurementTimeout_->stop();
                            sendPlcPayload("exe_err=Commn err");
                            pendingPlcCommand_.clear();
                        } });
            worker_->start();
        }
        running_ = true;
        activeOnline_ = online;
        updateViewInfo(1);
        updateViewInfo(2);
        if (online || config_.runtime.connectPlcInOffline)
            startPlc();
        if (online)
        {
            emit AppSignals::instance().onlineStageChanged(int(stage_));
            applyStageToUi();
            if (!plcOnly)
                emit AppSignals::instance().onlineCameraStartRequested();
            log(plcOnly ? "Runtime started: PLC test (cameras disabled)" : "Runtime started: online");
        }
        else
        {
            offlineTimer_->start(std::max(50, config_.runtime.loopIntervalMs));
            submitOfflineFrame();
            log("Runtime started: offline");
        }
        refreshControls();
    }
    void PageHome::stopRuntime()
    {
        if (!running_ && !worker_)
            return;
        const bool wasOnline = activeOnline_;
        running_ = false;
        offlineTimer_->stop();
        plcMeasurementTimeout_->stop();
        facetTimeoutTimer_->stop();
        onlineFrames_.clear();
        camera2Requests_.clear();
        awaitingMeasurementCam1_ = false;
        pendingPlcCommand_.clear();
        stopPlc();
        if (wasOnline)
            emit AppSignals::instance().onlineCameraStopRequested();
        if (worker_)
        {
            // 当前帧可能仍在 OpenCV 中计算。不要在 UI 线程等待它结束；
            // 断开页面信号后让线程自行收尾并在 finished 时释放。
            auto *retiringWorker = worker_.release();
            disconnect(retiringWorker, nullptr, this, nullptr);
            retiringWorker->setParent(this);
            connect(retiringWorker, &QThread::finished,
                    retiringWorker, &QObject::deleteLater);
            retiringWorker->stop();
        }
        activeOnline_ = false;
        updateViewInfo(1);
        updateViewInfo(2);
        setConnectionLed(ui_->lblCamera1Status, false);
        setConnectionLed(ui_->lblCamera2Status, false);
        log("Runtime stopped");
        refreshControls();
    }
    void PageHome::toggleOnline(bool online)
    {
        const auto answer = QMessageBox::question(this, "Confirm Mode Change", QString("Switch to %1 mode?").arg(online ? "Online" : "Offline"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            QSignalBlocker blocker(ui_->btnOnline);
            ui_->btnOnline->setChecked(!online);
            refreshControls();
            return;
        }
        stopRuntime();
        ui_->btnOnline->setText(online ? "Online" : "Offline");
        if (online)
            startRuntime(true);
        refreshControls();
    }
    void PageHome::selectStage()
    {
        auto *b = qobject_cast<QPushButton *>(sender());
        const auto oldStage = stage_;
        if (b == ui_->btnStageMelt)
            stage_ = MeasurementStage::Melt;
        else if (b == ui_->btnStageDip)
            stage_ = MeasurementStage::Dip;
        else if (b == ui_->btnStageNeck)
            stage_ = MeasurementStage::Neck;
        else if (b == ui_->btnStageCrown)
            stage_ = MeasurementStage::Crown;
        else if (b == ui_->btnStageBody)
            stage_ = MeasurementStage::Body;
        else if (b == ui_->btnStageEndcone)
            stage_ = MeasurementStage::Endcone;
        else
            stage_ = MeasurementStage::Idle;
        QString stateError;
        if (!persistPlcState(&stateError))
        {
            stage_ = oldStage;
            applyStageToUi();
            log("PLC state save failed: " + stateError);
            return;
        }
        updatePlcOverlayViews();
        if (activeOnline_)
        {
            emit AppSignals::instance().onlineStageChanged(int(stage_));
        }
        log(QString("Offline stage selected: %1").arg(b->text()));
        if (running_ && !activeOnline_)
            QTimer::singleShot(0, this, &PageHome::submitOfflineFrame);
    }
    void PageHome::firstImage() { setImageIndex(0); }
    void PageHome::previousImage() { setImageIndex(imageIndex_ - 1); }
    void PageHome::nextImage() { setImageIndex(imageIndex_ + 1); }
    void PageHome::lastImage() { setImageIndex(imagePaths_.size() - 1); }
    void PageHome::reloadImages(bool preserve)
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
        refreshControls();
    }
    void PageHome::setImageIndex(int index)
    {
        if (imagePaths_.isEmpty())
            return;
        imageIndex_ = std::clamp(index, 0, static_cast<int>(imagePaths_.size()) - 1);
        refreshControls();
        if (running_ && !activeOnline_)
            submitOfflineFrame();
    }
    void PageHome::submitOfflineFrame()
    {
        if (!running_ || !worker_ || imageIndex_ < 0)
            return;
        // QFile + imdecode 与 Python 的 np.fromfile + imdecode 等价，支持中文路径。
        cv::Mat composite = readImage(imagePaths_[imageIndex_]);
        if (composite.empty())
        {
            setStatus(QString("Cannot read offline image: %1").arg(imagePaths_[imageIndex_]), false);
            return;
        }
        if (composite.rows % 2)
        {
            setStatus(QString("Offline composite image height must be even: %1").arg(composite.rows), false);
            return;
        }
        const int middle = composite.rows / 2;
        // 两台相机均使用原始方向；worker 提交时会复制图像数据。
        const cv::Mat camera1 = composite.rowRange(0, middle);
        const cv::Mat camera2 = composite.rowRange(middle, composite.rows);
        setStatus(QString("Calculating: %1").arg(QFileInfo(imagePaths_[imageIndex_]).fileName()), true);
        worker_->submit(camera1, camera2, stage_);
    }
    void PageHome::showResult(const MeasurementResult &r)
    {
        auto overlay1 = r.overlay1;
        auto overlay2 = r.overlay2;
        if (activeOnline_ && config_.camera.autoExposureEnabled)
        {
            addAutoExposureRoi(overlay1, config_.measurement.autoExposureRoiCamera1, r.preview1.size());
            addAutoExposureRoi(overlay2, config_.measurement.autoExposureRoiCamera2, r.preview2.size());
            viewInfo_[0].roiMean = roiMean(r.preview1, config_.measurement.autoExposureRoiCamera1);
            viewInfo_[1].roiMean = roiMean(r.preview2, config_.measurement.autoExposureRoiCamera2);
        }
        lastAlgorithmOverlay1_ = overlay1;
        lastAlgorithmOverlay2_ = overlay2;
        appendPlcRoiOverlays(plcRois_, stage_, config_.measurement.diaRectHeightPx,
                             overlay1, overlay2);
        ui_->cam1GraphicsView->showImage(r.preview1, true);
        ui_->cam2GraphicsView->showImage(r.preview2, true);
        ui_->cam1GraphicsView->updateOverlays(overlay1);
        ui_->cam2GraphicsView->updateOverlays(overlay2);
        ui_->lblDiameter->setText(r.values.diameterMm ? QString::number(*r.values.diameterMm, 'f', 3) : "--");
        const auto cycle = r.diagnostics.find("cycle_ms");
        ui_->lblCycle->setText(cycle == r.diagnostics.end()
                                   ? "Cycle: --"
                                   : QString("Cycle: %1 ms").arg(cycle->second.toDouble(), 0, 'f', 1));
        updateProcessDiagnostics(r);
        setStatus(QString::fromStdString(r.message), r.valid);
        if (!r.valid)
            log(QString::fromStdString(r.message));
        ui_->lblLastFrame->setText("Last frame: " + QDateTime::currentDateTime().toString("HH:mm:ss"));
        if (!pendingPlcCommand_.isEmpty() && r.stage == stage_)
        {
            plcMeasurementTimeout_->stop();
            if (r.valid)
            {
                const QByteArray replyName = pendingPlcCommand_ == "dip_msr" ? "dip" :
                                             pendingPlcCommand_ == "mlt_msr" ? "mlt" : "dia";
                sendPlcPayload(SherlockProtocol::scaledPayload(replyName, r.plcValues));
            }
            else
                sendPlcPayload("exe_err=Commn err");
            pendingPlcCommand_.clear();
        }
        viewInfo_[0].light = r.diagnostics.contains("light_camera1") ? r.diagnostics.at("light_camera1").toDouble() : 0.0;
        viewInfo_[1].light = r.diagnostics.contains("light_camera2") ? r.diagnostics.at("light_camera2").toDouble() : 0.0;
        updateViewInfo(1);
        updateViewInfo(2);
    }
    void PageHome::refreshControls()
    {
        bool offline = !ui_->btnOnline->isChecked();
        ui_->btnStart->setText(running_ ? "Stop" : "Run");
        ui_->btnStart->setProperty("runtimeState", running_ ? "running" : "stopped");
        ui_->btnStart->style()->unpolish(ui_->btnStart);
        ui_->btnStart->style()->polish(ui_->btnStart);
        ui_->btnStart->setEnabled(offline);
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
            b->setEnabled(offline);
        ui_->lblOfflineImage->setText(imageIndex_ >= 0 ? QFileInfo(imagePaths_[imageIndex_]).fileName() : "No image");
        ui_->lblOfflineImage->setToolTip(imageIndex_ >= 0 ? imagePaths_[imageIndex_] : config_.runtime.offlineImageDir);
        ui_->lblImageIndex->setText(imagePaths_.isEmpty() ? "0 / 0" : QString("%1 / %2").arg(imageIndex_ + 1).arg(imagePaths_.size()));
        ui_->btnFirstImage->setEnabled(offline && imageIndex_ > 0);
        ui_->btnPreviousImage->setEnabled(offline && imageIndex_ > 0);
        ui_->btnNextImage->setEnabled(offline && imageIndex_ >= 0 && imageIndex_ < imagePaths_.size() - 1);
        ui_->btnLastImage->setEnabled(ui_->btnNextImage->isEnabled());
    }

    void PageHome::triggerOnlineCapture()
    {
        if (!running_ || !activeOnline_ || pendingPlcCommand_.isEmpty())
            return;
        onlineFrames_.clear();
        awaitingMeasurementCam1_ = true;
        camera2Requests_.enqueue({0, 0, QDateTime::currentMSecsSinceEpoch() + PlcMeasurementTimeoutMs});
        emit AppSignals::instance().onlineCameraTriggerRequested();
    }
    void PageHome::onCameraFrame(const QString &userId, const cv::Mat &image, qint64 timestampNs)
    {
        if (!running_ || !activeOnline_ || !CameraRole::Stereo.contains(userId))
            return;
        if (userId == CameraRole::Cam2)
        {
            if (camera2Requests_.isEmpty())
                return;
            const auto request = camera2Requests_.dequeue();
            if (camera2Requests_.isEmpty())
                facetTimeoutTimer_->stop();
            if (request.facetIndex > 0)
            {
                saveFacette(request.facetIndex, image);
                return;
            }
        }
        else if (!awaitingMeasurementCam1_)
            return;
        if (pendingPlcCommand_.isEmpty() || !worker_)
            return;
        onlineFrames_[userId] = {timestampNs, image.clone()};
        if (!onlineFrames_.contains(CameraRole::Cam1) || !onlineFrames_.contains(CameraRole::Cam2))
            return;
        const auto first = onlineFrames_.value(CameraRole::Cam1);
        const auto second = onlineFrames_.value(CameraRole::Cam2);
        const double deltaMs = std::abs(first.timestampNs - second.timestampNs) / 1.0e6;
        ui_->lblFrameDelta->setText(QString("Frame delta: %1 ms").arg(deltaMs, 0, 'f', 1));
        if (deltaMs > config_.runtime.stereoPairMaxDeltaMs)
        {
            onlineFrames_.clear();
            awaitingMeasurementCam1_ = false;
            const QString message = QString("Online stereo pair dropped: frame delta %1 ms > %2 ms").arg(deltaMs, 0, 'f', 1).arg(config_.runtime.stereoPairMaxDeltaMs);
            setStatus(message, false);
            log(message);
            if (!pendingPlcCommand_.isEmpty())
            {
                plcMeasurementTimeout_->stop();
                sendPlcPayload("exe_err=Acq fails");
                pendingPlcCommand_.clear();
            }
            return;
        }
        onlineFrames_.clear();
        awaitingMeasurementCam1_ = false;
        const auto effective = stage_ == MeasurementStage::Idle ? MeasurementStage::Neck : stage_;
        worker_->submit(first.image, second.image, effective);
    }
    void PageHome::onOnlineCaptureFailed(const QString &userId, const QString &message)
    {
        if (userId == CameraRole::Cam2 && !camera2Requests_.isEmpty())
        {
            const auto request = camera2Requests_.dequeue();
            if (request.facetIndex > 0)
            {
                const QString detail = QString("Facette%1 capture failed: %2")
                                           .arg(request.facetIndex).arg(message);
                if (camera2Requests_.isEmpty())
                    facetTimeoutTimer_->stop();
                log(detail);
                setStatus(detail, false);
                return;
            }
        }
        if (!pendingPlcCommand_.isEmpty())
            onOnlineCameraFailed("Camera " + userId + ": " + message);
        else
            log("Camera " + userId + ": " + message);
    }
    void PageHome::onFacetTriggerFailed(int requestId, const QString &message)
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
    void PageHome::expireFacetRequests()
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
    void PageHome::onCameraExposure(const QString &userId, double exposureUs)
    {
        const qsizetype index = CameraRole::Stereo.indexOf(userId);
        if (index < 0)
            return;
        viewInfo_[index].exposureUs = exposureUs;
        updateViewInfo(int(index + 1));
    }
    void PageHome::onOnlineCameraStarted()
    {
        if (!running_ || !activeOnline_)
            return;
        setConnectionLed(ui_->lblCamera1Status, true);
        setConnectionLed(ui_->lblCamera2Status, true);
        // 连接后重放已持久化的 PLC 曝光命令。
        for (const auto &name : {"exptme1", "exptme2"})
        {
            const auto it = plcParameters_.constFind(name);
            if (it == plcParameters_.cend() || it.value().size() != 1)
                continue;
            emit AppSignals::instance().plcCameraExposureRequested(
                QString::fromLatin1(name) == "exptme1" ? CameraRole::Cam1 : CameraRole::Cam2,
                3000.0 * it.value().front() / 100.0);
        }
        log("Online cameras started; waiting for Sherlock TCP command");
    }
    void PageHome::onOnlineCameraStopped()
    {
        setConnectionLed(ui_->lblCamera1Status, false);
        setConnectionLed(ui_->lblCamera2Status, false);
        log("Online cameras stopped");
    }
    void PageHome::onOnlineCameraFailed(const QString &message)
    {
        log("Online camera failed: " + message);
        if (!pendingPlcCommand_.isEmpty())
        {
            plcMeasurementTimeout_->stop();
            sendPlcPayload("exe_err=Acq fails");
            pendingPlcCommand_.clear();
        }
        stopRuntime();
        {
            QSignalBlocker blocker(ui_->btnOnline);
            ui_->btnOnline->setChecked(false);
            ui_->btnOnline->setText("Offline");
        }
        setStatus("Online camera failed: " + message, false);
        refreshControls();
    }
    void PageHome::setConnectionLed(QLabel *label, bool connected)
    {
        // 与 Python 版本一致：使用资源图片表示连接状态，不给 QLabel 绘制红绿背景。
        label->setStyleSheet({});
        label->setPixmap(QPixmap(connected
                                     ? ":/images/images/images/ledLow.png"
                                     : ":/images/images/images/ledHigh.png"));
    }
    void PageHome::addAutoExposureRoi(std::vector<OverlayElement> &elements, const cv::Rect &roi, const cv::Size &size) const
    {
        const cv::Rect clipped = roi & cv::Rect(0, 0, size.width, size.height);
        if (clipped.empty())
            return;
        elements.push_back({OverlayType::Polyline,
                            {{double(clipped.x), double(clipped.y)}, {double(clipped.x + clipped.width), double(clipped.y)}, {double(clipped.x + clipped.width), double(clipped.y + clipped.height)}, {double(clipped.x), double(clipped.y + clipped.height)}},
                            {255, 0, 0},
                            2,
                            true});
    }
    double PageHome::roiMean(const cv::Mat &image, const cv::Rect &roi)
    {
        const cv::Rect clipped = roi & cv::Rect(0, 0, image.cols, image.rows);
        if (clipped.empty())
            return 0.0;
        cv::Mat gray;
        const cv::Mat selected = image(clipped);
        if (selected.channels() == 1)
            gray = selected;
        else
            cv::cvtColor(selected, gray, selected.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        return cv::mean(gray)[0];
    }
    void PageHome::updateViewInfo(int viewId)
    {
        if (viewId < 1 || viewId > 2)
            return;
        const auto &info = viewInfo_[size_t(viewId - 1)];
        const QString light = info.light ? QString::number(*info.light, 'f', 1) : "--";
        const QString exposure = activeOnline_ && info.exposureUs
                                     ? QString::number(*info.exposureUs / 1000.0, 'f', 2)
                                     : "--";
        const QString mean = activeOnline_ && info.roiMean
                                 ? QString::number(*info.roiMean, 'f', 1)
                                 : "--";
        auto *label = viewId == 1 ? ui_->lblCam1Info : ui_->lblCam2Info;
        label->setText(QString("Pos (%1,%2) | G %3 | Light %4 | Exp %5 ms | Mean %6")
                           .arg(info.x)
                           .arg(info.y)
                           .arg(info.gray)
                           .arg(light, exposure, mean));
    }
    void PageHome::startPlc()
    {
        if (plcServer_)
            return;
        plcServer_ = std::make_unique<SherlockTcpServer>();
        connect(plcServer_.get(), &SherlockTcpServer::connectionChanged, this, [this](bool connected)
                {
                    setConnectionLed(ui_->lblPlcStatus, connected);
                    log(connected ? "PLC connected on Sherlock TCP 5000/5001" : "PLC Sherlock TCP connection incomplete"); });
        connect(plcServer_.get(), &SherlockTcpServer::commandReceived, this, &PageHome::onSherlockCommand);
        connect(plcServer_.get(), &SherlockTcpServer::failed, this, [this](const QString &message)
                { log(message); });
        QString error;
        if (!plcServer_->start(&error))
        {
            log(error);
            setStatus(error, false);
            plcServer_.reset();
            return;
        }
        log("Sherlock TCP compatibility server listening on ports 5000 and 5001");
    }
    void PageHome::stopPlc()
    {
        if (!plcServer_)
            return;
        plcServer_->stop();
        plcServer_.reset();
        plcMeasurementTimeout_->stop();
        pendingPlcCommand_.clear();
        setConnectionLed(ui_->lblPlcStatus, false);
    }

    void PageHome::sendPlcPayload(const QByteArray &payload)
    {
        if (!plcServer_)
            return;
        QString error;
        if (plcServer_->sendPayload(payload, &error))
            log("PLC <- " + QString::fromLatin1(payload));
        else if (!error.isEmpty())
            log(error);
    }

    bool PageHome::handleSherlockSettingCommand(const SherlockCommand &command)
    {
        const QString &name = command.name;
        const bool isModeCommand = name == "thr_rel" || name == "thr_abs";
        const bool isActionCommand = name == "cfit_ne" || name == "pfit_sb";
        if (isModeCommand || isActionCommand)
        {
            if (!command.parameters.isEmpty())
            {
                sendPlcPayload("err_prm=" + name.toLatin1() + " requires no parameters");
                return true;
            }
            const bool oldRelative = plcRelativeThreshold_;
            const bool oldPointFit = pointFitSelected_;
            const auto oldStage = stage_;
            if (isModeCommand)
                plcRelativeThreshold_ = name == "thr_rel";
            else if (name == "cfit_ne" || name == "pfit_sb")
            {
                pointFitSelected_ = name == "pfit_sb";
                stage_ = stageForPlcCommand(name, stage_, pointFitSelected_, plcRois_,
                                            config_.measurement.crownBodyInnerRadiusPx);
            }
            QString stateError;
            if (!persistPlcState(&stateError))
            {
                plcRelativeThreshold_ = oldRelative;
                pointFitSelected_ = oldPointFit;
                stage_ = oldStage;
                sendPlcPayload("err_exc=state save failed");
                log("PLC state save failed: " + stateError);
                return true;
            }
            if (stage_ != oldStage)
            {
                applyStageToUi();
                updatePlcOverlayViews();
                emit AppSignals::instance().onlineStageChanged(int(stage_));
            }
            if (isModeCommand)
                log(QString("PLC diameter threshold mode: %1")
                        .arg(plcRelativeThreshold_ ? "relative" : "absolute"));
            sendPlcPayload(name.toLatin1() + "=ok");
            return true;
        }

        int expectedCount = -1;
        if (name == "mlt_crd")
            expectedCount = 6;
        else if (name == "dia_crd")
            expectedCount = 8;
        else if (name == "rec_crd")
            expectedCount = 4;
        else if (name == "dip_crd" || name == "vib_crd")
            expectedCount = 3;
        else if (name == "mlt_thr" || name == "dip_thr" || name == "dia_thr" ||
                 name == "diathr2" || name == "rec_thr" || name == "vib_thr" ||
                 name == "exptme1" || name == "exptme2")
            expectedCount = 1;
        if (expectedCount < 0)
            return false;

        QString error;
        const auto values = SherlockProtocol::parseScaledParameters(command, expectedCount, &error);
        if (!values)
        {
            sendPlcPayload("err_prm=" + error.toLatin1());
            return true;
        }
        if ((name == "exptme1" || name == "exptme2") && values->front() <= 0.0)
        {
            sendPlcPayload("err_prm=exposure must be positive");
            return true;
        }
        const auto oldParameters = plcParameters_;
        const auto oldRois = plcRois_;
        const auto oldStage = stage_;
        if (name == "dia_crd" || name == "mlt_crd" || name == "dip_crd")
        {
            if (!setPlcRoi(plcRois_, name, *values, &error))
            {
                sendPlcPayload("err_prm=" + error.toLatin1());
                return true;
            }
        }
        plcParameters_.insert(name, *values);
        if (name == "dia_crd" || name == "mlt_crd" || name == "dip_crd")
            stage_ = stageForPlcCommand(name, stage_, pointFitSelected_, plcRois_,
                                        config_.measurement.crownBodyInnerRadiusPx);
        if (!persistPlcState(&error))
        {
            plcParameters_ = oldParameters;
            plcRois_ = oldRois;
            stage_ = oldStage;
            sendPlcPayload("err_exc=state save failed");
            log("PLC state save failed: " + error);
            return true;
        }

        if (name == "dia_thr" || name == "diathr2")
        {
            // PLC 百分比恢复到旧 Sherlock 的 0..255 灰度阈值。
            applyPlcConfigOverrides();
            if (worker_)
                worker_->updateConfig(config_);
        }
        else if (name == "exptme1" || name == "exptme2")
        {
            // 与Sherlock 4.14一致：PLC值为3000 us基准曝光的百分比。
            const double exposureUs = 3000.0 * values->front() / 100.0;
            const bool first = name == "exptme1";
            if (first)
                config_.camera.initialExposureCamera1 = exposureUs;
            else
                config_.camera.initialExposureCamera2 = exposureUs;
            emit AppSignals::instance().plcCameraExposureRequested(
                first ? CameraRole::Cam1 : CameraRole::Cam2, exposureUs);
        }
        if (name == "dia_crd" || name == "mlt_crd" || name == "dip_crd")
        {
            if (worker_)
                worker_->updatePlcRois(plcRois_);
            applyStageToUi();
            updatePlcOverlayViews();
            if (stage_ != oldStage)
                emit AppSignals::instance().onlineStageChanged(int(stage_));
        }

        log(QString("PLC setting %1 accepted (%2 parameter(s))").arg(name).arg(expectedCount));
        sendPlcPayload(name.toLatin1() + "=ok");
        return true;
    }

    void PageHome::onSherlockCommand(const SherlockCommand &command)
    {
        const QString name = command.name;
        qDebug().noquote()
            << "PLC raw size =" << command.raw.size()
            << "raw ASCII =" << QString::fromLatin1(command.raw);
            // << "name size =" << name.size()
            // << "name ASCII =" << name;

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
            if (activeOnline_ && running_)
            {
                const int requestId = nextFacetRequestId_++;
                camera2Requests_.enqueue(
                    {requestId, facetIndex,
                     QDateTime::currentMSecsSinceEpoch() + PlcMeasurementTimeoutMs});
                facetTimeoutTimer_->start();
                emit AppSignals::instance().onlineFacetTriggerRequested(requestId);
            }
            else if (!ui_->btnOnline->isChecked())
                captureOfflineFacette(facetIndex);
            else
                log(QString("Facette%1 capture failed: Camera 2 is not online").arg(facetIndex));
            return;
        }

        if (name == "acq_on_")
        {
            const bool previous = acquisitionEnabled_;
            acquisitionEnabled_ = true;
            QString error;
            if (!persistPlcState(&error))
            {
                acquisitionEnabled_ = previous;
                sendPlcPayload("err_exc=state save failed");
                log("PLC state save failed: " + error);
                return;
            }
            sendPlcPayload("acq_on_=ok");
            return;
        }
        if (name == "acq_off")
        {
            const bool previous = acquisitionEnabled_;
            acquisitionEnabled_ = false;
            QString error;
            if (!persistPlcState(&error))
            {
                acquisitionEnabled_ = previous;
                sendPlcPayload("err_exc=state save failed");
                log("PLC state save failed: " + error);
                return;
            }
            sendPlcPayload("acq_off=ok");
            return;
        }
        if (name == "acq_get")
        {
            sendPlcPayload("acq_get=" + QByteArray::number(acquisitionEnabled_ ? 1 : 0));
            return;
        }
        if (name == "ver_get")
        {
            sendPlcPayload("ver_get=414");
            return;
        }
        if (name == "rfr_get")
        {
            sendPlcPayload(SherlockProtocol::scaledPayload("rfr_get", {plcRefreshRate_}));
            return;
        }
        if (name == "rfr_set")
        {
            if (command.parameters.size() != 1)
            {
                sendPlcPayload("err_prm=rfr_set requires one parameter");
                return;
            }
            const auto value = SherlockProtocol::fromPlcNumber(command.parameters.front());
            if (!value || *value <= 0.0)
            {
                sendPlcPayload("err_prm=invalid refresh rate");
                return;
            }
            const double previous = plcRefreshRate_;
            plcRefreshRate_ = *value;
            QString error;
            if (!persistPlcState(&error))
            {
                plcRefreshRate_ = previous;
                sendPlcPayload("err_exc=state save failed");
                log("PLC state save failed: " + error);
                return;
            }
            sendPlcPayload("rfr_set=ok");
            return;
        }
        if (handleSherlockSettingCommand(command))
            return;

        if (name != "dia_msr" && name != "dia_rec" && name != "dip_msr" && name != "mlt_msr")
        {
            sendPlcPayload("err_unk=" + name.toLatin1());
            return;
        }
        if (!acquisitionEnabled_ || !running_ /*|| !activeOnline_*/ || !worker_)
        {
            sendPlcPayload("err_lck=Measurement are disabled");
            return;
        }
        if (!pendingPlcCommand_.isEmpty())
        {
            sendPlcPayload("exe_err=measurement busy");
            return;
        }

        const auto oldStage = stage_;
        stage_ = stageForPlcCommand(name, stage_, pointFitSelected_, plcRois_,
                                    config_.measurement.crownBodyInnerRadiusPx);
        QString stateError;
        if (!persistPlcState(&stateError))
        {
            stage_ = oldStage;
            sendPlcPayload("err_exc=state save failed");
            log("PLC state save failed: " + stateError);
            return;
        }
        pendingPlcCommand_ = name;
        plcMeasurementTimeout_->start(PlcMeasurementTimeoutMs);
        applyStageToUi();
        updatePlcOverlayViews();
        emit AppSignals::instance().onlineStageChanged(int(stage_));
        triggerOnlineCapture();
    }
    void PageHome::applyStageToUi()
    {
        QPushButton *selected = ui_->btnStageIdle;
        switch (stage_)
        {
        case MeasurementStage::Melt:
            selected = ui_->btnStageMelt;
            break;
        case MeasurementStage::Dip:
            selected = ui_->btnStageDip;
            break;
        case MeasurementStage::Neck:
            selected = ui_->btnStageNeck;
            break;
        case MeasurementStage::Crown:
            selected = ui_->btnStageCrown;
            break;
        case MeasurementStage::Body:
            selected = ui_->btnStageBody;
            break;
        case MeasurementStage::Endcone:
            selected = ui_->btnStageEndcone;
            break;
        default:
            break;
        }
        QSignalBlocker blocker(selected);
        selected->setChecked(true);
    }

    QString PageHome::plcStatePath() const
    {
        return QFileInfo(config_.runtime.stateFile).absoluteDir().filePath("plc_runtime_state.json");
    }

    bool PageHome::persistPlcState(QString *error) const
    {
        PlcRuntimeState snapshot;
        snapshot.stage = stage_;
        snapshot.pointFitSelected = pointFitSelected_;
        snapshot.acquisitionEnabled = acquisitionEnabled_;
        snapshot.relativeThreshold = plcRelativeThreshold_;
        snapshot.refreshRate = plcRefreshRate_;
        snapshot.parameters = plcParameters_;
        return PlcRuntimeStore(plcStatePath()).save(snapshot, error);
    }

    void PageHome::applyPlcConfigOverrides()
    {
        const auto threshold = [this](const QString &name) -> std::optional<double>
        {
            const auto it = plcParameters_.constFind(name);
            if (it == plcParameters_.cend() || it.value().size() != 1)
                return {};
            return it.value().front();
        };
        if (const auto value = threshold("dia_thr"))
            config_.neck.gradientThresholdCamera1 = std::clamp(*value * 255.0 / 100.0, 0.0, 255.0);
        if (const auto value = threshold("diathr2"))
            config_.neck.gradientThresholdCamera2 = std::clamp(*value * 255.0 / 100.0, 0.0, 255.0);
        if (const auto value = threshold("exptme1"); value && *value > 0.0)
            config_.camera.initialExposureCamera1 = 3000.0 * *value / 100.0;
        if (const auto value = threshold("exptme2"); value && *value > 0.0)
            config_.camera.initialExposureCamera2 = 3000.0 * *value / 100.0;
    }

    void PageHome::restorePlcState()
    {
        PlcRuntimeState saved;
        QString error;
        if (!PlcRuntimeStore(plcStatePath()).load(&saved, &error))
        {
            log("PLC state ignored: " + error);
            return;
        }
        stage_ = saved.stage;
        pointFitSelected_ = saved.pointFitSelected;
        acquisitionEnabled_ = saved.acquisitionEnabled;
        plcRelativeThreshold_ = saved.relativeThreshold;
        plcRefreshRate_ = saved.refreshRate;
        plcParameters_ = std::move(saved.parameters);
        for (const QString name : {"dia_crd", "mlt_crd", "dip_crd"})
        {
            const auto it = plcParameters_.constFind(name);
            if (it == plcParameters_.cend())
                continue;
            if (!setPlcRoi(plcRois_, name, it.value(), &error))
            {
                plcParameters_.remove(name);
                log("Saved PLC ROI ignored: " + error);
            }
        }
        applyPlcConfigOverrides();
    }

    void PageHome::updatePlcOverlayViews()
    {
        auto first = lastAlgorithmOverlay1_;
        auto second = lastAlgorithmOverlay2_;
        appendPlcRoiOverlays(plcRois_, stage_, config_.measurement.diaRectHeightPx,
                             first, second);
        ui_->cam1GraphicsView->updateOverlays(first);
        ui_->cam2GraphicsView->updateOverlays(second);
    }

    void PageHome::log(const QString &message)
    {
        dailyLog_.setDirectory(QFileInfo(config_.runtime.stateFile)
                                   .absoluteDir().filePath("Log"));
        const auto result = dailyLog_.append(QDateTime::currentDateTime(), message);
        if (!result.written)
            qWarning() << "Failed to write measurement log:" << result.error;
        if (result.merged)
        {
            QTextCursor cursor = ui_->txtLog->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.select(QTextCursor::LineUnderCursor);
            cursor.insertText(result.line);
        }
        else
            ui_->txtLog->appendPlainText(result.line);
    }
    void PageHome::setStatus(const QString &message, bool ok)
    {
        emit AppSignals::instance().status(
            "Measurement", ok ? "OK" : "NG", ok ? "info" : "error", message);
    }
    cv::Mat PageHome::readImage(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray encoded = file.readAll();
        if (encoded.isEmpty())
            return {};
        const cv::Mat buffer(1, encoded.size(), CV_8U, const_cast<char *>(encoded.constData()));
        return cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
    }
    void PageHome::loadFacette(int index)
    {
        CustomGraphicsView *views[] = {ui_->facetView1, ui_->facetView2,
                                       ui_->facetView3, ui_->facetView4};
        if (index < 1 || index > 4)
            return;
        const QString filename = QString("Facette%1.bmp").arg(index);
        const cv::Mat image = readImage(QDir(config_.runtime.facetteImageDir).filePath(filename));
        if (image.empty())
            views[index - 1]->setText(filename);
        else
            views[index - 1]->showImage(image);
    }
    void PageHome::saveFacette(int index, const cv::Mat &image)
    {
        const QString filename = QString("Facette%1.bmp").arg(index);
        const QString path = QDir(config_.runtime.facetteImageDir).filePath(filename);
        QString error;
        if (image.empty())
            error = "Camera 2 image is empty";
        cv::Mat gray;
        if (error.isEmpty())
        {
            if (image.channels() == 1)
                gray = image;
            else if (image.channels() == 3 || image.channels() == 4)
                cv::cvtColor(image, gray, image.channels() == 3 ? cv::COLOR_BGR2GRAY
                                                                  : cv::COLOR_BGRA2GRAY);
            else
                error = "Unsupported Camera 2 image format";
        }
        if (error.isEmpty() && gray.depth() != CV_8U)
        {
            cv::Mat converted;
            gray.convertTo(converted, CV_8U, gray.depth() == CV_16U ? 1.0 / 256.0 : 1.0);
            gray = converted;
        }
        std::vector<uchar> encoded;
        if (error.isEmpty())
        {
            try
            {
                if (!cv::imencode(".bmp", gray, encoded) || encoded.empty())
                    error = "BMP encoding failed";
            }
            catch (const cv::Exception &exception)
            {
                error = QString::fromLocal8Bit(exception.what());
            }
        }
        if (error.isEmpty() && !QDir().mkpath(config_.runtime.facetteImageDir))
            error = "Cannot create Facette directory";
        if (error.isEmpty())
        {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(reinterpret_cast<const char *>(encoded.data()),
                           qint64(encoded.size())) != qint64(encoded.size()) ||
                !file.commit())
                error = file.errorString();
        }
        if (!error.isEmpty())
        {
            const QString detail = filename + " save failed: " + error;
            log(detail);
            setStatus(detail, false);
            return;
        }
        loadFacette(index);
        log(filename + " saved to " + path);
    }
    void PageHome::captureOfflineFacette(int index)
    {
        if (imageIndex_ < 0 || imageIndex_ >= imagePaths_.size())
        {
            const QString detail = QString("Facette%1 offline capture failed: no selected image").arg(index);
            log(detail);
            setStatus(detail, false);
            return;
        }
        const cv::Mat composite = readImage(imagePaths_[imageIndex_]);
        if (composite.empty() || composite.rows % 2 != 0)
        {
            const QString detail = QString("Facette%1 offline capture failed: invalid stereo image").arg(index);
            log(detail);
            setStatus(detail, false);
            return;
        }
        saveFacette(index, composite.rowRange(composite.rows / 2, composite.rows));
    }
    void PageHome::updateProcessDiagnostics(const MeasurementResult &result)
    {
        ui_->treeProcess->clear();
        auto *camera1 = new QTreeWidgetItem(ui_->treeProcess, {"Camera 1"});
        auto *camera2 = new QTreeWidgetItem(ui_->treeProcess, {"Camera 2"});
        auto *algorithm = new QTreeWidgetItem(ui_->treeProcess, {"Algorithm"});
        auto *runtime = new QTreeWidgetItem(ui_->treeProcess, {"Runtime"});
        const auto highlightGroup = [this](QTreeWidgetItem *group)
        {
            QFont font = group->font(0);
            font.setBold(true);
            for (int column = 0; column < ui_->treeProcess->columnCount(); ++column)
            {
                group->setBackground(column, QBrush(QColor(98, 114, 164)));
                group->setForeground(column, QBrush(Qt::white));
                group->setFont(column, font);
            }
        };
        // 四个顶层分组使用主题强调色，和普通诊断项形成清晰分隔。
        for (auto *group : {camera1, camera2, algorithm, runtime})
            highlightGroup(group);
        camera1->setExpanded(true);
        camera2->setExpanded(true);
        algorithm->setExpanded(true);
        runtime->setExpanded(true);

        const auto formattedValue = [](const QVariant &value)
        {
            if (!value.isValid() || value.isNull())
                return QString("NA");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const int valueType = value.metaType().id();
#else
            const int valueType = value.userType();
#endif
            if (valueType == QMetaType::Bool)
                return value.toBool() ? QString("Yes") : QString("No");
            if (valueType == QMetaType::Int || valueType == QMetaType::UInt ||
                valueType == QMetaType::LongLong || valueType == QMetaType::ULongLong)
                return QString::number(value.toLongLong());
            if (valueType == QMetaType::QVariantList)
            {
                QStringList values;
                for (const auto &entry : value.toList())
                    values.append(QString::number(entry.toDouble(), 'f', 3));
                return "[" + values.join(", ") + "]";
            }
            bool numeric = false;
            const double number = value.toDouble(&numeric);
            return numeric ? QString::number(number, 'f', 3) : value.toString();
        };
        const auto diagnosticLabel = [&result](const std::string &key, QString &unit)
        {
            QString normalized = QString::fromStdString(key);
            for (const auto &[suffix, suffixUnit] : {std::pair<QString, QString>{"_mm", "mm"},
                                                     {"_px", "px"},
                                                     {"_ms", "ms"},
                                                     {"_deg", "deg"}})
                if (normalized.endsWith(suffix))
                {
                    normalized.chop(suffix.size());
                    unit = suffixUnit;
                    break;
                }
            normalized.replace("_camera1", "");
            normalized.replace("_camera2", "");
            QStringList words = normalized.split('_', Qt::SkipEmptyParts);
            for (QString &word : words)
            {
                word = word.toLower();
                if (!word.isEmpty())
                    word[0] = word[0].toUpper();
            }
            QString label = words.join(' ');
            for (const auto &[source, replacement] : {
                     std::pair<QString, QString>{"Column Maximum Maximum", "Col Peak Max"},
                     {"Column Maximum P90", "Col Peak P90"},
                     {"Column Strengths Maximum", "Col Strength Max"},
                     {"Column Strengths P90", "Col Strength P90"},
                     {"Left Side Candidate Count", "Left Cand Count"},
                     {"Right Side Candidate Count", "Right Cand Count"},
                     {"Search Bottom Ratio", "Search Y1 Ratio"},
                     {"Search Top Ratio", "Search Y0 Ratio"},
                     {"Search Start Y", "Search Y0"},
                     {"Search Stop Y", "Search Y1"}})
                label.replace(source, replacement);
            static const QHash<QString, QString> abbreviations{
                {"Boundaries", "Bounds"}, {"Boundary", "Bound"}, {"Brightness", "Bright"}, {"Candidate", "Cand"}, {"Candidates", "Cands"}, {"Column", "Col"}, {"Columns", "Cols"}, {"Height", "H"}, {"Horizontal", "Horiz"}, {"Image", "Img"}, {"Maximum", "Max"}, {"Minimum", "Min"}, {"Point", "Pt"}, {"Points", "Pts"}, {"Previous", "Prev"}, {"Residual", "Resid"}, {"Strengths", "Strength"}, {"Threshold", "Thresh"}, {"Tracking", "Track"}, {"Vertical", "Vert"}};
            words = label.split(' ', Qt::SkipEmptyParts);
            for (QString &word : words)
                word = abbreviations.value(word, word);
            label = words.join(' ');
            if (result.stage == MeasurementStage::Crown && label.startsWith("Crown "))
                label.remove(0, 6);
            else if (result.stage == MeasurementStage::Body && label.startsWith("Body "))
                label.remove(0, 5);
            return label;
        };

        for (const auto &[key, value] : result.diagnostics)
        {
            QTreeWidgetItem *group = algorithm;
            if (key.find("camera1") != std::string::npos)
                group = camera1;
            else if (key.find("camera2") != std::string::npos)
                group = camera2;
            else if (key.find("cycle_") != std::string::npos || key == "source" ||
                     key.find("tracking_active") != std::string::npos)
                group = runtime;

            QString unit;
            const QString label = diagnosticLabel(key, unit);
            auto *item = new QTreeWidgetItem(group, {label, formattedValue(value), unit});
            item->setToolTip(0, QString::fromStdString(key));
            item->setToolTip(1, formattedValue(value));
        }

        // 只按显示名称排序各分组的子项，保持四个顶层分组的固定顺序。
        camera1->sortChildren(0, Qt::AscendingOrder);
        camera2->sortChildren(0, Qt::AscendingOrder);
        algorithm->sortChildren(0, Qt::AscendingOrder);
        runtime->sortChildren(0, Qt::AscendingOrder);
    }
}
