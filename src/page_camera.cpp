#include <QRunnable>
#include <QShowEvent>
#include <QTimer>
#include <stdexcept>
#include "page_camera.hpp"
#include "app_signals.hpp"
#include "ui_PageCamera.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>
#include <QSignalBlocker>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <array>
#include <cmath>
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
                        if (QFileInfo::exists(candidate))
                            return QFileInfo(candidate).absoluteFilePath();
                    }
                    if (!directory.cdUp())
                        break;
                }
            return {};
        }

        cv::Mat processGraphNode(const QString &type, const QJsonObject &parameters, const cv::Mat &input)
        {
            if (input.empty())
                return {};
            cv::Mat output;
            if (type == "Gray")
            {
                if (input.channels() == 1)
                    return input.clone();
                cv::cvtColor(input, output, input.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
            }
            else if (type == "Binarize")
            {
                cv::Mat gray = input;
                if (input.channels() != 1)
                    cv::cvtColor(input, gray, input.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
                cv::threshold(gray, output, parameters.value("threshold").toDouble(100), 255, cv::THRESH_BINARY);
            }
            else if (type == "ROI")
            {
                cv::Rect roi(parameters.value("x").toInt(), parameters.value("y").toInt(),
                             parameters.value("width").toInt(100), parameters.value("height").toInt(100));
                roi &= cv::Rect(0, 0, input.cols, input.rows);
                if (roi.empty())
                    return {};
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
                if (type == "Erode")
                    cv::erode(input, output, kernel, {}, iterations);
                else
                    cv::dilate(input, output, kernel, {}, iterations);
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
            else if (type == "Start" || type == "End")
                output = input.clone();
            return output;
        }
    }

    cv::Mat runPreviewGraph(const cv::Mat &originalImage_, const QString &graphPath_)
    {
        if (originalImage_.empty() || graphPath_.isEmpty())
            return originalImage_;
        QFile file(graphPath_);
        if (!file.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            throw std::runtime_error(parseError.errorString().toStdString());
        }
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
            if (iterator.value() == 0)
                ready.enqueue(iterator.key());
        QHash<QString, cv::Mat> results;
        cv::Mat output;
        int processed = 0;
        while (!ready.isEmpty())
        {
            const QString id = ready.dequeue();
            const auto definition = definitions.value(id);
            const cv::Mat input = predecessor.contains(id) ? results.value(predecessor.value(id)) : originalImage_;
            output = processGraphNode(definition.value("type").toString(), definition.value("params").toObject(), input);
            if (output.empty())
            {
                throw std::runtime_error(QString("Pipeline node failed: %1").arg(definition.value("type").toString()).toStdString());
            }
            results[id] = output;
            ++processed;
            for (const auto &next : successors.value(id))
                if (--indegree[next] == 0)
                    ready.enqueue(next);
        }
        if (processed != nodes.size())
        {
            throw std::runtime_error("Pipeline graph contains a cycle");
        }
        return output;
    }

    PageCamera::PageCamera(CameraService &service, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageCamera>()), service_(service)
    {
        initializePage([this]
                       { ui_->setupUi(this); });
    }
    PageCamera::~PageCamera() { previewPool_.waitForDone(); }
    void PageCamera::initializeState() { previewPool_.setMaxThreadCount(1); }
    void PageCamera::onReady()
    {
        graphPath_ = findGraphPath();
        refreshUi();
    }
    void PageCamera::bindSignals()
    {
        connect(&service_, &CameraService::stateChanged, this, [this](const CameraSnapshot &s)
                {
            snapshot_ = s;
            QSignalBlocker blocker(ui_->combCameraList);
            QStringList previous;
            for (int i = 0; i < ui_->combCameraList->count(); ++i) previous << ui_->combCameraList->itemData(i).toString();
            if (previous != s.ids) {
                ui_->combCameraList->clear();
                for (const auto &id : s.ids) ui_->combCameraList->addItem(id, id);
            }
            ui_->combCameraList->setCurrentIndex(ui_->combCameraList->findData(s.selected));
            refreshUi(); });
        connect(&service_, &CameraService::statusChanged, this, &PageCamera::setStatus);
        auto *previewTimer = new QTimer(this);
        previewTimer->setInterval(100);
        connect(previewTimer, &QTimer::timeout, this, [this]
                {
            if (!isVisible()) return;
            const auto frame = service_.takePreviewFrame();
            if (frame.empty()) return;
            originalImage_ = frame;
            pendingImagePath_.clear();
            previewPending_ = true;
            startPreview(); });
        previewTimer->start();
    }
    void PageCamera::setupPageUi()
    {
        ui_->orgGraphicsView->setViewId(1);
        ui_->transGraphicsView->setViewId(2);
        ui_->orgGraphicsView->setText("Original Image");
        ui_->transGraphicsView->setText("Pipeline Output");
        ui_->lblOrgInfo->setText("View 1 - Pos: (0, 0) | Value: 0");
        ui_->lblTransformedInfo->setText("View 2 - Pos: (0, 0) | Value: 0");
        ui_->splitter->setSizes({500, 500});
        ui_->splitter_2->setSizes({500, 500});
        ui_->splitter_3->setSizes({600, 400});

        ui_->combTrigMode->clear();
        ui_->combTrigMode->addItem("Off", 0);
        ui_->combTrigMode->addItem("On", 1);
        ui_->combTrigSource->clear();
        ui_->combTrigSource->addItem("Software", 0);
        ui_->combTrigSource->addItem("Line1", 1);
        ui_->combTrigEdge->clear();
        ui_->combTrigEdge->addItem("FallingEdge", 0);
        ui_->combTrigEdge->addItem("RisingEdge", 1);
    }

    void PageCamera::bindEvents()
    {
        connect(ui_->btnOrgOpen, &QPushButton::clicked, this, &PageCamera::openImage);
        connect(ui_->orgGraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                { ui_->lblOrgInfo->setText(QString("View 1 - Pos: (%1, %2) | Value: %3").arg(x).arg(y).arg(gray)); });
        connect(ui_->transGraphicsView, &CustomGraphicsView::pixelInfoChanged, this,
                [this](int, int x, int y, int gray)
                { ui_->lblTransformedInfo->setText(QString("View 2 - Pos: (%1, %2) | Value: %3").arg(x).arg(y).arg(gray)); });
        connect(ui_->btnRefresh, &QPushButton::clicked, this, &PageCamera::refreshCameras);
        connect(ui_->combCameraList, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageCamera::selectCamera);
        connect(ui_->btnCamON, &QPushButton::toggled, this, &PageCamera::toggleCamera);
        connect(ui_->btnStartSnap, &QPushButton::toggled, this, &PageCamera::toggleStream);
        connect(ui_->btnSoftTrigger, &QPushButton::clicked, this, &PageCamera::softwareTrigger);
        connect(ui_->edtExposure, &QLineEdit::returnPressed, this, &PageCamera::applyExposure);
        connect(ui_->edtGain, &QLineEdit::returnPressed, this, &PageCamera::applyGain);
        connect(ui_->edtWidth, &QLineEdit::returnPressed, this, &PageCamera::applyWidth);
        connect(ui_->edtHeight, &QLineEdit::returnPressed, this, &PageCamera::applyHeight);
        connect(ui_->edtOffsetX, &QLineEdit::returnPressed, this, &PageCamera::applyOffsetX);
        connect(ui_->edtOffsetY, &QLineEdit::returnPressed, this, &PageCamera::applyOffsetY);
        connect(ui_->combTrigMode, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageCamera::applyTriggerMode);
        connect(ui_->combTrigSource, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageCamera::applyTriggerSource);
        connect(ui_->combTrigEdge, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageCamera::applyTriggerEdge);
        connect(ui_->btnRun, &QPushButton::clicked, this, &PageCamera::runPreviewPipeline);
        connect(ui_->btnConfig, &QPushButton::clicked, this, [this]
                { setStatus(true, QString("Pipeline: %1").arg(graphPath_)); });
        connect(ui_->btnSave, &QPushButton::clicked, this, &PageCamera::savePipeline);
        connect(ui_->btnLoad, &QPushButton::clicked, this, &PageCamera::loadPipeline);
    }

    void PageCamera::setManualControlsEnabled(bool enabled)
    {
        const std::array<QWidget *, 11> widgets{
            ui_->btnStartSnap, ui_->combTrigMode, ui_->combTrigSource, ui_->btnSoftTrigger,
            ui_->combTrigEdge, ui_->edtExposure, ui_->edtGain, ui_->edtWidth,
            ui_->edtHeight, ui_->edtOffsetX, ui_->edtOffsetY};
        for (auto *widget : widgets)
            widget->setEnabled(enabled);
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
        if (path.isEmpty())
            return;
        pendingImagePath_ = path;
        runPreviewPipeline();
    }

    void PageCamera::runPreviewPipeline()
    {
        previewPending_ = true;
        ++previewRevision_;
        startPreview();
    }

    void PageCamera::startPreview()
    {
        if (previewBusy_ || !previewPending_ || !isVisible())
            return;
        previewPending_ = false;
        previewBusy_ = true;
        const auto revision = previewRevision_;
        const auto input = originalImage_;
        const auto path = graphPath_;
        const auto imagePath = pendingImagePath_;
        pendingImagePath_.clear();
        // 同时最多一个任务；忙碌期间只保留最新输入，不堆积图像。
        previewPool_.start(QRunnable::create([this, revision, input, path, imagePath]
                                             {
            cv::Mat original = input, output;
            QString error;
            try {
                if (!imagePath.isEmpty()) {
                    QFile file(imagePath);
                    if (!file.open(QIODevice::ReadOnly))
                        throw std::runtime_error(file.errorString().toStdString());
                    const auto bytes = file.readAll();
                    original = cv::imdecode(cv::Mat(1, bytes.size(), CV_8U,
                        const_cast<char *>(bytes.constData())), cv::IMREAD_UNCHANGED);
                    if (original.empty()) throw std::runtime_error("Cannot decode image");
                }
                output = runPreviewGraph(original, path);
            } catch (const std::exception &exception) {
                error = QString::fromUtf8(exception.what());
            }
            QMetaObject::invokeMethod(this, [this, revision, original, output, error] {
                previewBusy_ = false;
                if (revision == previewRevision_) {
                    if (!previewPending_) originalImage_ = original;
                    if (isVisible()) {
                        if (!original.empty()) ui_->orgGraphicsView->showImage(original, true);
                        if (!output.empty()) ui_->transGraphicsView->showImage(output, true);
                        setStatus(error.isEmpty(), error.isEmpty() ? "Pipeline completed" : error);
                    }
                }
                startPreview();
            }, Qt::QueuedConnection); }));
    }

    void PageCamera::showEvent(QShowEvent *event)
    {
        BasePage::showEvent(event);
        runPreviewPipeline();
    }

    void PageCamera::loadPipeline()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Load pipeline", graphPath_, "Pipeline (*.json)");
        if (path.isEmpty())
            return;
        graphPath_ = path;
        runPreviewPipeline();
    }

    void PageCamera::savePipeline()
    {
        if (graphPath_.isEmpty())
            return;
        const QString destination = QFileDialog::getSaveFileName(this, "Save pipeline", graphPath_, "Pipeline (*.json)");
        if (destination.isEmpty())
            return;
        QFile source(graphPath_);
        if (!source.open(QIODevice::ReadOnly))
        {
            setStatus(false, source.errorString());
            return;
        }
        QSaveFile target(destination);
        if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
        {
            setStatus(false, target.errorString());
            return;
        }
        graphPath_ = destination;
        setStatus(true, "Pipeline saved");
    }

    void PageCamera::refreshUi()
    {
        const bool open = snapshot_.open, streaming = snapshot_.streaming, manual = !snapshot_.production;
        {
            QSignalBlocker block(ui_->btnCamON);
            ui_->btnCamON->setChecked(open);
        }
        {
            QSignalBlocker block(ui_->btnStartSnap);
            ui_->btnStartSnap->setChecked(streaming);
        }
        ui_->btnCamON->setEnabled(!snapshot_.selected.isEmpty() && manual);
        ui_->combCameraList->setEnabled(manual);
        ui_->btnRefresh->setEnabled(manual);
        setManualControlsEnabled(open && manual);
        if (!open)
            return;

        const auto syncCombo = [](QComboBox *combo, qint64 value)
        {
            const QSignalBlocker blocker(combo);
            const int index = combo->findData(value);
            if (index >= 0)
                combo->setCurrentIndex(index);
        };
        const qint64 triggerMode = snapshot_.triggerMode;
        syncCombo(ui_->combTrigMode, triggerMode);
        syncCombo(ui_->combTrigSource, snapshot_.triggerSource);
        syncCombo(ui_->combTrigEdge, snapshot_.triggerEdge);
        ui_->btnSoftTrigger->setEnabled(manual && triggerMode != 0);

        ui_->edtExposure->setText(QString::number(snapshot_.exposure));
        ui_->lblExposure->setText(QString("Exposure(%1 us)").arg(snapshot_.exposure));
        ui_->edtGain->setText(QString::number(snapshot_.gain));
        ui_->lblGain->setText(QString("Gain(%1)").arg(snapshot_.gain));
        ui_->edtWidth->setText(QString::number(snapshot_.width));
        ui_->lblWidth->setText(QString("Width(%1)").arg(snapshot_.width));
        ui_->edtHeight->setText(QString::number(snapshot_.height));
        ui_->lblHeight->setText(QString("Height(%1)").arg(snapshot_.height));
        ui_->edtOffsetX->setText(QString::number(snapshot_.offsetX));
        ui_->lblOffsetX->setText(QString("OffsetX(%1)").arg(snapshot_.offsetX));
        ui_->edtOffsetY->setText(QString::number(snapshot_.offsetY));
        ui_->lblOffsetY->setText(QString("OffsetY(%1)").arg(snapshot_.offsetY));
    }
    void PageCamera::refreshCameras() { QMetaObject::invokeMethod(&service_, "refreshCameras", Qt::QueuedConnection); }
    void PageCamera::softwareTrigger() { QMetaObject::invokeMethod(&service_, "softwareTrigger", Qt::QueuedConnection); }
    void PageCamera::selectCamera(int index) { QMetaObject::invokeMethod(&service_, "selectCamera", Qt::QueuedConnection, Q_ARG(QString, ui_->combCameraList->itemData(index).toString())); }
    void PageCamera::toggleCamera(bool value) { QMetaObject::invokeMethod(&service_, "toggleCamera", Qt::QueuedConnection, Q_ARG(bool, value)); }
    void PageCamera::toggleStream(bool value) { QMetaObject::invokeMethod(&service_, "toggleStream", Qt::QueuedConnection, Q_ARG(bool, value)); }
    void PageCamera::applyExposure() { QMetaObject::invokeMethod(&service_, "applyExposure", Qt::QueuedConnection, Q_ARG(double, ui_->edtExposure->text().toDouble())); }
    void PageCamera::applyGain() { QMetaObject::invokeMethod(&service_, "applyGain", Qt::QueuedConnection, Q_ARG(double, ui_->edtGain->text().toDouble())); }
    void PageCamera::applyWidth() { QMetaObject::invokeMethod(&service_, "applyWidth", Qt::QueuedConnection, Q_ARG(double, ui_->edtWidth->text().toDouble())); }
    void PageCamera::applyHeight() { QMetaObject::invokeMethod(&service_, "applyHeight", Qt::QueuedConnection, Q_ARG(double, ui_->edtHeight->text().toDouble())); }
    void PageCamera::applyOffsetX() { QMetaObject::invokeMethod(&service_, "applyOffsetX", Qt::QueuedConnection, Q_ARG(double, ui_->edtOffsetX->text().toDouble())); }
    void PageCamera::applyOffsetY() { QMetaObject::invokeMethod(&service_, "applyOffsetY", Qt::QueuedConnection, Q_ARG(double, ui_->edtOffsetY->text().toDouble())); }
    void PageCamera::applyTriggerMode(int) { QMetaObject::invokeMethod(&service_, "applyTriggerMode", Qt::QueuedConnection, Q_ARG(int, ui_->combTrigMode->currentData().toInt())); }
    void PageCamera::applyTriggerSource(int) { QMetaObject::invokeMethod(&service_, "applyTriggerSource", Qt::QueuedConnection, Q_ARG(int, ui_->combTrigSource->currentData().toInt())); }
    void PageCamera::applyTriggerEdge(int) { QMetaObject::invokeMethod(&service_, "applyTriggerEdge", Qt::QueuedConnection, Q_ARG(int, ui_->combTrigEdge->currentData().toInt())); }

}
