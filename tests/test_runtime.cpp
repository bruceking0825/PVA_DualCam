#include "runtime_controller.hpp"
#include "offline_image_source.hpp"
#include "image_storage.hpp"
#include "dalsa_camera.hpp"
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
    pva::CameraManager cameraDelivery;
    cameraDelivery.reset(pva::CameraRole::Stereo);
    int delivered = 0;
    QObject::connect(&cameraDelivery, &pva::CameraManager::frameReady, &app,
                     [&](const QString &, const cv::Mat &, qint64) { ++delivered; });
    cameraDelivery.get(pva::CameraRole::Cam1)->frameReady(pva::CameraRole::Cam1, cam, 1);
    cameraDelivery.closeAll();
    QCoreApplication::processEvents();
    check(delivered == 0, "Camera close invalidates already queued SDK frames");
    cameraDelivery.get(pva::CameraRole::Cam1)->frameReady(pva::CameraRole::Cam1, cam, 2);
    check(until([&] { return delivered == 1; }), "New camera epoch accepts new frames");
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
    config.runtime.facetteImageDir = dir.filePath("facettes");
    config.runtime.loopIntervalMs = 50;
    config.runtime.connectPlcInOffline = false;
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
    QObject::connect(&runtime, &pva::RuntimeController::payloadReady, &app, [&](const auto &r) { replies.append(r); });
    runtime.initialize();
    runtime.stepImage(1);
    runtime.stepImage(1);
    check(snapshot.imageIndex == 2, "Relative navigation uses controller index for rapid clicks");
    runtime.setImageIndex(0);
    runtime.startRuntime(false);
    check(until([&]{return results > 0;}), "Headless offline runtime produces results");
    check(last.valid, "Headless offline neck measurement is valid");
    check(last.task.runId != 0 && last.task.requestId != 0 && last.task.configurationVersion != 0,
          "Measurement carries run, request and configuration identity");
    const auto originalVersion = last.task.configurationVersion;
    runtime.onSherlockCommand({"dia_thr", {"2500"}, "dia_thr=2500"});
    check(std::abs(snapshot.config.neck.gradientThresholdCamera1 - 63.75) < 1e-9,
          "PLC threshold overrides base configuration");
    runtime.reloadConfig(config);
    check(std::abs(snapshot.config.neck.gradientThresholdCamera1 - 63.75) < 1e-9,
          "Base hot reload preserves PLC override priority");
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
    runtime.selectStage(pva::MeasurementStage::Neck);
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraStartRequested, &app,
                     [&] { runtime.onOnlineCameraStarted(); });
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraTriggerRequested, &app,
                     [&](quint64 generation) {
        QTimer::singleShot(0, &app, [&, generation] {
            runtime.onCameraFrame(pva::CameraRole::Cam2, cam, 1000000, generation);
            runtime.onCameraFrame(pva::CameraRole::Cam1, cam, 1000001, generation);
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
    pva::ImageStorage storage;
    int rejected = 0, savedImages = 0;
    QObject::connect(&storage, &pva::ImageStorage::failed, &app, [&](const QString &) { ++rejected; });
    QObject::connect(&storage, &pva::ImageStorage::saved, &app, [&](const QString &) { ++savedImages; });
    for (int i = 0; i < 20; ++i) storage.save(dir.filePath("bounded"), i % 4 + 1, cam);
    check(rejected == 4, "Full storage queue explicitly rejects excess requests");
    storage.flush();
    check(savedImages == 16, "Accepted storage requests drain on shutdown");
    return failures ? 1 : 0;
}
