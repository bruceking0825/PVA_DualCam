#include "pva/page_home.hpp"
#include "pva/state_store.hpp"
#include "pva/app_signals.hpp"
#include "pva/config_manager.hpp"
#include "pva/opcua_worker.hpp"
#include "ui_PageHome.h"
#include <QButtonGroup>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace pva
{
    PageHome::PageHome(MeasurementConfig config, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageHome>()), config_(std::move(config))
    {
        initializePage([this] { ui_->setupUi(this); });
    }

    void PageHome::initializeState()
    {
        offlineTimer_ = new QTimer(this);
        onlineTimer_ = new QTimer(this);
        onlineClock_.start();
        stage_ = MeasurementStage::Neck;
    }

    void PageHome::setupPageUi()
    {
        ui_->cam1GraphicsView->setViewId(1);
        ui_->cam2GraphicsView->setViewId(2);
        ui_->cam1GraphicsView->setText("Camera 1");
        ui_->cam2GraphicsView->setText("Camera 2");
        ui_->viewSplitter->setSizes({520, 520});
        ui_->mainSplitter->setSizes({960, 320});
        auto *group = new QButtonGroup(this);
        group->setExclusive(true);
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
            group->addButton(b);
        ui_->btnStageNeck->setChecked(true);
    }

    void PageHome::bindEvents()
    {
        connect(offlineTimer_, &QTimer::timeout, this, &PageHome::submitOfflineFrame);
        connect(onlineTimer_, &QTimer::timeout, this, &PageHome::triggerOnlineCapture);
        connect(ui_->btnStart, &QPushButton::clicked, this, &PageHome::toggleRuntime);
        connect(ui_->btnOnline, &QPushButton::toggled, this, &PageHome::toggleOnline);
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
            connect(b, &QPushButton::clicked, this, &PageHome::selectStage);
        connect(ui_->btnFirstImage, &QPushButton::clicked, this, &PageHome::firstImage);
        connect(ui_->btnPreviousImage, &QPushButton::clicked, this, &PageHome::previousImage);
        connect(ui_->btnNextImage, &QPushButton::clicked, this, &PageHome::nextImage);
        connect(ui_->btnLastImage, &QPushButton::clicked, this, &PageHome::lastImage);
        connect(ui_->cam1GraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                {
                    viewInfo_[0].x = x; viewInfo_[0].y = y; viewInfo_[0].gray = gray;
                    updateViewInfo(1);
                });
        connect(ui_->cam2GraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                {
                    viewInfo_[1].x = x; viewInfo_[1].y = y; viewInfo_[1].gray = gray;
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
        connect(&appSignals, &AppSignals::appClose, this, &PageHome::stopRuntime);
        connect(&ConfigManager::instance(), &ConfigManager::batchChanged, this, [this]
                { reloadConfig(ConfigManager::instance().config()); });
    }

    void PageHome::onReady()
    {
        updateViewInfo(1);
        updateViewInfo(2);
        // Python 离线启动时由 offline_mode="neck" 进入 Neck，而不是停留在 Idle。
        stage_ = MeasurementStage::Neck;
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
        const bool restart = running_;
        const bool online = activeOnline_;
        if (restart)
            stopRuntime();
        config_ = config;
        reloadImages(true);
        if (restart)
            QTimer::singleShot(0, this, [this, online] { startRuntime(online); });
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
        if (running_) return;
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
            connect(worker_.get(), &MeasurementWorker::resultReady, this, &PageHome::showResult);
            connect(worker_.get(), &MeasurementWorker::failed, this, [this](const QString &m)
                    {
                        setStatus(m, false);
                        log(m);
                    });
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
            stage_ = MeasurementStage::Idle;
            emit AppSignals::instance().onlineStageChanged(int(stage_));
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
        if (!running_ && !worker_) return;
        const bool wasOnline = activeOnline_;
        running_ = false;
        offlineTimer_->stop();
        onlineTimer_->stop();
        onlineFrames_.clear();
        stopPlc();
        if (wasOnline) emit AppSignals::instance().onlineCameraStopRequested();
        if (worker_) { worker_->stop(); worker_->wait(); worker_.reset(); }
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
        if (online) startRuntime(true);
        refreshControls();
    }
    void PageHome::selectStage()
    {
        auto *b = qobject_cast<QPushButton *>(sender());
        if (b == ui_->btnStageNeck)
            stage_ = MeasurementStage::Neck;
        else if (b == ui_->btnStageCrown)
            stage_ = MeasurementStage::Crown;
        else if (b == ui_->btnStageBody)
            stage_ = MeasurementStage::Body;
        else if (b == ui_->btnStageEndcone)
            stage_ = MeasurementStage::Endcone;
        else
            stage_ = MeasurementStage::Idle;
        if (activeOnline_)
        {
            emit AppSignals::instance().onlineStageChanged(int(stage_));
            if (onlineTimer_->isActive()) onlineTimer_->start(onlineSampleInterval());
        }
        log(QString("Offline stage selected: %1").arg(b->text()));
        if (running_)
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
        if (running_)
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
        if (composite.cols % 2)
        {
            setStatus(QString("Offline composite image width must be even: %1").arg(composite.cols), false);
            return;
        }
        int middle = composite.cols / 2;
        setStatus(QString("Calculating: %1").arg(QFileInfo(imagePaths_[imageIndex_]).fileName()), true);
        worker_->submit(composite.colRange(0, middle), composite.colRange(middle, composite.cols), stage_);
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
        if (r.valid && r.values.diameterMm && plcWorker_)
            plcWorker_->queueDiameter(*r.values.diameterMm);
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
        for (auto *b : {ui_->btnStageIdle, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody, ui_->btnStageEndcone})
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
        if (!running_ || !activeOnline_) return;
        onlineFrames_.clear();
        emit AppSignals::instance().onlineCameraTriggerRequested();
    }
    void PageHome::onCameraFrame(const QString &role, const cv::Mat &image)
    {
        if (!running_ || !activeOnline_ || !worker_ || (role != "1" && role != "2")) return;
        onlineFrames_[role] = {onlineClock_.nsecsElapsed(), image.clone()};
        if (!onlineFrames_.contains("1") || !onlineFrames_.contains("2")) return;
        const auto first = onlineFrames_.value("1"), second = onlineFrames_.value("2");
        const double deltaMs = std::abs(first.timestampNs - second.timestampNs) / 1.0e6;
        ui_->lblFrameDelta->setText(QString("Frame delta: %1 ms").arg(deltaMs, 0, 'f', 1));
        if (deltaMs > config_.runtime.stereoPairMaxDeltaMs)
        {
            onlineFrames_.remove(first.timestampNs < second.timestampNs ? "1" : "2");
            const QString message = QString("Online stereo pair dropped: frame delta %1 ms > %2 ms").arg(deltaMs, 0, 'f', 1).arg(config_.runtime.stereoPairMaxDeltaMs);
            setStatus(message, false); log(message); return;
        }
        onlineFrames_.clear();
        const auto effective = stage_ == MeasurementStage::Idle ? MeasurementStage::Neck : stage_;
        worker_->submit(first.image, second.image, effective);
    }
    void PageHome::onCameraExposure(const QString &role, double exposureUs)
    {
        if (role == "1") { viewInfo_[0].exposureUs = exposureUs; updateViewInfo(1); }
        else if (role == "2") { viewInfo_[1].exposureUs = exposureUs; updateViewInfo(2); }
    }
    void PageHome::onOnlineCameraStarted()
    {
        if (!running_ || !activeOnline_) return;
        setConnectionLed(ui_->lblCamera1Status, true);
        setConnectionLed(ui_->lblCamera2Status, true);
        onlineTimer_->start(onlineSampleInterval());
        triggerOnlineCapture();
        log("Online cameras started");
    }
    void PageHome::onOnlineCameraStopped()
    {
        onlineTimer_->stop();
        setConnectionLed(ui_->lblCamera1Status, false);
        setConnectionLed(ui_->lblCamera2Status, false);
        log("Online cameras stopped");
    }
    void PageHome::onOnlineCameraFailed(const QString &message)
    {
        log("Online camera failed: " + message);
        stopRuntime();
        { QSignalBlocker blocker(ui_->btnOnline); ui_->btnOnline->setChecked(false); ui_->btnOnline->setText("Offline"); }
        setStatus("Online camera failed: " + message, false);
        refreshControls();
    }
    int PageHome::onlineSampleInterval() const
    {
        switch (stage_)
        {
        case MeasurementStage::Neck: return config_.runtime.neckSampleIntervalMs;
        case MeasurementStage::Crown: return config_.runtime.crownSampleIntervalMs;
        case MeasurementStage::Body: return config_.runtime.bodySampleIntervalMs;
        case MeasurementStage::Endcone: return config_.runtime.endconeSampleIntervalMs;
        default: return config_.runtime.idleSampleIntervalMs;
        }
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
        if (clipped.empty()) return;
        elements.push_back({OverlayType::Polyline,
                            {{double(clipped.x), double(clipped.y)}, {double(clipped.x + clipped.width), double(clipped.y)},
                             {double(clipped.x + clipped.width), double(clipped.y + clipped.height)}, {double(clipped.x), double(clipped.y + clipped.height)}},
                            {255, 0, 0}, 2, true});
    }
    double PageHome::roiMean(const cv::Mat &image, const cv::Rect &roi)
    {
        const cv::Rect clipped = roi & cv::Rect(0, 0, image.cols, image.rows);
        if (clipped.empty()) return 0.0;
        cv::Mat gray;
        const cv::Mat selected = image(clipped);
        if (selected.channels() == 1) gray = selected;
        else cv::cvtColor(selected, gray, selected.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        return cv::mean(gray)[0];
    }
    void PageHome::updateViewInfo(int viewId)
    {
        if (viewId < 1 || viewId > 2) return;
        const auto &info = viewInfo_[size_t(viewId - 1)];
        const QString light = info.light ? QString::number(*info.light, 'f', 1) : "--";
        const QString exposure = activeOnline_ && info.exposureUs
                                     ? QString::number(*info.exposureUs / 1000.0, 'f', 2) : "--";
        const QString mean = activeOnline_ && info.roiMean
                                 ? QString::number(*info.roiMean, 'f', 1) : "--";
        auto *label = viewId == 1 ? ui_->lblCam1Info : ui_->lblCam2Info;
        label->setText(QString("Pos (%1,%2) | G %3 | Light %4 | Exp %5 ms | Mean %6")
                           .arg(info.x).arg(info.y).arg(info.gray).arg(light, exposure, mean));
    }
    void PageHome::startPlc()
    {
        if (plcWorker_) return;
        plcWorker_ = std::make_unique<OpcUaWorker>();
        connect(plcWorker_.get(), &OpcUaWorker::connectionChanged, this, [this](bool connected)
                {
                    setConnectionLed(ui_->lblPlcStatus, connected);
                    log(connected ? "PLC connected" : "PLC disconnected");
                });
        connect(plcWorker_.get(), &OpcUaWorker::controlsChanged, this, &PageHome::onPlcControls);
        connect(plcWorker_.get(), &OpcUaWorker::failed, this, [this](const QString &message) { log(message); });
        plcWorker_->start();
    }
    void PageHome::stopPlc()
    {
        if (!plcWorker_) return;
        plcWorker_->stop();
        plcWorker_->wait();
        plcWorker_.reset();
        setConnectionLed(ui_->lblPlcStatus, false);
    }
    void PageHome::onPlcControls(int stageValue, bool shoulderTransition)
    {
        if (!activeOnline_) return;
        MeasurementStage next = MeasurementStage::Idle;
        switch (stageValue)
        {
        case 1: next = MeasurementStage::Neck; break;
        case 2: next = shoulderTransition ? MeasurementStage::Body : MeasurementStage::Crown; break;
        case 3: next = MeasurementStage::Endcone; break;
        case 4: next = MeasurementStage::Body; break;
        default: break;
        }
        if (next == stage_) return;
        stage_ = next;
        applyStageToUi();
        emit AppSignals::instance().onlineStageChanged(int(stage_));
        if (onlineTimer_->isActive()) onlineTimer_->start(onlineSampleInterval());
        log(QString("PLC stage changed: %1").arg(stageValue));
    }
    void PageHome::applyStageToUi()
    {
        QPushButton *selected = ui_->btnStageIdle;
        switch (stage_)
        {
        case MeasurementStage::Neck: selected = ui_->btnStageNeck; break;
        case MeasurementStage::Crown: selected = ui_->btnStageCrown; break;
        case MeasurementStage::Body: selected = ui_->btnStageBody; break;
        case MeasurementStage::Endcone: selected = ui_->btnStageEndcone; break;
        default: break;
        }
        QSignalBlocker blocker(selected);
        selected->setChecked(true);
    }
    void PageHome::log(const QString &m) { ui_->txtLog->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss ") + m); }
    void PageHome::setStatus(const QString &message, bool ok)
    {
        ui_->lblStatus->setText(message);
        ui_->lblStatus->setToolTip(message);
        ui_->lblStatus->setStyleSheet(QString("background-color: %1; color: black; border-radius: 4px; padding: 4px 6px;")
                                         .arg(ok ? "rgb(42, 170, 80)" : "rgb(220, 70, 70)"));
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
    void PageHome::updateProcessDiagnostics(const MeasurementResult &result)
    {
        ui_->treeProcess->clear();
        auto *camera1 = new QTreeWidgetItem(ui_->treeProcess, {"Camera 1"});
        auto *camera2 = new QTreeWidgetItem(ui_->treeProcess, {"Camera 2"});
        auto *algorithm = new QTreeWidgetItem(ui_->treeProcess, {"Algorithm"});
        auto *runtime = new QTreeWidgetItem(ui_->treeProcess, {"Runtime"});
        camera1->setExpanded(true);
        camera2->setExpanded(true);
        algorithm->setExpanded(true);
        runtime->setExpanded(true);

        const auto formattedValue = [](const QVariant &value)
        {
            if (!value.isValid() || value.isNull()) return QString("NA");
            if (value.metaType().id() == QMetaType::Bool)
                return value.toBool() ? QString("Yes") : QString("No");
            if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::UInt ||
                value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::ULongLong)
                return QString::number(value.toLongLong());
            if (value.metaType().id() == QMetaType::QVariantList)
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
                                                     {"_px", "px"}, {"_ms", "ms"}, {"_deg", "deg"}})
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
                if (!word.isEmpty()) word[0] = word[0].toUpper();
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
                {"Boundaries", "Bounds"}, {"Boundary", "Bound"}, {"Brightness", "Bright"},
                {"Candidate", "Cand"}, {"Candidates", "Cands"}, {"Column", "Col"},
                {"Columns", "Cols"}, {"Height", "H"}, {"Horizontal", "Horiz"},
                {"Image", "Img"}, {"Maximum", "Max"}, {"Minimum", "Min"},
                {"Point", "Pt"}, {"Points", "Pts"}, {"Previous", "Prev"},
                {"Residual", "Resid"}, {"Strengths", "Strength"}, {"Threshold", "Thresh"},
                {"Tracking", "Track"}, {"Vertical", "Vert"}};
            words = label.split(' ', Qt::SkipEmptyParts);
            for (QString &word : words)
                word = abbreviations.value(word, word);
            label = words.join(' ');
            if (result.stage == MeasurementStage::Crown && label.startsWith("Crown ")) label.remove(0, 6);
            else if (result.stage == MeasurementStage::Body && label.startsWith("Body ")) label.remove(0, 5);
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
    }
}
