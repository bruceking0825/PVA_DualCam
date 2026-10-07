#include "app_services.hpp"
#include "config_manager.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

// 服务装配/退出回归：不创建页面，不启动相机采集，不连接 PLC。
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    cv::Mat image(400, 400, CV_8U, cv::Scalar(20));
    cv::ellipse(image, {250, 200}, {40, 80}, 0, 0, 360, cv::Scalar(220), 5);
    cv::Mat pair; cv::vconcat(image, image, pair);
    cv::imwrite(QFile::encodeName(directory.filePath("pair.bmp")).constData(), pair);
    pva::MeasurementConfig config;
    config.runtime.offlineImageDir = directory.path();
    config.runtime.stateFile = directory.filePath("state.json");
    config.runtime.facetteImageDir = directory.filePath("facettes");
    config.runtime.connectPlcInOffline = false;
    config.runtime.loopIntervalMs = 50;
    config.measurement.brightnessMin = 1;
    config.neck.stopSearchRatio = 1;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = {0,0,400,400};
    { pva::AppServices neverStarted(config); }
    for (int cycle = 0; cycle < 3; ++cycle) {
        pva::AppServices services(config);
        QPointer<pva::RuntimeController> runtime = &services.runtime();
        QPointer<pva::CameraService> cameras = &services.cameras();
        int results = 0;
        QObject::connect(runtime, &pva::RuntimeController::resultReady, &app,
                         [&](const pva::MeasurementResult &result) { if (result.valid) ++results; });
        services.start();
        QMetaObject::invokeMethod(runtime, [runtime] { runtime->startRuntime(false); }, Qt::QueuedConnection);
        QElapsedTimer clock; clock.start();
        while (!results && clock.elapsed() < 5000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        services.stop();
        if (!results || runtime || cameras) {
            std::cerr << "Service run/teardown failed at cycle " << cycle << '\n';
            return 1;
        }
        services.stop();
        services.start(); // 已销毁的应用服务不能再次启动悬空指针。
        pva::ConfigManager::instance().batchChanged();
        QCoreApplication::processEvents();
    }
    return 0;
}
