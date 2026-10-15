#include "camera_service.hpp"
#include "dalsa_camera.hpp"
#include "sherlock_tcp_server.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTcpServer>
#include <QThread>
#include <functional>
#include <iostream>

namespace pva {
// 只注入设备和 SDK 信号；使用生产代码的入队、批次隔离和故障处理。
struct CameraServiceTestAccess {
    static void reset(CameraService &service, const QStringList &ids) { service.resetDevices(ids); }
    static DalsaCamera *device(CameraService &service, const QString &id) { return service.camera(id); }
};
}
namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return ready();
}
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    // 不打开硬件，以 SDK 故障信号模拟单设备错误。
    pva::CameraService cameras(pva::MeasurementConfig{});
    using Access = pva::CameraServiceTestAccess;
    pva::CameraSnapshot cameraState;
    QObject::connect(&cameras, &pva::CameraService::stateChanged, &app,
        [&](const auto &state) { cameraState = state; });
    {
        Access::reset(cameras, {pva::CameraRole::Cam1});
        cameras.selectCamera(pva::CameraRole::Cam1);
        check(cameraState.healthyIds == QStringList{pva::CameraRole::Cam1}, "Enumerated camera is healthy without streaming");
        Access::reset(cameras, pva::CameraRole::Stereo);
        cameras.selectCamera(pva::CameraRole::Cam1);
        auto *device = Access::device(cameras, pva::CameraRole::Cam1);
        cv::Mat frame(8, 8, CV_8U, cv::Scalar(42));
        device->frameReady(pva::CameraRole::Cam1, frame, 1000000000);
        device->captureFailed(pva::CameraRole::Cam1, "stale capture error");
        cameras.closeAll();
        QCoreApplication::processEvents();
        check(cameras.takePreviewFrame().empty() && cameraState.healthyIds.size() == 2,
              "Close invalidates queued SDK frames and errors");
        device->frameReady(pva::CameraRole::Cam1, frame, 2000000000);
        cv::Mat preview;
        check(until([&] { if (preview.empty()) preview = cameras.takePreviewFrame(); return !preview.empty(); }) &&
              preview.at<uchar>(0, 0) == 42, "New epoch accepts SDK frames");
        device->captureFailed(pva::CameraRole::Cam1, "simulated capture error");
        check(until([&] { return !cameraState.healthyIds.contains(pva::CameraRole::Cam1); }) &&
            cameraState.healthyIds.contains(pva::CameraRole::Cam2), "Only failed camera loses health");
        cameras.closeAll();
        check(cameraState.healthyIds == QStringList{pva::CameraRole::Cam2}, "Normal close preserves health and latched fault");
    }
    pva::SherlockTcpServer server(nullptr, 0, 0);
    QStringList logs;
    bool connected{};
    int errors{}, commands{};
    QObject::connect(&server, &pva::SherlockTcpServer::logMessage, &app,
        [&](const QString &message) { logs.append(message); });
    QObject::connect(&server, &pva::SherlockTcpServer::connectionChanged, &app,
        [&](bool on) { connected = on; });
    QObject::connect(&server, &pva::SherlockTcpServer::failed, &app,
        [&](const QString &) { ++errors; });
    QObject::connect(&server, &pva::SherlockTcpServer::commandReceived, &app,
        [&](const auto &) { ++commands; });
    QString error;
    check(server.start(&error), "Service starts before measurement");
    const auto port = server.commandPort();
    const auto logCount = logs.size();
    check(server.start(&error) && logs.size() == logCount && server.commandPort() == port,
        "Repeated service start is idempotent");
    QTcpSocket command, result;
    command.connectToHost("127.0.0.1", port);
    check(until([&] { return command.state() == QAbstractSocket::ConnectedState &&
        logs.size() > logCount; }) && !connected, "Command connection alone leaves LED off");
    check(!server.sendPayload("acq_on_=ok", &error) && errors == 0, "Initial response queue is not a communication fault");
    result.connectToHost("127.0.0.1", server.resultPort());
    check(until([&] { return connected && result.bytesAvailable() > 0; }), "Dual connection lights LED and flushes queued reply");
    result.readAll();
    command.write("acq_get\r\n");
    check(until([&] { return commands == 1; }) && connected, "Valid command leaves LED on");
    command.write("=bad\r\n");
    check(until([&] { return errors > 0 && !connected &&
        result.state() == QAbstractSocket::UnconnectedState; }), "Invalid protocol clears both connections and LED");
    command.abort(); result.abort();
    command.connectToHost("127.0.0.1", server.commandPort());
    result.connectToHost("127.0.0.1", server.resultPort());
    check(until([&] { return connected; }), "Fresh dual connection restores LED");
    result.disconnectFromHost();
    check(until([&] { return !connected && command.state() == QAbstractSocket::UnconnectedState; }),
        "Either channel disconnect turns LED off and clears peer");
    check(server.commandPort() == port, "Disconnected service continues listening");
    server.stop();
    check(logs.contains("Sherlock service closed"), "Service closure logged");
    const auto stoppedLogCount = logs.size();
    server.stop();
    check(logs.size() == stoppedLogCount, "Repeated stop does not duplicate log");
    QTcpServer occupied;
    check(occupied.listen(QHostAddress::AnyIPv4, 0), "Reserve test port");
    pva::SherlockTcpServer conflict(nullptr, occupied.serverPort(), 0);
    check(!conflict.start(&error) && !error.isEmpty(), "Listen failure reports error");
    return failures ? 1 : 0;
}
