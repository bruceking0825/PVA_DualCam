#include "page_home.hpp"
#include "camera_service.hpp"
#include <QLabel>
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
    pva::RuntimeController runtime(config, nullptr, false);
    pva::PageHome page(config, runtime);
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
    check(QMetaObject::invokeMethod(&page, "onSherlockCommand", Qt::DirectConnection,
                                    Q_ARG(SherlockCommand, command)),
          "Offline Facette command reaches page");
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
    check(QMetaObject::invokeMethod(&page, "onSherlockCommand", Qt::DirectConnection,
                                    Q_ARG(SherlockCommand, command)),
          "Repeat Facette command reaches page");
    QCoreApplication::processEvents();
    check(until([&] { return storageUpdates >= 6; }), "Repeated Facette save completes");
    saved = cv::imread(QFile::encodeName(savedPath).constData(), cv::IMREAD_GRAYSCALE);
    check(!saved.empty() && saved.at<uchar>(0, 0) == 70,
          "Repeated Facette command atomically overwrites BMP");

    config.runtime.facetteImageDir = temporary.filePath("other-facettes");
    page.reloadConfig(config);
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
    picture.preview1 = cv::Mat(50, 80, CV_8U, cv::Scalar(20));
    picture.preview1(cv::Rect(4, 0, 4, 4)).setTo(40);
    picture.preview1(cv::Rect(18, 13, 4, 4)).setTo(100);
    picture.preview1(cv::Rect(30, 20, 4, 4)).setTo(200);
    picture.preview2 = picture.preview1.clone();
    picture.overlay1.push_back({pva::OverlayType::Polyline, {{0, 0}, {3, 0}, {3, 3}, {0, 3}}, {0, 255, 0}, 1, true, true});
    check(QMetaObject::invokeMethod(&page, "showResult", Qt::DirectConnection, Q_ARG(MeasurementResult, picture)),
          "Display result reaches page");
    check(until([&] { return info1->text().endsWith("Mean 100.0"); }), "Offline exposure-disabled Mean uses PLC ROI");
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
    return failures == 0 ? 0 : 1;
}
