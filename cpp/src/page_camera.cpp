#include "pva/page_camera.hpp"
#include "pva/app_signals.hpp"
#include "pva/dalsa_camera.hpp"
#include "ui_PageCamera.h"
#include <QCoreApplication>
#include <array>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QQueue>
#include <QFile>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QPointer>
#include <QThread>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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
    namespace
    {
        QString findGraphPath()
        {
            for (QDir directory : {QDir::current(), QDir(QCoreApplication::applicationDirPath())})
                for (int level = 0; level < 6; ++level)
                {
                    for (const auto &relative : {QString("src/graph.json"), QString("graph.json")})
                    {
                        const QString candidate = directory.absoluteFilePath(relative);
                        if (QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
                    }
                    if (!directory.cdUp()) break;
                }
            return {};
        }

        cv::Mat processGraphNode(const QString &type, const QJsonObject &parameters, const cv::Mat &input)
        {
            if (input.empty()) return {};
            cv::Mat output;
            if (type == "Gray")
            {
                if (input.channels() == 1) return input.clone();
                cv::cvtColor(input, output, input.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
            }
            else if (type == "Binarize")
            {
                cv::Mat gray = input;
                if (input.channels() != 1) cv::cvtColor(input, gray, input.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
                cv::threshold(gray, output, parameters.value("threshold").toDouble(100), 255, cv::THRESH_BINARY);
            }
            else if (type == "ROI")
            {
                cv::Rect roi(parameters.value("x").toInt(), parameters.value("y").toInt(),
                             parameters.value("width").toInt(100), parameters.value("height").toInt(100));
                roi &= cv::Rect(0, 0, input.cols, input.rows);
                if (roi.empty()) return {};
                output = input(roi).clone();
            }
            else if (type == "GaussianBlur")
            {
                int size = std::clamp(parameters.value("ksize").toInt(5), 1, 99) | 1;
                cv::GaussianBlur(input, output, {size, size}, parameters.value("sigma").toDouble(1.0));
            }
            else if (type == "Erode" || type == "Dilate")
            {
                const int kx = std::clamp(parameters.value("kx").toInt(3), 1, 99);
                const int ky = std::clamp(parameters.value("ky").toInt(3), 1, 99);
                const int iterations = std::max(parameters.value("iterations").toInt(1), 1);
                const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, {kx, ky});
                if (type == "Erode") cv::erode(input, output, kernel, {}, iterations);
                else cv::dilate(input, output, kernel, {}, iterations);
            }
            else if (type == "Rotate")
            {
                const double angle = parameters.value("angle").toDouble(1.0);
                const cv::Point2f center(input.cols / 2.0f, input.rows / 2.0f);
                cv::Mat transform = cv::getRotationMatrix2D(center, angle, 1.0);
                const double cosine = std::abs(transform.at<double>(0, 0));
                const double sine = std::abs(transform.at<double>(0, 1));
                const cv::Size size(int(input.rows * sine + input.cols * cosine), int(input.rows * cosine + input.cols * sine));
                transform.at<double>(0, 2) += size.width / 2.0 - center.x;
                transform.at<double>(1, 2) += size.height / 2.0 - center.y;
                cv::warpAffine(input, output, transform, size);
            }
            else if (type == "Start" || type == "End") output = input.clone();
            return output;
        }
    }

    PageCamera::PageCamera(MeasurementConfig config, QWidget *parent)
        : QWidget(parent), ui_(std::make_unique<Ui::PageCamera>()), config_(std::move(config))
    {
        ui_->setupUi(this);
        clock_.start();
        ui_->orgGraphicsView->setViewId(1);
        ui_->transGraphicsView->setViewId(2);
        ui_->orgGraphicsView->setText("Original Image");
        ui_->transGraphicsView->setText("Pipeline Output");
        ui_->lblOrgInfo->setText("View 1 - Pos: (0, 0) | Value: 0");
        ui_->lblTransformedInfo->setText("View 2 - Pos: (0, 0) | Value: 0");
        ui_->splitter->setSizes({500, 500});
        ui_->splitter_2->setSizes({500, 500});
        ui_->splitter_3->setSizes({600, 400});

        ui_->combTrigMode->clear(); ui_->combTrigMode->addItem("Off", 0); ui_->combTrigMode->addItem("On", 1);
        ui_->combTrigSource->clear(); ui_->combTrigSource->addItem("Software", 0); ui_->combTrigSource->addItem("Line1", 1);
        ui_->combTrigEdge->clear(); ui_->combTrigEdge->addItem("FallingEdge", 0); ui_->combTrigEdge->addItem("RisingEdge", 1);

        connect(ui_->btnOrgOpen, &QPushButton::clicked, this, &PageCamera::openImage);
        connect(ui_->orgGraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                { ui_->lblOrgInfo->setText(QString("View 1 - Pos: (%1, %2) | Value: %3").arg(x).arg(y).arg(gray)); });
        connect(ui_->transGraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                { ui_->lblTransformedInfo->setText(QString("View 2 - Pos: (%1, %2) | Value: %3").arg(x).arg(y).arg(gray)); });
        connect(ui_->btnRefresh, &QPushButton::clicked, this, &PageCamera::refreshCameras);
        connect(ui_->combCameraList, &QComboBox::currentIndexChanged, this, &PageCamera::selectCamera);
        connect(ui_->btnCamON, &QPushButton::toggled, this, &PageCamera::toggleCamera);
        connect(ui_->btnStartSnap, &QPushButton::toggled, this, &PageCamera::toggleStream);
        connect(ui_->btnSoftTrigger, &QPushButton::clicked, this, &PageCamera::softwareTrigger);
        connect(ui_->edtExposure, &QLineEdit::returnPressed, this, &PageCamera::applyExposure);
        connect(ui_->edtGain, &QLineEdit::returnPressed, this, &PageCamera::applyGain);
        connect(ui_->edtWidth, &QLineEdit::returnPressed, this, &PageCamera::applyWidth);
        connect(ui_->edtHeight, &QLineEdit::returnPressed, this, &PageCamera::applyHeight);
        connect(ui_->edtOffsetX, &QLineEdit::returnPressed, this, &PageCamera::applyOffsetX);
        connect(ui_->edtOffsetY, &QLineEdit::returnPressed, this, &PageCamera::applyOffsetY);
        connect(ui_->combTrigMode, &QComboBox::currentIndexChanged, this, &PageCamera::applyTriggerMode);
        connect(ui_->combTrigSource, &QComboBox::currentIndexChanged, this, &PageCamera::applyTriggerSource);
        connect(ui_->combTrigEdge, &QComboBox::currentIndexChanged, this, &PageCamera::applyTriggerEdge);
        connect(ui_->btnRun, &QPushButton::clicked, this, &PageCamera::runPreviewPipeline);
        connect(ui_->btnConfig, &QPushButton::clicked, this, [this] { setStatus(true, QString("Pipeline: %1").arg(graphPath_)); });
        connect(ui_->btnSave, &QPushButton::clicked, this, &PageCamera::savePipeline);
        connect(ui_->btnLoad, &QPushButton::clicked, this, &PageCamera::loadPipeline);

        auto &appSignals = AppSignals::instance();
        connect(&appSignals, &AppSignals::onlineCameraStartRequested, this, &PageCamera::startOnlineCameras);
        connect(&appSignals, &AppSignals::onlineCameraStopRequested, this, &PageCamera::stopOnlineCameras);
        connect(&appSignals, &AppSignals::onlineCameraTriggerRequested, this, &PageCamera::triggerOnlineCameras);
        connect(&appSignals, &AppSignals::onlineStageChanged, this, [this](int stage) { onlineStage_ = MeasurementStage(stage); });
        connect(&appSignals, &AppSignals::appClose, this, &PageCamera::closeAll);

        graphPath_ = findGraphPath();
        QString error;
        sdkInitialized_ = DalsaCamera::initialize(&error);
        if (!sdkInitialized_) setStatus(false, error);
        else refreshCameras();
    }
    PageCamera::~PageCamera()
    {
        closeAll();
        if (sdkInitialized_) DalsaCamera::shutdown();
    }
    void PageCamera::reloadConfig(const MeasurementConfig &config) { config_ = config; }
    DalsaCamera *PageCamera::camera(const QString &role) const
    {
        const auto iterator = cameras_.find(role);
        return iterator == cameras_.end() ? nullptr : iterator->second.get();
    }
    void PageCamera::refreshCameras()
    {
        if (cameraDiscoveryRunning_) return;
        closeAll();
        current_ = nullptr;
        cameras_.clear();
        ui_->combCameraList->clear();
        cameraDiscoveryRunning_ = true;
        ui_->btnRefresh->setEnabled(false);
        setStatus(true, "Searching for Sapera cameras...");

        const QPointer<PageCamera> guard(this);
        auto *thread = QThread::create([guard]
        {
            QString error;
            DalsaCamera::initialize();
            const QStringList ids = DalsaCamera::enumerate(&error);
            DalsaCamera::shutdown();
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, ids, error]
            {
                if (!guard) return;
                guard->cameraDiscoveryRunning_ = false;
                for (const auto &id : ids)
                    if (id == "1" || id == "2")
                    {
                        auto value = std::make_unique<DalsaCamera>(id);
                        connect(value.get(), &DalsaCamera::frameReady, guard, &PageCamera::onFrame, Qt::QueuedConnection);
                        connect(value.get(), &DalsaCamera::captureFailed, guard, &PageCamera::onCaptureFailed, Qt::QueuedConnection);
                        guard->cameras_.insert_or_assign(id, std::move(value));
                        guard->ui_->combCameraList->addItem("CAM" + id, id);
                    }
                if (guard->ui_->combCameraList->count()) guard->ui_->combCameraList->setCurrentIndex(0);
                guard->setStatus(error.isEmpty(), error.isEmpty() ? QString("%1 camera(s) found").arg(guard->cameras_.size()) : error);
                guard->refreshUi();
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    void PageCamera::selectCamera(int index)
    {
        current_ = index < 0 ? nullptr : camera(ui_->combCameraList->itemData(index).toString());
        refreshUi();
    }
    bool PageCamera::applyConfiguredParameters(DalsaCamera &value, const cv::Rect &roi, bool online, QString *error)
    {
        const bool first = value.userId() == "1";
        const double initial = first ? config_.camera.initialExposureCamera1 : config_.camera.initialExposureCamera2;
        const double exposure = online ? loadRememberedExposure(value.userId(), initial) : initial;
        const double gain = first ? config_.camera.gainCamera1 : config_.camera.gainCamera2;
        // GenICam ROI 修改顺序必须先清零 Offset，再缩放尺寸，最后恢复 Offset。
        return value.setOffsetX(0, error) && value.setOffsetY(0, error) &&
               value.setWidth(roi.width, error) && value.setHeight(roi.height, error) &&
               value.setOffsetX(roi.x, error) && value.setOffsetY(roi.y, error) &&
               value.setExposure(exposure, error) && value.setGain(gain, error);
    }
    void PageCamera::toggleCamera(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty()) { refreshUi(); return; }
        QString error;
        if (checked)
        {
            if (!current_->open(&error) || !applyConfiguredParameters(*current_, config_.camera.offlineCropRoi, false, &error))
                setStatus(false, error);
        }
        else current_->close();
        refreshUi();
    }
    void PageCamera::toggleStream(bool checked)
    {
        if (!current_ || !streamOwner_.isEmpty()) { refreshUi(); return; }
        QString error;
        if (checked)
        {
            if (!current_->startStream(&error)) setStatus(false, error);
        }
        else current_->stopStream();
        refreshUi();
    }
    void PageCamera::softwareTrigger() { QString error; if (!current_ || !current_->softwareTrigger(&error)) setStatus(false, error); }
    void PageCamera::applyExposure() { QString e; if (current_ && !current_->setExposure(ui_->edtExposure->text().toDouble(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyGain() { QString e; if (current_ && !current_->setGain(ui_->edtGain->text().toDouble(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyWidth() { QString e; if (current_ && !current_->setWidth(ui_->edtWidth->text().toLongLong(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyHeight() { QString e; if (current_ && !current_->setHeight(ui_->edtHeight->text().toLongLong(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyOffsetX() { QString e; if (current_ && !current_->setOffsetX(ui_->edtOffsetX->text().toLongLong(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyOffsetY() { QString e; if (current_ && !current_->setOffsetY(ui_->edtOffsetY->text().toLongLong(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyTriggerMode(int) { QString e; if (current_ && !current_->setTriggerMode(ui_->combTrigMode->currentData().toBool(), &e)) setStatus(false, e); refreshUi(); }
    void PageCamera::applyTriggerSource(int) { QString e; if (current_ && !current_->setTriggerSource(ui_->combTrigSource->currentData().toLongLong(), &e)) setStatus(false, e); }
    void PageCamera::applyTriggerEdge(int) { QString e; if (current_ && !current_->setTriggerEdge(ui_->combTrigEdge->currentData().toLongLong(), &e)) setStatus(false, e); }

    void PageCamera::startOnlineCameras()
    {
        if (!streamOwner_.isEmpty()) { emit AppSignals::instance().onlineCameraFailed("Cameras are already in use"); return; }
        QString error;
        for (const auto &role : {QString("1"), QString("2")})
        {
            auto *value = camera(role);
            if (!value) { error = "Missing camera user id: " + role; break; }
            if (!value->open(&error) || !applyConfiguredParameters(*value, config_.camera.onlineCropRoi, true, &error) ||
                !value->setTriggerSource(0, &error) || !value->setTriggerMode(true, &error) || !value->startStream(&error)) break;
        }
        if (!error.isEmpty())
        {
            closeAll(); setStatus(false, error); emit AppSignals::instance().onlineCameraFailed(error); return;
        }
        streamOwner_ = "online";
        refreshUi();
        emit AppSignals::instance().onlineCameraStarted();
        setStatus(true, "Online cameras started: CAM1, CAM2");
    }
    void PageCamera::stopOnlineCameras()
    {
        if (streamOwner_ != "online") return;
        saveRememberedExposures();
        closeAll();
        emit AppSignals::instance().onlineCameraStopped();
    }
    void PageCamera::triggerOnlineCameras()
    {
        if (streamOwner_ != "online") return;
        for (const auto &role : {QString("1"), QString("2")})
        {
            QString error;
            if (!camera(role)->softwareTrigger(&error)) { emit AppSignals::instance().onlineCameraFailed("CAM" + role + " trigger failed: " + error); return; }
        }
    }
    void PageCamera::onFrame(const QString &role, const cv::Mat &frame, qint64 timestampNs)
    {
        auto *value = camera(role);
        if (value) adjustAutoExposure(*value, frame, timestampNs);
        // GigE 特征读取是同步操作，不在每个图像回调中执行。
        if (value && timestampNs - lastExposurePublishNs_.value(role, 0) >= 500000000LL)
        {
            lastExposurePublishNs_[role] = timestampNs;
            emit AppSignals::instance().cameraExposureChanged(role, value->exposure());
        }
        // 手动自由运行预览限制为 10 FPS，图像管线不会占满 UI 线程。
        if (streamOwner_.isEmpty() && timestampNs - lastManualPreviewNs_ >= 100000000LL)
        {
            lastManualPreviewNs_ = timestampNs;
            originalImage_ = frame.clone();
            ui_->orgGraphicsView->showImage(frame, true);
            runPreviewPipeline();
        }
        emit AppSignals::instance().cameraFrameCaptured(role, frame);
        if (value) value->frameConsumed();
    }
    void PageCamera::onCaptureFailed(const QString &role, const QString &message)
    {
        setStatus(false, "CAM" + role + ": " + message);
        if (streamOwner_ == "online") emit AppSignals::instance().onlineCameraFailed("CAM" + role + ": " + message);
    }
    void PageCamera::adjustAutoExposure(DalsaCamera &value, const cv::Mat &frame, qint64 timestampNs)
    {
        if (streamOwner_ != "online" || !config_.camera.autoExposureEnabled ||
            (onlineStage_ != MeasurementStage::Idle && onlineStage_ != MeasurementStage::Neck)) return;
        const qint64 minimumDelta = qint64(std::max(config_.camera.autoExposureIntervalMs, 50)) * 1000000;
        if (timestampNs - lastExposureAdjustNs_.value(value.userId(), 0) < minimumDelta) return;
        const cv::Rect roi = clippedRoi(value.userId() == "1" ? config_.measurement.autoExposureRoiCamera1 : config_.measurement.autoExposureRoiCamera2, frame.size());
        if (roi.empty()) return;
        cv::Mat gray;
        if (frame.channels() == 1) gray = frame; else cv::cvtColor(frame, gray, frame.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        const double mean = cv::mean(gray(roi))[0];
        const double target = std::max(config_.camera.autoExposureTarget, 1.0);
        const double error = target - mean;
        lastExposureAdjustNs_[value.userId()] = timestampNs;
        if (std::abs(error) <= config_.camera.autoExposureDeadband) return;
        const double next = std::clamp(value.exposure() * (1.0 + std::max(config_.camera.autoExposureGain, 0.0) * error / target),
                                       std::min(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs),
                                       std::max(config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs));
        value.setExposure(next);
        saveRememberedExposures();
    }
    double PageCamera::loadRememberedExposure(const QString &role, double fallback) const
    {
        QFile file(cameraStatePath(config_));
        if (!file.open(QIODevice::ReadOnly)) return fallback;
        const auto values = QJsonDocument::fromJson(file.readAll()).object().value("exposure_us").toObject();
        return std::clamp(values.value("camera" + role).toDouble(fallback), config_.camera.autoExposureMinUs, config_.camera.autoExposureMaxUs);
    }
    void PageCamera::saveRememberedExposures() const
    {
        QJsonObject exposures;
        for (const auto &role : {QString("1"), QString("2")}) if (auto *value = camera(role); value && value->isOpen()) exposures["camera" + role] = value->exposure();
        if (exposures.isEmpty()) return;
        QFile file(cameraStatePath(config_));
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{{"version", 1}, {"exposure_us", exposures}}).toJson(QJsonDocument::Indented));
    }
    void PageCamera::closeAll()
    {
        for (auto &[role, value] : cameras_) value->close();
        streamOwner_.clear();
        refreshUi();
    }
    void PageCamera::setManualControlsEnabled(bool enabled)
    {
        const std::array<QWidget *, 11> widgets{
            ui_->btnStartSnap, ui_->combTrigMode, ui_->combTrigSource, ui_->btnSoftTrigger,
            ui_->combTrigEdge, ui_->edtExposure, ui_->edtGain, ui_->edtWidth,
            ui_->edtHeight, ui_->edtOffsetX, ui_->edtOffsetY};
        for (auto *widget : widgets) widget->setEnabled(enabled);
    }
    void PageCamera::refreshUi()
    {
        const bool open = current_ && current_->isOpen(), streaming = current_ && current_->isStreaming(), manual = streamOwner_.isEmpty();
        { QSignalBlocker block(ui_->btnCamON); ui_->btnCamON->setChecked(open); }
        { QSignalBlocker block(ui_->btnStartSnap); ui_->btnStartSnap->setChecked(streaming); }
        ui_->btnCamON->setEnabled(current_ && manual);
        ui_->combCameraList->setEnabled(manual);
        ui_->btnRefresh->setEnabled(manual);
        setManualControlsEnabled(open && manual);
        if (!open) return;
        ui_->edtExposure->setText(QString::number(current_->exposure())); ui_->lblExposure->setText(QString("Exposure(%1 us)").arg(current_->exposure()));
        ui_->edtGain->setText(QString::number(current_->gain())); ui_->lblGain->setText(QString("Gain(%1)").arg(current_->gain()));
        ui_->edtWidth->setText(QString::number(current_->width())); ui_->lblWidth->setText(QString("Width(%1)").arg(current_->width()));
        ui_->edtHeight->setText(QString::number(current_->height())); ui_->lblHeight->setText(QString("Height(%1)").arg(current_->height()));
        ui_->edtOffsetX->setText(QString::number(current_->offsetX())); ui_->lblOffsetX->setText(QString("OffsetX(%1)").arg(current_->offsetX()));
        ui_->edtOffsetY->setText(QString::number(current_->offsetY())); ui_->lblOffsetY->setText(QString("OffsetY(%1)").arg(current_->offsetY()));
    }
    void PageCamera::setStatus(bool ok, const QString &message)
    {
        ui_->lblStatus->setToolTip(message);
        ui_->lblStatus->setPixmap(QPixmap(ok ? ":/images/images/images/ledLow.png" : ":/images/images/images/ledHigh.png"));
        emit AppSignals::instance().status("Camera", ok ? "OK" : "NG", ok ? "info" : "error", message);
    }
    void PageCamera::openImage()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Open image", {}, "Images (*.bmp *.png *.jpg *.jpeg *.tif *.tiff)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        const QByteArray bytes = file.readAll();
        const cv::Mat buffer(1, bytes.size(), CV_8U, const_cast<char *>(bytes.constData()));
        originalImage_ = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
        if (!originalImage_.empty()) { ui_->orgGraphicsView->showImage(originalImage_); runPreviewPipeline(); }
    }
    void PageCamera::runPreviewPipeline()
    {
        if (originalImage_.empty() || graphPath_.isEmpty()) return;
        QFile file(graphPath_);
        if (!file.open(QIODevice::ReadOnly)) { setStatus(false, file.errorString()); return; }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) { setStatus(false, parseError.errorString()); return; }
        const auto root = document.object();
        const auto nodes = root.value("nodes").toArray();
        const auto edges = root.value("edges").toArray();
        QHash<QString, QJsonObject> definitions;
        QHash<QString, QStringList> successors;
        QHash<QString, QString> predecessor;
        QHash<QString, int> indegree;
        for (const auto &entry : nodes)
        {
            const auto object = entry.toObject();
            const QString id = object.value("id").toString();
            definitions[id] = object;
            indegree[id] = 0;
        }
        for (const auto &entry : edges)
        {
            const auto edge = entry.toObject();
            const QString source = edge.value("source").toString(), target = edge.value("target").toString();
            successors[source].append(target);
            predecessor[target] = source;
            ++indegree[target];
        }
        QQueue<QString> ready;
        for (auto iterator = indegree.cbegin(); iterator != indegree.cend(); ++iterator)
            if (iterator.value() == 0) ready.enqueue(iterator.key());
        QHash<QString, cv::Mat> results;
        cv::Mat output;
        int processed = 0;
        while (!ready.isEmpty())
        {
            const QString id = ready.dequeue();
            const auto definition = definitions.value(id);
            const cv::Mat input = predecessor.contains(id) ? results.value(predecessor.value(id)) : originalImage_;
            output = processGraphNode(definition.value("type").toString(), definition.value("params").toObject(), input);
            if (output.empty()) { setStatus(false, QString("Pipeline node failed: %1").arg(definition.value("type").toString())); return; }
            results[id] = output;
            ++processed;
            for (const auto &next : successors.value(id)) if (--indegree[next] == 0) ready.enqueue(next);
        }
        if (processed != nodes.size()) { setStatus(false, "Pipeline graph contains a cycle"); return; }
        ui_->transGraphicsView->showImage(output, true);
        setStatus(true, QString("Pipeline completed: %1 nodes").arg(processed));
    }

    void PageCamera::loadPipeline()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Load pipeline", graphPath_, "Pipeline (*.json)");
        if (path.isEmpty()) return;
        graphPath_ = path;
        runPreviewPipeline();
    }

    void PageCamera::savePipeline()
    {
        if (graphPath_.isEmpty()) return;
        const QString destination = QFileDialog::getSaveFileName(this, "Save pipeline", graphPath_, "Pipeline (*.json)");
        if (destination.isEmpty()) return;
        QFile source(graphPath_);
        if (!source.open(QIODevice::ReadOnly)) { setStatus(false, source.errorString()); return; }
        QSaveFile target(destination);
        if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
        {
            setStatus(false, target.errorString());
            return;
        }
        graphPath_ = destination;
        setStatus(true, "Pipeline saved");
    }
}
