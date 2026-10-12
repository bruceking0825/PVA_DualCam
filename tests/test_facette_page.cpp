#include "page_home.hpp"
#include "camera_service.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QPushButton>
#include <QFileInfo>
#include <QPixmap>
#include "sherlock_protocol.hpp"
#include "custom_graphics_view.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <functional>
#include <opencv2/imgcodecs.hpp>
#include <iostream>

namespace
{
    int failures = 0;
    bool until(const std::function<bool()> &ready) {
        QElapsedTimer clock; clock.start();
        while (!ready() && clock.elapsed() < 3000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        return ready();
    }
    void check(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    bool hasPicture(CustomGraphicsView *view)
    {
        if (!view)
            return false;
        for (auto *item : view->scene()->items())
            if (qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
                return true;
        return false;
    }
}

int main(int argc, char **argv)
{
    using pva::SherlockCommand;
    using pva::MeasurementResult;
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary test directory");
    if (!temporary.isValid())
        return 1;

    const QString inputDirectory = temporary.filePath("input");
    const QString outputDirectory = temporary.filePath("facettes");
    QDir().mkpath(inputDirectory);
    cv::Mat pair(8, 8, CV_8UC1, cv::Scalar(10));
    pair.rowRange(4, 8).setTo(40);
    const QString pairPath = QDir(inputDirectory).filePath("pair.bmp");
    check(cv::imwrite(QFile::encodeName(pairPath).constData(), pair),
          "Write simulated stereo image");

    auto config = pva::MeasurementConfig::loadIni(QString::fromLocal8Bit(argv[1]));
    config.runtime.offlineImageDir = inputDirectory;
    config.runtime.facetteImageDir = outputDirectory;
   config.runtime.stateFile = temporary.filePath("measurement_state.json");
    config.runtime.measurementImageDir = temporary.filePath("measurement_images");
    pva::RuntimeController runtime(config, nullptr, false);
    QWidget pagesContainer;
    pagesContainer.setObjectName("pagesContainer");
    pva::PageHome page(config, runtime, &pagesContainer);
    pagesContainer.resize(1100, 655);
    pagesContainer.show();
    page.show();
    pva::CameraSnapshot health;
    health.ids = pva::CameraRole::Stereo;
    health.healthyIds = pva::CameraRole::Stereo;
    page.onCameraState(health);
    auto *led1 = page.findChild<QLabel *>("lblCamera1Status");
    auto *led2 = page.findChild<QLabel *>("lblCamera2Status");
    const auto onImage = QPixmap(":/images/images/images/ledLow.png").toImage();
    const auto offImage = QPixmap(":/images/images/images/ledHigh.png").toImage();
    check(led1 && led2 && led1->property("pixmap").value<QPixmap>().toImage() == onImage &&
          led2->property("pixmap").value<QPixmap>().toImage() == onImage,
          "Home lights both enumerated healthy cameras");
    health.healthyIds.removeAll(pva::CameraRole::Cam1);
    page.onCameraState(health);
    check(led1 && led2 && led1->property("pixmap").value<QPixmap>().toImage() == offImage &&
          led2->property("pixmap").value<QPixmap>().toImage() == onImage,
          "Home extinguishes only the failed camera");
    int storageUpdates = 0;
    QObject::connect(&runtime, &pva::RuntimeController::facetteReady, &app, [&](int, const cv::Mat &) { ++storageUpdates; });
    runtime.initialize();
    check(until([&] { return storageUpdates >= 4; }), "Initial Facette loads complete");

    auto *firstView = page.findChild<CustomGraphicsView *>("facetView1");
    auto *secondView = page.findChild<CustomGraphicsView *>("facetView2");
    check(firstView && secondView && !hasPicture(firstView) && !hasPicture(secondView),
          "Facette views begin with filename placeholders");

    pva::SherlockCommand command{"pic_fac1", {}, "pic_fac1"};
    runtime.onSherlockCommand(command);
    QCoreApplication::processEvents();
    const QString savedPath = QDir(outputDirectory).filePath("Facette1.bmp");
    check(until([&] { return storageUpdates >= 5; }), "Facette save completes in background");
    check(until([&] { return hasPicture(firstView); }), "Visible page renders latest Facette");
    cv::Mat saved = cv::imread(QFile::encodeName(savedPath).constData(), cv::IMREAD_GRAYSCALE);
    check(!saved.empty() && saved.rows == 4 && saved.cols == 8 &&
              saved.at<uchar>(0, 0) == 40 && hasPicture(firstView) && !hasPicture(secondView),
          "Offline Facette uses Camera 2 half and updates only its view");

    pair.rowRange(4, 8).setTo(70);
    cv::imwrite(QFile::encodeName(pairPath).constData(), pair);
    runtime.onSherlockCommand(command);
    QCoreApplication::processEvents();
    check(until([&] { return storageUpdates >= 6; }), "Repeated Facette save completes");
    saved = cv::imread(QFile::encodeName(savedPath).constData(), cv::IMREAD_GRAYSCALE);
    check(!saved.empty() && saved.at<uchar>(0, 0) == 70,
          "Repeated Facette command atomically overwrites BMP");

    config.runtime.facetteImageDir = temporary.filePath("other-facettes");
    runtime.reloadConfig(config);
    QCoreApplication::processEvents();
    check(until([&] { return storageUpdates >= 10; }), "Changed directory loads complete");
    check(until([&] { return !hasPicture(firstView); }), "Visible page renders cleared Facette");
    check(!hasPicture(firstView), "Changing Facette directory restores filename placeholder");
    // 曝光 ROI 显示与实际曝光共用选择规则，不需要真实相机或新测量。
    auto *main1 = page.findChild<CustomGraphicsView *>("cam1GraphicsView");
    auto *main2 = page.findChild<CustomGraphicsView *>("cam2GraphicsView");
    auto *info1 = page.findChild<QLabel *>("lblCam1Info");
    auto *info2 = page.findChild<QLabel *>("lblCam2Info");
    const auto paths = [](CustomGraphicsView *view, QColor color, Qt::PenStyle style = Qt::SolidLine) {
        QList<QGraphicsPathItem *> result;
        for (auto *item : view->scene()->items())
            if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item))
                if (path->isVisible() && path->pen().color() == color && path->pen().style() == style) result.append(path);
        return result;
    };
    pva::RuntimeSnapshot display;
    display.state = pva::RunState::Running;
    display.stage = pva::MeasurementStage::Melt;
    display.config = config;
    display.config.camera.autoExposureEnabled = false;
    display.config.measurement.autoExposureRoiCamera1 = {0, 0, 4, 4};
    display.config.measurement.autoExposureRoiCamera2 = {4, 0, 4, 4};
    display.rois.melt = std::array<double, 6>{20, 15, 4, 4, 12, 7};
    runtime.stateChanged(display);
    pva::MeasurementResult picture;
    picture.diagnostics["processing_time_ms"] = 12.345;
    for (int camera = 1; camera <= 2; ++camera) {
        const auto suffix = "_camera" + std::to_string(camera);
        picture.diagnostics["diameter_roi_mean" + suffix] = 50.0;
        picture.diagnostics["diameter_roi_maximum" + suffix] = 100.0;
        picture.diagnostics["diameter_roi_minimum" + suffix] = 10.0;
        picture.diagnostics["diameter_roi_pixel_count" + suffix] = 20;
    }
    picture.preview1 = cv::Mat(50, 80, CV_8U, cv::Scalar(20));
    picture.preview1(cv::Rect(4, 0, 4, 4)).setTo(40);
    picture.preview1(cv::Rect(18, 13, 4, 4)).setTo(100);
    picture.preview1(cv::Rect(30, 20, 4, 4)).setTo(200);
    picture.preview2 = picture.preview1.clone();
    picture.overlay1.push_back({pva::OverlayType::Polyline, {{0, 0}, {3, 0}, {3, 3}, {0, 3}}, {0, 255, 0}, 1, true, true});
    check(QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture)),
          "Display result reaches page");
    check(until([&] { return info1->text().endsWith("Mean 100.0"); }), "Offline exposure-disabled Mean uses PLC ROI");
    auto *processingLabel = page.findChild<QLabel *>("lblCycle");
    auto *timingLabel = page.findChild<QLabel *>("lblTriggerToResultTime");
    check(processingLabel && timingLabel && processingLabel->text() == "Processing time: 12.3 ms" &&
          timingLabel->text() == "Trigger to result time: --",
          "Offline shows processing time and missing trigger time");
    auto *resultsLayout = page.findChild<QVBoxLayout *>("resultsLayout");
    auto *frameDeltaLabel = page.findChild<QLabel *>("lblFrameDelta");
    check(resultsLayout && processingLabel && timingLabel && frameDeltaLabel && resultsLayout->spacing() == 8 &&
          resultsLayout->indexOf(timingLabel) == resultsLayout->indexOf(processingLabel) + 1 &&
          resultsLayout->indexOf(frameDeltaLabel) == resultsLayout->indexOf(timingLabel) + 1 &&
          !processingLabel->text().contains('\n') && !timingLabel->text().contains('\n'),
          "Times are separate consecutive layout items with the same spacing as other results");
    picture.task.source = pva::MeasurementSource::Online;
    display.online = true;
    runtime.stateChanged(display);
    picture.diagnostics["trigger_to_result_time_ms"] = 78.987;
    QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture));
    check(until([&] { return timingLabel->text().endsWith("79.0 ms"); }), "Online shows trigger-to-result time to one decimal");
    auto *processTree = page.findChild<QTreeWidget *>("treeProcess");
    auto *detailsTabs = page.findChild<QTabWidget *>("detailsTabs");
    auto *processTab = page.findChild<QWidget *>("tabProcess");
    auto *resultsTab = page.findChild<QWidget *>("tabResults");
    check(processTree && processTree->topLevelItemCount() == 0,
          "Hidden Process tree is not populated by incoming results");
    detailsTabs->setCurrentWidget(processTab);
    int rectangleItemCount = 0;
    if (processTree && processTree->topLevelItemCount() >= 2) {
        for (int camera = 0; camera < 2; ++camera) {
            const auto *group = processTree->topLevelItem(camera);
            for (int i = 0; i < group->childCount(); ++i) {
                const auto *item = group->child(i);
                if (!item->isHidden() && (item->text(0) == "Diameter Roi Mean" || item->text(0) == "Diameter Roi Max" ||
                    item->text(0) == "Diameter Roi Min" || item->text(0) == "Diameter Roi Pixel Count")) ++rectangleItemCount;
            }
        }
    }
    check(rectangleItemCount == 8, "Process displays four English rectangle statistics in each camera group");
    const auto runtimeTimingCount = [&] {
        int count = 0;
        if (!processTree || processTree->topLevelItemCount() < 4) return count;
        auto *runtimeGroup = processTree->topLevelItem(3);
        for (int i = 0; i < runtimeGroup->childCount(); ++i) {
            auto *item = runtimeGroup->child(i);
            if (!item->isHidden() && (item->text(0) == QString("Processing time") ||
                                     item->text(0) == QString("Trigger to result time"))) ++count;
        }
        return count;
    };
    check(until([&] { return runtimeTimingCount() == 2; }), "Both times appear in Process Runtime group");
    display.online = false;
    runtime.stateChanged(display);
    check(timingLabel->text().endsWith("--") && runtimeTimingCount() == 1,
          "Switching Offline clears cached Online timing without waiting for new image");
    picture.task.source = pva::MeasurementSource::Offline;
    picture.diagnostics.erase("trigger_to_result_time_ms");
    QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture));
    check(until([&] { return timingLabel->text().endsWith("--") && runtimeTimingCount() == 1; }),
          "Offline clears previous Online timing in label and Process");
    detailsTabs->setCurrentWidget(resultsTab);
    const auto diagnosticValue = [&] {
        auto *group = processTree->topLevelItem(0);
        for (int i = 0; i < group->childCount(); ++i)
            if (group->child(i)->toolTip(0) == "diameter_roi_mean_camera1") return group->child(i)->text(1);
        return QString();
    };
    const auto oldDiagnostic = diagnosticValue();
    picture.diagnostics["diameter_roi_mean_camera1"] = 75.0;
    QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture));
    QMetaObject::invokeMethod(&page, "renderLatest", Qt::DirectConnection);
    check(diagnosticValue() == oldDiagnostic, "Results tab does not refresh hidden Process diagnostics");
    detailsTabs->setCurrentWidget(processTab);
    check(diagnosticValue() == "75.000", "Opening Process immediately shows the latest cached diagnostics");
    detailsTabs->setCurrentWidget(resultsTab);

    const auto submitDisplay = [&](double time) {
        picture.diagnostics["processing_time_ms"] = time;
        QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture));
        QMetaObject::invokeMethod(&page, "renderLatest", Qt::DirectConnection);
    };
    const QPoint wheelPosition = main1->mapFromScene(QPointF(20, 15));
    const auto beforeZoom = main1->transform();
    QWheelEvent wheel(QPointF(wheelPosition), QPointF(main1->viewport()->mapToGlobal(wheelPosition)),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(main1->viewport(), &wheel);
    check(main1->isInteracting() && main1->transform() != beforeZoom, "Wheel zoom enters display pause immediately");
    submitDisplay(20.0);
    submitDisplay(30.0);
    check(processingLabel->text() == "Processing time: 12.3 ms", "Zoom pauses frame and result display updates");
    check(until([&] { return processingLabel->text() == "Processing time: 30.0 ms"; }),
          "Zoom idle resumes only the newest result");
    const auto zoomTransform = main1->transform();
    const QPoint pressPosition = main1->mapFromScene(QPointF(20, 15));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pressPosition), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(main1->viewport(), &press);
    check(main1->isInteracting(), "Mouse drag pauses display updates");
    submitDisplay(40.0);
    check(processingLabel->text() == "Processing time: 30.0 ms", "Held drag keeps the previous frame visible");
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pressPosition), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(main1->viewport(), &release);
    check(until([&] { return processingLabel->text() == "Processing time: 40.0 ms"; }),
          "Drag release resumes pending display updates");
    check(main1->transform() == zoomTransform, "Resumed image refresh preserves the zoom transform");
    submitDisplay(12.345);
    check(info2->text().endsWith("Mean 200.0"), "Camera2 displayed Mean uses PLC offsets");
    check(paths(main1, Qt::green).size() == 1 && paths(main2, Qt::green).size() == 1 &&
          paths(main1, Qt::blue).isEmpty() && paths(main2, Qt::blue).isEmpty() &&
          paths(main1, Qt::magenta).isEmpty(), "Melt draws one exposure rectangle without duplicate PLC rectangle");
    check(paths(main1, Qt::green).size() == 1 &&
          paths(main1, Qt::green).front()->path().boundingRect() == QRectF(18.5, 13.5, 4, 4),
          "Displayed exposure rectangle matches statistics region");
    check(paths(main1, Qt::green, Qt::DashLine).size() == 1, "Dashed measurement overlay preserved");
    for (const auto stage : {pva::MeasurementStage::Dip, pva::MeasurementStage::Neck}) {
        display.stage = stage;
        display.rois.dip = std::array<double, 3>{50, 20, 10};
        runtime.stateChanged(display);
        check(info1->text().endsWith("Mean 100.0") && paths(main1, Qt::green).size() == 1,
              "Dip and Neck show ROI using cached image");
        if (stage == pva::MeasurementStage::Dip)
            check(paths(main1, Qt::magenta).size() == 1, "Dip sampling line preserved");
    }
    display.rois.melt = std::array<double, 6>{20, 15, 4, 4, 100, 0};
    runtime.stateChanged(display);
    check(info1->text().endsWith("Mean 100.0") && info2->text().endsWith("Mean 40.0"),
          "Per-camera fallback updates without new frame");
    check(paths(main1, Qt::green).size() == 1 && paths(main1, Qt::blue).isEmpty() &&
          paths(main2, Qt::blue).size() == 1 && paths(main2, Qt::green).isEmpty(),
          "Each camera independently switches PLC green and local blue");
    display.rois.melt = std::array<double, 6>{1, 1, 4, 4, 0, 0};
    runtime.stateChanged(display);
    check(paths(main1, Qt::green).size() == 1 &&
          paths(main1, Qt::green).front()->path().boundingRect() == QRectF(0.5, 0.5, 3, 3),
          "Partial PLC rectangle displayed clipped");
    display.rois.melt = std::array<double, 6>{200, 200, 4, 4, 0, 0};
    display.config.measurement.autoExposureRoiCamera1 = {30, 20, 4, 4};
    runtime.stateChanged(display);
    check(info1->text().endsWith("Mean 200.0"), "Fallback parameter update refreshes cached Mean");
    check(paths(main1, Qt::blue).size() == 1 && paths(main1, Qt::green).isEmpty(),
          "Local fallback renders only blue");
    display.config.measurement.autoExposureRoiCamera1 = {200, 200, 4, 4};
    runtime.stateChanged(display);
    check(info1->text().endsWith("Mean --") && paths(main1, Qt::blue).isEmpty() && paths(main1, Qt::green).isEmpty(),
          "Invalid PLC and config region clears rectangle and Mean");
    display.rois.melt = std::array<double, 6>{20, 15, 4, 4, 12, 7};
    display.online = true;
    runtime.stateChanged(display);
    check(info1->text().endsWith("Mean 100.0"), "Online shows same selected Mean with exposure disabled");
    check(paths(main1, Qt::green).size() == 1 && paths(main1, Qt::blue).isEmpty(),
          "Restored PLC ROI switches back to green without new frame");
    for (const auto stage : {pva::MeasurementStage::Crown, pva::MeasurementStage::Body}) {
        display.stage = stage;
        runtime.stateChanged(display);
        check(info1->text().endsWith("Mean --") && info2->text().endsWith("Mean --") &&
              paths(main1, Qt::blue).isEmpty() && paths(main2, Qt::blue).isEmpty() &&
              paths(main1, Qt::green).isEmpty() && paths(main2, Qt::green).isEmpty(),
              "Crown and Body clear exposure overlay and Mean");
    }
    const std::array<const char *, 5> stageButtons{"btnStageMelt", "btnStageDip", "btnStageNeck", "btnStageCrown", "btnStageBody"};
    const std::array<pva::MeasurementStage, 5> stages{pva::MeasurementStage::Melt, pva::MeasurementStage::Dip,
        pva::MeasurementStage::Neck, pva::MeasurementStage::Crown, pva::MeasurementStage::Body};
    for (const auto &theme : {"py_dracula_dark.qss", "py_dracula_light.qss"}) {
        QFile stylesheet(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteDir().filePath(QString("themes/") + theme));
        check(stylesheet.open(QIODevice::ReadOnly), "Load actual theme for stage color regression");
        app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
        for (const bool online : {false, true}) {
            display.online = online;
            for (size_t i = 0; i < stages.size(); ++i) {
                display.stage = stages[i];
                runtime.stateChanged(display);
                QCoreApplication::processEvents();
                auto *button = page.findChild<QPushButton *>(stageButtons[i]);
                check(button && button->isChecked() && button->isEnabled() == !online,
                      "All five stage buttons track stage and Online state");
                if (button) {
                    const auto pixels = button->grab().toImage();
                    bool green = false;
                    for (int y = 0; y < pixels.height() && !green; ++y)
                        for (int x = 0; x < pixels.width(); ++x)
                            if (pixels.pixelColor(x, y) == QColor(54, 138, 90)) { green = true; break; }
                    check(green, "Selected stage stays green in both themes, including disabled Online buttons");
                }
                for (size_t j = 0; j < stages.size(); ++j)
                    check(page.findChild<QPushButton *>(stageButtons[j])->isChecked() == (i == j),
                          "Only current stage button is checked");
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
