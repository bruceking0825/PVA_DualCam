#include "page_home.hpp"
#include "app_signals.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <iostream>
#include <vector>
#ifdef Q_OS_WIN
#include <Windows.h>
#include <Psapi.h>
#endif

// 不占用硬件、PLC 端口或用户状态文件。参数为持续秒数，默认十分钟。
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int seconds = argc > 1 ? std::max(1, QString::fromLocal8Bit(argv[1]).toInt()) : 600;
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    cv::Mat camera(1024, 1024, CV_8U, cv::Scalar(20));
    cv::ellipse(camera, {650, 500}, {150, 300}, 0, 0, 360, cv::Scalar(220), 5);
    cv::Mat pair; cv::vconcat(camera, camera, pair);
    cv::imwrite(QFile::encodeName(directory.filePath("pair.bmp")).constData(), pair);
    pva::MeasurementConfig config;
    config.runtime.offlineImageDir = directory.path();
    config.runtime.stateFile = directory.filePath("state.json");
    config.runtime.facetteImageDir = directory.filePath("facettes");
    config.runtime.loopIntervalMs = 50;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = {0,0,1024,1024};
    config.measurement.brightnessMin = 1;
    config.neck.stopSearchRatio = 1;
#ifndef PVA_LEGACY_UI_BENCHMARK
    qRegisterMetaType<pva::RuntimeSnapshot>("pva::RuntimeSnapshot");
#endif
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    int results = 0;
#ifndef PVA_LEGACY_UI_BENCHMARK
    QThread thread;
    auto *runtime = new pva::RuntimeController(config, nullptr, false);
    runtime->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, runtime, &QObject::deleteLater);
    pva::PageHome page(config, *runtime);
    page.resize(1200, 900); page.show();
    QObject::connect(runtime, &pva::RuntimeController::resultReady, &app, [&](const auto &) { ++results; });
    QObject::connect(&thread, &QThread::started, runtime, [runtime] { runtime->initialize(); runtime->startRuntime(false); });
    thread.start();
#else
    // 仅用于旧提交的性能对比，不修改旧版业务流程。
    pva::PageHome page(config);
    page.resize(1200, 900); page.show();
    QObject::connect(&pva::AppSignals::instance(), &pva::AppSignals::status, &app,
        [&](const QString &source, const QString &, const QString &, const QString &message) {
            if (source == "Measurement" && message == "Neck measurement updated") ++results;
        });
    QMetaObject::invokeMethod(&page, "toggleRuntime", Qt::DirectConnection);
#endif
    std::vector<double> lateness;
    std::vector<quint64> memory;
    QElapsedTimer clock; clock.start();
    qint64 previous = clock.nsecsElapsed();
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
        const auto now = clock.nsecsElapsed();
        if (clock.elapsed() > 1000) lateness.push_back(std::max(0.0, (now-previous)/1e6-10.0));
        previous = now;
    });
    heartbeat.start(10);
    QTimer sample;
    QObject::connect(&sample, &QTimer::timeout, &app, [&] {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            memory.push_back(counters.WorkingSetSize);
#endif
    });
    sample.start(1000);
    QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    app.exec();
#ifndef PVA_LEGACY_UI_BENCHMARK
    QMetaObject::invokeMethod(runtime, "stopRuntime", Qt::BlockingQueuedConnection);
    thread.quit(); thread.wait();
#else
    QMetaObject::invokeMethod(&page, "toggleRuntime", Qt::DirectConnection);
#endif
    std::sort(lateness.begin(), lateness.end());
    const double p95 = lateness.empty() ? 0.0 : lateness[size_t((lateness.size()-1)*0.95)];
    std::cout << "duration_s=" << seconds << " results=" << results << " event_delay_p95_ms=" << p95;
    if (!memory.empty()) std::cout << " working_set_first_mb=" << memory.front()/1048576.0
        << " working_set_last_mb=" << memory.back()/1048576.0
        << " working_set_max_mb=" << *std::max_element(memory.begin(), memory.end())/1048576.0;
    std::cout << std::endl;
    return results > 0 && p95 <= 50.0 ? 0 : 1;
}
