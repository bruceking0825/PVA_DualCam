#include "measurement_worker.hpp"
#include "plc_session.hpp"
#include "state_store.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <opencv2/imgproc.hpp>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready, int timeout = 4500) {
    QElapsedTimer clock; clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(2);
    }
    return ready();
}
void pump(int ms) {
    QElapsedTimer clock; clock.start();
    until([&] { return clock.elapsed() >= ms; }, ms + 100);
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const auto plcPath = dir.filePath("plc.json");
    const auto measurementPath = dir.filePath("measurement.json");
    pva::PlcSession plc;
    plc.setStatePath(plcPath);
    plc.requestStateSave(pva::MeasurementStage::Neck);
    pva::MeasurementConfig config;
    config.measurement.brightnessMin = 1;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(0, 0, 400, 400);
    config.neck.stopSearchRatio = 1;
    cv::Mat cam(400, 400, CV_8U, cv::Scalar(20));
    cv::ellipse(cam, {250, 200}, {40, 80}, 0, 0, 360, cv::Scalar(220), 5);
    pva::MeasurementWorker worker(pva::MeasurementEngine(config), measurementPath);
    int results = 0;
    bool valid = false;
    QObject::connect(&worker, &pva::MeasurementWorker::resultReady, &app,
                     [&](const auto &result) { ++results; valid = result.valid; });
    worker.start();
    worker.submit(cam, cam, pva::MeasurementStage::Neck);
    check(until([&] { return results == 1; }) && valid, "Measurement result delivered before persistence");
    check(!QFileInfo::exists(plcPath) && !QFileInfo::exists(measurementPath), "Both files initially deferred");
    pump(700);
    plc.requestStateSave(pva::MeasurementStage::Crown);
    cam += cv::Scalar(5);
    worker.submit(cam, cam, pva::MeasurementStage::Neck);
    check(until([&] { return results == 2; }) && valid, "Latest measurement accepted within same window");
    pump(700);
    plc.requestStateSave(pva::MeasurementStage::Body);
    check(!QFileInfo::exists(plcPath) && !QFileInfo::exists(measurementPath), "Updates do not save inside three seconds");
    check(until([&] { return QFileInfo::exists(plcPath) && QFileInfo::exists(measurementPath); }, 2000),
          "Original deadline retained; idle worker and PLC both save");
    pva::PlcRuntimeState saved;
    QString error;
    check(pva::PlcRuntimeStore(plcPath).load(&saved, &error) && saved.stage == pva::MeasurementStage::Body,
          "PLC saves latest snapshot");
    const auto measured = pva::StateStore(measurementPath).load(&error);
    check(measured.validNeck && measured.neckCentersPx.has_value(), "Measurement snapshot restores");
    const auto plcTime = QFileInfo(plcPath).lastModified();
    const auto measurementTime = QFileInfo(measurementPath).lastModified();
    pump(3200);
    check(QFileInfo(plcPath).lastModified() == plcTime && QFileInfo(measurementPath).lastModified() == measurementTime,
          "Clean state is not rewritten");
    plc.requestStateSave(pva::MeasurementStage::Dip);
    plc.stop();
    check(pva::PlcRuntimeStore(plcPath).load(&saved, &error) && saved.stage == pva::MeasurementStage::Dip,
          "PLC stop flushes immediately");
    worker.submit(cam, cam, pva::MeasurementStage::Neck);
    check(until([&] { return results == 3; }), "Final measurement completed");
    worker.stop(); worker.wait();
    check(QFileInfo(measurementPath).lastModified() != measurementTime, "Worker stop flushes immediately");

    const auto blockedPath = dir.filePath("blocked.json");
    QDir().mkpath(blockedPath);
    pva::PlcSession retry;
    retry.setStatePath(blockedPath);
    QStringList errors;
    QObject::connect(&retry, &pva::PlcSession::logMessage, &app, [&](const auto &message) { errors.append(message); });
    retry.requestStateSave(pva::MeasurementStage::Neck);
    const auto blockedMeasurementPath = dir.filePath("blocked_measurement.json");
    QDir().mkpath(blockedMeasurementPath);
    pva::MeasurementWorker retryWorker(pva::MeasurementEngine(config), blockedMeasurementPath);
    QStringList measurementErrors;
    QObject::connect(&retryWorker, &pva::MeasurementWorker::persistenceFailed, &app,
                     [&](const auto &message) { measurementErrors.append(message); });
    retryWorker.start();
    retryWorker.submit(cam, cam, pva::MeasurementStage::Neck);
    check(until([&] { return !errors.isEmpty(); }), "Deferred save failure logged");
    check(until([&] { return !measurementErrors.isEmpty(); }), "Measurement failure logged while idle");
    check(!measurementErrors.isEmpty() && measurementErrors.back().contains(blockedMeasurementPath),
          "Measurement failure includes file path");
    check(errors.back().contains(blockedPath) && errors.back().contains("failed"), "Error includes path and failed step");
    QDir().rmdir(blockedPath);
    QDir().rmdir(blockedMeasurementPath);
    retry.requestStateSave(pva::MeasurementStage::Body);
    check(until([&] { return QFileInfo(blockedPath).isFile(); }), "Failed snapshot retries after three seconds");
    check(until([&] { return QFileInfo(blockedMeasurementPath).isFile(); }), "Measurement retries without new frames");
    retryWorker.stop(); retryWorker.wait();
    check(pva::PlcRuntimeStore(blockedPath).load(&saved, &error) && saved.stage == pva::MeasurementStage::Body,
          "Retry uses newest snapshot");
    const auto switchedPath = dir.filePath("switched.json");
    retry.requestStateSave(pva::MeasurementStage::Crown);
    retry.setStatePath(switchedPath);
    check(pva::PlcRuntimeStore(blockedPath).load(&saved, &error) && saved.stage == pva::MeasurementStage::Crown,
          "Path switch flushes old path");
    pump(100);
    check(!QFileInfo::exists(switchedPath), "Old snapshot does not leak to new path");
    retry.requestStateSave(pva::MeasurementStage::Dip);
    retry.stop();
    check(pva::PlcRuntimeStore(switchedPath).load(&saved, &error) && saved.stage == pva::MeasurementStage::Dip,
          "New path stores subsequent state");
    const auto rejectedPath = dir.filePath("rejected.json");
    const auto cleanPath = dir.filePath("clean.json");
    QDir().mkpath(rejectedPath);
    retry.setStatePath(rejectedPath);
    QList<QByteArray> replies;
    QObject::connect(&retry, &pva::PlcSession::payloadReady, &app,
                     [&](const auto &payload) { replies.append(payload); });
    retry.handleControlCommand({"thr_rel", {}, "thr_rel"}, pva::MeasurementStage::Neck, config);
    check(!replies.isEmpty() && replies.back() == "thr_rel=ok",
          "PLC settings reply immediately even when disk will fail");
    const auto errorCount = errors.size();
    retry.setStatePath(cleanPath);
    check(errors.size() > errorCount, "Old path flush failure reported before switching");
    pump(3100);
    check(!QFileInfo::exists(cleanPath),
          "Failed old snapshot neither leaks to new path nor rolls back current settings");
    return failures ? 1 : 0;
}
