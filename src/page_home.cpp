#include "page_home.hpp"
#include "ui_PageHome.h"
#include "app_signals.hpp"
#include <QTimer>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QFileInfo>
#include <QTextCursor>
#include <QTreeWidgetItem>
#include <opencv2/imgproc.hpp>
namespace pva
{
    PageHome::PageHome(MeasurementConfig config, RuntimeController &runtime, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageHome>()), runtime_(runtime)
    {
        snapshot_.config = std::move(config);
        initializePage([this] { ui_->setupUi(this); });
    }
    PageHome::~PageHome() = default;
    void PageHome::initializeState()
    {
        renderTimer_ = new QTimer(this);
        renderTimer_->setInterval(67);
        connect(renderTimer_, &QTimer::timeout, this, &PageHome::renderLatest);
        renderTimer_->start();
    }
    void PageHome::bindEvents()
    {
        connect(ui_->btnStart, &QPushButton::clicked, this, &PageHome::toggleRuntime);
        connect(ui_->btnOnline, &QPushButton::toggled, this, &PageHome::toggleOnline);
        for (auto *b : {ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody})
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
        connect(&runtime_, &RuntimeController::stateChanged, this, [this](const RuntimeSnapshot &s) {
            snapshot_ = s;
            if (s.state != RunState::Running || (latestResult_ && latestResult_->generation != s.generation)) { latestResult_.reset(); frameDirty_ = false; }
            QSignalBlocker blocker(ui_->btnOnline);
            ui_->btnOnline->setChecked(s.online);
            ui_->btnOnline->setText(s.online ? "Online" : "Offline");
            refreshControls();
            applyStageToUi();
        });
        connect(&runtime_, &RuntimeController::logReady, this, [this](const QString &line, bool merged) {
            if (merged) {
                auto cursor = ui_->txtLog->textCursor();
                cursor.movePosition(QTextCursor::End);
                cursor.select(QTextCursor::LineUnderCursor);
                cursor.insertText(line);
            } else ui_->txtLog->appendPlainText(line);
        });
        connect(&runtime_, &RuntimeController::facetteReady, this, [this](int i, const cv::Mat &image) {
            if (i < 1 || i > 4) return;
            facetteImages_[i-1] = image;
            facetteDirty_[i-1] = true;
        });
        connect(&runtime_, &RuntimeController::cameraConnectionChanged, this, [this](int i, bool on) {
            setConnectionLed(i == 1 ? ui_->lblCamera1Status : ui_->lblCamera2Status, on);
        });
        connect(&runtime_, &RuntimeController::plcConnectionChanged, this, [this](bool on) { setConnectionLed(ui_->lblPlcStatus, on); });
        connect(&runtime_, &RuntimeController::frameDeltaChanged, this, [this](double ms) {
            ui_->lblFrameDelta->setText(QString("Frame delta: %1 ms").arg(ms, 0, 'f', 1));
        });
    }
    void PageHome::onReady()
    {
        ui_->txtLog->setMaximumBlockCount(2000);
        for (auto *view : {ui_->facetView1, ui_->facetView2, ui_->facetView3, ui_->facetView4}) view->setText("Facette");
        refreshControls();
    }
    void PageHome::reloadConfig(const MeasurementConfig &config)
    {
        auto *runtime = &runtime_;
        QMetaObject::invokeMethod(runtime, [runtime, config] { runtime->reloadConfig(config); }, Qt::QueuedConnection);
    }
    void PageHome::toggleRuntime()
    {
        const bool stop = snapshot_.state == RunState::Running;
        const bool online = ui_->btnOnline->isChecked();
        auto *runtime = &runtime_;
        QMetaObject::invokeMethod(runtime, [runtime, stop, online] {
            if (stop) runtime->stopRuntime(); else runtime->startRuntime(online);
        }, Qt::QueuedConnection);
    }
    void PageHome::toggleOnline(bool online)
    {
        if (QMessageBox::question(this, "Confirm Mode Change", QString("Switch to %1 mode?").arg(online ? "Online" : "Offline"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            QSignalBlocker blocker(ui_->btnOnline); ui_->btnOnline->setChecked(!online); return;
        }
        auto *runtime = &runtime_;
        QMetaObject::invokeMethod(runtime, [runtime, online] {
            runtime->stopRuntime(); if (online) runtime->startRuntime(true);
        }, Qt::QueuedConnection);
    }
    void PageHome::selectStage()
    {
        const std::array<QPushButton *, 5> buttons{ui_->btnStageMelt, ui_->btnStageDip,
            ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody};
        const std::array<MeasurementStage, 5> stages{MeasurementStage::Melt, MeasurementStage::Dip,
            MeasurementStage::Neck, MeasurementStage::Crown, MeasurementStage::Body};
        for (size_t i = 0; i < buttons.size(); ++i) if (sender() == buttons[i]) {
            auto *runtime = &runtime_;
            QMetaObject::invokeMethod(runtime, [runtime, stage = stages[i]] { runtime->selectStage(stage); }, Qt::QueuedConnection);
        }
    }
    void PageHome::firstImage() { QMetaObject::invokeMethod(&runtime_, "setImageIndex", Qt::QueuedConnection, Q_ARG(int, 0)); }
    void PageHome::previousImage() { QMetaObject::invokeMethod(&runtime_, "stepImage", Qt::QueuedConnection, Q_ARG(int, -1)); }
    void PageHome::nextImage() { QMetaObject::invokeMethod(&runtime_, "stepImage", Qt::QueuedConnection, Q_ARG(int, 1)); }
    void PageHome::lastImage() { QMetaObject::invokeMethod(&runtime_, "setImageIndex", Qt::QueuedConnection, Q_ARG(int, int(snapshot_.images.size()) - 1)); }
    void PageHome::onSherlockCommand(const SherlockCommand &command)
    {
        auto *runtime = &runtime_;
        QMetaObject::invokeMethod(runtime, [runtime, command] { runtime->onSherlockCommand(command); }, Qt::QueuedConnection);
    }
    void PageHome::showResult(const MeasurementResult &result)
    {
        if (result.generation != snapshot_.generation || snapshot_.state != RunState::Running) return;
        latestResult_ = result; frameDirty_ = true; diagnosticDirty_ = true;
    }
    void PageHome::renderLatest()
    {
        if (!isVisible()) return;
        CustomGraphicsView *views[] = {ui_->facetView1, ui_->facetView2, ui_->facetView3, ui_->facetView4};
        for (int i = 0; i < 4; ++i) if (facetteDirty_[i]) {
            if (facetteImages_[i].empty()) views[i]->setText(QString("Facette%1.bmp").arg(i+1));
            else views[i]->showImage(facetteImages_[i]);
            facetteDirty_[i] = false;
        }
        if (auto result = runtime_.takeLatestResult()) showResult(*result);
        if (!latestResult_) return;
        if (frameDirty_) {
            paintResult(*latestResult_);
            frameDirty_ = false;
        }
        if (diagnosticDirty_ && (!diagnosticClock_.isValid() || diagnosticClock_.elapsed() >= 200)) {
            updateProcessDiagnostics(*latestResult_);
            diagnosticClock_.restart();
            diagnosticDirty_ = false;
        }
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
        for (auto *b : {ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody})
            group->addButton(b);
        applyStageToUi();
    }

    void PageHome::refreshControls()
    {
        bool offline = !ui_->btnOnline->isChecked();
        ui_->btnStart->setText((snapshot_.state == RunState::Running) ? "Stop" : "Run");
        ui_->btnStart->setProperty("runtimeState", (snapshot_.state == RunState::Running) ? "running" : "stopped");
        ui_->btnStart->style()->unpolish(ui_->btnStart);
        ui_->btnStart->style()->polish(ui_->btnStart);
        ui_->btnStart->setEnabled(offline);
        for (auto *b : {ui_->btnStageMelt, ui_->btnStageDip, ui_->btnStageNeck, ui_->btnStageCrown, ui_->btnStageBody})
            b->setEnabled(offline);
        ui_->lblOfflineImage->setText(snapshot_.imageIndex >= 0 ? QFileInfo(snapshot_.images[snapshot_.imageIndex]).fileName() : "No image");
        ui_->lblOfflineImage->setToolTip(snapshot_.imageIndex >= 0 ? snapshot_.images[snapshot_.imageIndex] : snapshot_.config.runtime.offlineImageDir);
        ui_->lblImageIndex->setText(snapshot_.images.isEmpty() ? "0 / 0" : QString("%1 / %2").arg(snapshot_.imageIndex + 1).arg(snapshot_.images.size()));
        ui_->btnFirstImage->setEnabled(offline && snapshot_.imageIndex > 0);
        ui_->btnPreviousImage->setEnabled(offline && snapshot_.imageIndex > 0);
        ui_->btnNextImage->setEnabled(offline && snapshot_.imageIndex >= 0 && snapshot_.imageIndex < snapshot_.images.size() - 1);
        ui_->btnLastImage->setEnabled(ui_->btnNextImage->isEnabled());
    }

    void PageHome::applyStageToUi()
    {
        QPushButton *selected = ui_->btnStageMelt;
        switch (snapshot_.stage)
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
        default:
            break;
        }
        QSignalBlocker blocker(selected);
        selected->setChecked(true);
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
        const QString exposure = snapshot_.online && info.exposureUs
                                     ? QString::number(*info.exposureUs / 1000.0, 'f', 2)
                                     : "--";
        const QString mean = snapshot_.online && info.roiMean
                                 ? QString::number(*info.roiMean, 'f', 1)
                                 : "--";
        auto *label = viewId == 1 ? ui_->lblCam1Info : ui_->lblCam2Info;
        label->setText(QString("Pos (%1,%2) | G %3 | Light %4 | Exp %5 ms | Mean %6")
                           .arg(info.x)
                           .arg(info.y)
                           .arg(info.gray)
                           .arg(light, exposure, mean));
    }

    void PageHome::onCameraExposure(const QString &userId, double exposureUs)
    {
        const qsizetype index = CameraRole::Stereo.indexOf(userId);
        if (index < 0)
            return;
        viewInfo_[index].exposureUs = exposureUs;
        updateViewInfo(int(index + 1));
    }

    void PageHome::paintResult(const MeasurementResult &r)
    {
        auto overlay1 = r.overlay1;
        auto overlay2 = r.overlay2;
        if (snapshot_.online && snapshot_.config.camera.autoExposureEnabled)
        {
            addAutoExposureRoi(overlay1, snapshot_.config.measurement.autoExposureRoiCamera1, r.preview1.size());
            addAutoExposureRoi(overlay2, snapshot_.config.measurement.autoExposureRoiCamera2, r.preview2.size());
            viewInfo_[0].roiMean = roiMean(r.preview1, snapshot_.config.measurement.autoExposureRoiCamera1);
            viewInfo_[1].roiMean = roiMean(r.preview2, snapshot_.config.measurement.autoExposureRoiCamera2);
        }
        appendPlcRoiOverlays(snapshot_.rois, snapshot_.stage, snapshot_.config.measurement.diaRectHeightPx,
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
        ui_->lblLastFrame->setText("Last frame: " + QDateTime::currentDateTime().toString("HH:mm:ss"));
        viewInfo_[0].light = r.diagnostics.contains("light_camera1") ? r.diagnostics.at("light_camera1").toDouble() : 0.0;
        viewInfo_[1].light = r.diagnostics.contains("light_camera2") ? r.diagnostics.at("light_camera2").toDouble() : 0.0;
        updateViewInfo(1);
        updateViewInfo(2);
    }

    void PageHome::updateProcessDiagnostics(const MeasurementResult &result)
    {
        const bool first = ui_->treeProcess->topLevelItemCount() == 0;
        ui_->treeProcess->setUpdatesEnabled(false);
        auto *camera1 = first ? new QTreeWidgetItem(ui_->treeProcess, {"Camera 1"}) : ui_->treeProcess->topLevelItem(0);
        auto *camera2 = first ? new QTreeWidgetItem(ui_->treeProcess, {"Camera 2"}) : ui_->treeProcess->topLevelItem(1);
        auto *algorithm = first ? new QTreeWidgetItem(ui_->treeProcess, {"Algorithm"}) : ui_->treeProcess->topLevelItem(2);
        auto *runtime = first ? new QTreeWidgetItem(ui_->treeProcess, {"Runtime"}) : ui_->treeProcess->topLevelItem(3);
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
        if (first) camera1->setExpanded(true);
        if (first) camera2->setExpanded(true);
        if (first) algorithm->setExpanded(true);
        if (first) runtime->setExpanded(true);

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

        for (auto *item : diagnosticItems_) item->setHidden(true);
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
            const auto itemKey = QString::fromStdString(key);
            auto *item = diagnosticItems_.value(itemKey, nullptr);
            if (!item) { item = new QTreeWidgetItem(group); diagnosticItems_.insert(itemKey, item); }
            item->setHidden(false);
            item->setText(0, label);
            item->setText(1, formattedValue(value));
            item->setText(2, unit);
            item->setToolTip(0, QString::fromStdString(key));
            item->setToolTip(1, formattedValue(value));
        }

        // 只按显示名称排序各分组的子项，保持四个顶层分组的固定顺序。
        camera1->sortChildren(0, Qt::AscendingOrder);
        camera2->sortChildren(0, Qt::AscendingOrder);
        algorithm->sortChildren(0, Qt::AscendingOrder);
        runtime->sortChildren(0, Qt::AscendingOrder);
        ui_->treeProcess->setUpdatesEnabled(true);
    }

}
