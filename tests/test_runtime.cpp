#include "runtime_controller.hpp"
#include "offline_image_source.hpp"
#include "image_storage.hpp"
#include "dalsa_camera.hpp"
#include "camera_service.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QDir>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <functional>

namespace {
int failures = 0;
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready, int timeout = 3000) {
    QElapsedTimer clock; clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return ready();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    cv::Mat cam(400, 400, CV_8U, cv::Scalar(20));
    cv::ellipse(cam, {250, 200}, {40, 80}, 0, 0, 360, cv::Scalar(220), 5);
    cv::Mat composite; cv::vconcat(cam, cam, composite);
    const auto path = dir.filePath("pair.bmp");
    cv::imwrite(QFile::encodeName(path).constData(), composite);
    cv::imwrite(QFile::encodeName(dir.filePath("pair2.bmp")).constData(), composite);
    cv::imwrite(QFile::encodeName(dir.filePath("pair3.bmp")).constData(), composite);
    pva::OfflineImageSource source;
    const auto first = source.load(path), cached = source.load(path);
    check(first.first.data == cached.first.data, "Offline source reuses current decoded image");
    check(first.first.size() == cam.size() && cv::norm(first.first, cam) == 0,
          "Offline source preserves native direction");

    pva::MeasurementConfig config;
    config.runtime.offlineImageDir = dir.path();
   config.runtime.stateFile = dir.filePath("measurement_state.json");
    config.runtime.measurementImageDir = dir.filePath("measurement_images");
    config.runtime.facetteImageDir = dir.filePath("facettes");
    config.runtime.loopIntervalMs = 50;
    config.measurement.brightnessMin = 1;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(0,0,400,400);
    config.neck.stopSearchRatio = 1;
    pva::RuntimeController runtime(config, nullptr, false);
    pva::RuntimeSnapshot snapshot;
    int results = 0;
    pva::MeasurementResult last;
    QList<QByteArray> replies;
    QObject::connect(&runtime, &pva::RuntimeController::stateChanged, &app, [&](const auto &s) { snapshot = s; });
    QObject::connect(&runtime, &pva::RuntimeController::resultReady, &app, [&](const auto &r) { ++results; last = r; });
    QObject::connect(&runtime.plcSession(), &pva::PlcSession::payloadReady, &app, [&](const auto &r) { replies.append(r); });
    runtime.initialize();
    runtime.selectStage(pva::MeasurementStage::Neck);
    runtime.stepImage(1);
    runtime.stepImage(1);
    check(snapshot.imageIndex == 2, "Relative navigation uses controller index for rapid clicks");
    runtime.setImageIndex(0);
    runtime.startRuntime(false);
    check(until([&]{return results > 0;}), "Headless offline runtime produces results");
    check(last.valid, "Headless offline neck measurement is valid");
    check(last.diagnostics.contains("processing_time_ms") &&
          !last.diagnostics.contains("trigger_to_result_time_ms") && !last.task.triggerStarted,
          "Offline measures processing time only");
    check(until([&] { return QFile::exists(QDir(config.runtime.measurementImageDir).filePath("image1.bmp")) &&
                            QFile::exists(QDir(config.runtime.measurementImageDir).filePath("image2.bmp")); }),
          "Headless accepted results save both images without a page refresh");
    check(last.task.runId != 0 && last.task.requestId != 0 && last.task.configurationVersion != 0,
          "Measurement carries run, request and configuration identity");
    const auto originalVersion = last.task.configurationVersion;
    runtime.onSherlockCommand({"dia_thr", {"2500"}, "dia_thr=2500"});
    check(snapshot.config.neck.gradientThresholdPercentCamera1 == config.neck.gradientThresholdPercentCamera1,
          "Disabled PLC threshold override preserves manual configuration");
    runtime.reloadConfig(config);
    check(snapshot.config.neck.gradientThresholdPercentCamera1 == config.neck.gradientThresholdPercentCamera1,
          "Base hot reload preserves manual threshold");
    check(until([&] { return last.task.configurationVersion > originalVersion; }),
          "Configuration update tags subsequent results");
    const auto stale = last;
    runtime.selectStage(pva::MeasurementStage::Body);
    const int before = results;
    runtime.acceptResult(stale);
    check(results == before, "Stage switch rejects stale result");
    runtime.stopRuntime();
    runtime.acceptResult(last);
    check(results == before, "Stopped runtime rejects queued results");

    // 模拟在线相机，不创建任何 QWidget，也不访问硬件或占用 PLC 端口。
    config.runtime.continuousSaveEnabled = true;
    config.runtime.continuousSaveDir = dir.filePath("online_history");
    runtime.reloadConfig(config);
    runtime.selectStage(pva::MeasurementStage::Neck);
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraStartRequested, &app,
                     [&] { runtime.onOnlineCameraStarted(); });
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraStopRequested, &app,
                     [&](quint64 request) { runtime.onOnlineCameraStopped(request); });
    bool blankFrames = false;
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraTriggerRequested, &app,
                     [&](quint64 generation) {
        const cv::Mat frame = blankFrames ? cv::Mat(cam.size(), CV_8U, cv::Scalar(0)) : cam;
        QTimer::singleShot(30, &app, [&, generation, frame] {
            runtime.onCameraFrame(pva::CameraRole::Cam2, frame, 1000000, generation);
            runtime.onCameraFrame(pva::CameraRole::Cam1, frame, 1000001, generation);
        });
    });
    runtime.startRuntime(true);
    int facets = 0;
    QObject::connect(&runtime, &pva::RuntimeController::onlineFacetTriggerRequested, &app,
                     [&](int request) { QTimer::singleShot(0, &app, [&, request] { runtime.onFacetFrame(request, cam); }); });
    QObject::connect(&runtime, &pva::RuntimeController::facetteReady, &app,
                     [&](int, const cv::Mat &image) { if (!image.empty()) ++facets; });
    runtime.onSherlockCommand({"dia_msr", {}, "dia_msr"});
    runtime.onSherlockCommand({"pic_fac1", {}, "pic_fac1"});
    runtime.onSherlockCommand({"dia_msr", {}, "dia_msr"});
    check(replies.contains("exe_err=measurement busy"), "Concurrent PLC request is explicitly rejected");
    check(until([&] { for (const auto &r : replies) if (r.startsWith("dia=")) return true; return false; }),
          "Simulated online pair produces PLC diameter reply");
    check(until([&] { return facets == 1; }), "Interleaved Facette finishes independently of measurement");
    check(last.task.triggerStarted && last.diagnostics.contains("trigger_to_result_time_ms") &&
          last.diagnostics.at("trigger_to_result_time_ms").toDouble() >= 25.0 &&
          last.diagnostics.at("trigger_to_result_time_ms").toDouble() >= last.diagnostics.at("processing_time_ms").toDouble(),
          "Trigger-to-result time includes delayed capture and processing");
    check(until([&] { return QDir(config.runtime.continuousSaveDir).entryList({"*.png"}, QDir::Files).size() == 1; }),
          "Online PLC measurement also saves a combined PNG");
    config.runtime.continuousSaveEnabled = false;
    runtime.reloadConfig(config);
    blankFrames = true;
    const int beforeFailedMeasurement = results;
    runtime.onSherlockCommand({"dia_msr", {}, "dia_msr"});
    check(until([&] { return results > beforeFailedMeasurement; }) && !last.valid &&
          last.diagnostics.contains("trigger_to_result_time_ms"), "Failed detection also reports trigger-to-result time");
    const auto failedTrigger = last.task.triggerStarted;
    blankFrames = false;
    const int beforeNextMeasurement = results;
    runtime.onSherlockCommand({"dia_msr", {}, "dia_msr"});
    check(until([&] { return results > beforeNextMeasurement; }) && last.valid &&
          last.task.triggerStarted && failedTrigger && *last.task.triggerStarted > *failedTrigger,
          "Consecutive measurement starts its own trigger clock");
    runtime.onOnlineCaptureFailed(pva::CameraRole::Cam1, "simulated failure");
    check(snapshot.state == pva::RunState::Faulted, "Camera failure moves runtime to Faulted");
    runtime.startRuntime(false);
    const int restarted = results;
    check(until([&] { return results > restarted; }), "Runtime restarts after camera failure");
    runtime.onOnlineCameraFailed("stale camera error", last.task.runId - 1);
    check(snapshot.state == pva::RunState::Running, "Previous camera session cannot fault restarted runtime");
    runtime.stopRuntime();

    pva::PlcSession session(nullptr, 20);
    int timeouts = 0, payloads = 0;
    QObject::connect(&session, &pva::PlcSession::timedOut, &app, [&] { ++timeouts; });
    QObject::connect(&session, &pva::PlcSession::payloadReady, &app, [&](const auto &) { ++payloads; });
    check(session.begin("dia_msr", 10), "PLC session accepts first request");
    check(!session.begin("dia_msr", 11), "PLC session never replaces pending request");
    pva::MeasurementResult wrong; wrong.generation = 9;
    session.complete(wrong);
    check(session.busy() && payloads == 0, "Old generation cannot complete current PLC request");
    check(until([&] {return timeouts == 1;}), "PLC timeout releases pending request");
    check(!session.busy() && payloads == 1, "Timeout sends exactly one reply");
    {
        pva::MeasurementWorker timingWorker{pva::MeasurementEngine(config)};
        std::optional<pva::MeasurementResult> measured;
        QObject::connect(&timingWorker, &pva::MeasurementWorker::resultReady, &app,
                         [&](const auto &result) { measured = result; });
        pva::MeasurementTaskInfo task;
        task.source = pva::MeasurementSource::Online;
        task.triggerStarted = std::chrono::steady_clock::now();
        timingWorker.submit(cam, cam, pva::MeasurementStage::Melt, 1, task);
        QThread::msleep(40); // queued task waits before measurement thread starts
        timingWorker.start();
        check(until([&] { return measured.has_value(); }) &&
              measured->diagnostics.at("trigger_to_result_time_ms").toDouble() >= 35.0,
              "Trigger-to-result timing includes measurement queue wait");
        measured.reset(); task.triggerStarted.reset();
        timingWorker.submit(cam, cam, pva::MeasurementStage::Melt, 2, task);
        check(until([&] { return measured.has_value(); }) &&
              !measured->diagnostics.contains("trigger_to_result_time_ms"),
              "Online task without trigger time reports no fabricated duration");
        measured.reset(); task.source = pva::MeasurementSource::Offline;
        task.triggerStarted = std::chrono::steady_clock::now();
        timingWorker.submit(cam, cam, pva::MeasurementStage::Melt, 3, task);
        check(until([&] { return measured.has_value(); }) &&
              !measured->diagnostics.contains("trigger_to_result_time_ms"),
              "Offline ignores stale optional trigger timestamp");
        timingWorker.stop(); timingWorker.wait();
    }
    pva::ImageStorage storage;
    int rejected = 0, savedImages = 0;
    QObject::connect(&storage, &pva::ImageStorage::failed, &app, [&](const QString &) { ++rejected; });
    QObject::connect(&storage, &pva::ImageStorage::saved, &app, [&](const QString &) { ++savedImages; });
    for (int i = 0; i < 20; ++i) storage.save(dir.filePath("bounded"), i % 4 + 1, cam);
    check(rejected == 4, "Full storage queue explicitly rejects excess requests");
    storage.flush();
    check(savedImages == 16, "Accepted storage requests drain on shutdown");
    {
        auto historyConfig = config;
        historyConfig.runtime.continuousSaveDir = dir.filePath("history");
        historyConfig.runtime.measurementImageDir = dir.filePath("history_bmp");
        historyConfig.runtime.stateFile = dir.filePath("history_state.json");
        historyConfig.runtime.loopIntervalMs = 50;
        pva::RuntimeController history(historyConfig, nullptr, false);
        int historyResults = 0;
        QObject::connect(&history, &pva::RuntimeController::resultReady, &app, [&](const auto &) { ++historyResults; });
        const auto count = [&] { return QDir(historyConfig.runtime.continuousSaveDir).entryList({"*.png"}, QDir::Files).size(); };
        history.initialize(); history.startRuntime(false);
        check(until([&] { return historyResults > 0; }) && count() == 0, "Continuous save is off by default");
        historyConfig.runtime.continuousSaveEnabled = true;
        history.reloadConfig(historyConfig);
        check(until([&] { return count() == 1; }), "Enabling continuous save immediately saves next result");
        QElapsedTimer rate; rate.start();
        until([&] { return rate.elapsed() >= 550; });
        check(count() == 1, "Repeated results within one second do not create more PNGs");
        check(until([&] { return count() >= 2; }), "Later result saves when interval has elapsed");
        historyConfig.runtime.continuousSaveIntervalS = 60;
        historyConfig.runtime.continuousSaveDir = dir.filePath("history_changed");
        history.reloadConfig(historyConfig);
        check(until([&] { return count() == 1; }), "Path and interval changes reset throttle for next result");
        historyConfig.runtime.continuousSaveEnabled = false;
        history.reloadConfig(historyConfig);
        rate.restart(); until([&] { return rate.elapsed() >= 1100; });
        check(count() == 1, "Disabling continuous save prevents further PNGs");
        const auto invalidDirectory = dir.filePath("history_blocked");
        QFile invalidFile(invalidDirectory);
        check(invalidFile.open(QIODevice::WriteOnly), "Create failed history directory fixture");
        invalidFile.close();
        int historyErrors = 0;
        QObject::connect(&history, &pva::RuntimeController::logReady, &app, [&](const QString &line, bool) {
            if (line.contains("Cannot create continuous image directory")) ++historyErrors;
        });
        historyConfig.runtime.continuousSaveEnabled = true;
        historyConfig.runtime.continuousSaveIntervalS = 1;
        historyConfig.runtime.continuousSaveDir = invalidDirectory;
        history.reloadConfig(historyConfig);
        check(until([&] { return historyErrors == 1; }), "Failed continuous save records error");
        rate.restart(); until([&] { return rate.elapsed() >= 550; });
        check(historyErrors == 1, "Failed saves are also limited to configured interval");
        check(until([&] { return historyErrors >= 2; }), "Failure retries on next result after interval");
        history.stopRuntime();
        historyConfig.runtime.continuousSaveEnabled = true;
        historyConfig.runtime.continuousSaveDir = dir.filePath("history_stopped");
        history.reloadConfig(historyConfig);
        rate.restart(); until([&] { return rate.elapsed() >= 1100; });
        check(count() == 0, "No new measurement means no repeated stale PNG");
        history.shutdown();
    }
    return failures ? 1 : 0;
}
