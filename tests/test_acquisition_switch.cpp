#include "runtime_controller.hpp"
#include "camera_service.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <opencv2/imgcodecs.hpp>
#include <functional>
#include <iostream>

namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready, int timeout = 6500) {
    QElapsedTimer clock; clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(2);
    }
    return ready();
}
struct Fixture {
    QTemporaryDir directory;
    pva::MeasurementConfig config;
    std::unique_ptr<pva::RuntimeController> runtime;
    pva::RuntimeSnapshot state;
    QList<QByteArray> replies;
    QList<quint64> starts, stops;
    int triggers{};
    Fixture(bool plcOnly = false) {
        config.runtime.stateFile = directory.filePath("measurement_state.json");
        config.runtime.offlineImageDir = directory.path();
        config.runtime.facetteImageDir = directory.filePath("facettes");
        config.runtime.disableCameraForPlcTest = plcOnly;
        pva::PlcRuntimeState saved; saved.acquisitionEnabled = false;
        pva::PlcRuntimeStore(directory.filePath("plc_runtime_state.json")).save(saved);
        runtime = std::make_unique<pva::RuntimeController>(config, nullptr, false);
        QObject::connect(runtime.get(), &pva::RuntimeController::stateChanged, runtime.get(), [&](const auto &s) { state = s; });
        QObject::connect(runtime.get(), &pva::RuntimeController::payloadReady, runtime.get(), [&](const auto &r) { replies.append(r); });
        QObject::connect(runtime.get(), &pva::RuntimeController::onlineCameraStartRequested, runtime.get(), [&](quint64 id) { starts.append(id); });
        QObject::connect(runtime.get(), &pva::RuntimeController::onlineCameraStopRequested, runtime.get(), [&](quint64 id) { stops.append(id); });
        QObject::connect(runtime.get(), &pva::RuntimeController::onlineCameraTriggerRequested, runtime.get(), [&](quint64) { ++triggers; });
        runtime->initialize();
    }
    void command(const QString &name) { runtime->onSherlockCommand({name, {}, name.toLatin1()}); }
    QByteArray getAcquisition() { command("acq_get"); return replies.back(); }
    ~Fixture() { runtime->shutdown(); }
};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    {
        Fixture f;
        f.runtime->onSherlockCommand({"acq_on_", {"100"}, "acq_on_=100"});
        check(f.replies.back().startsWith("err_prm=") && f.starts.isEmpty(), "Acquisition parameters rejected");
        f.replies.clear();
        f.command("acq_on_");
        check(f.starts.size() == 1 && f.replies.isEmpty() && f.state.state == pva::RunState::Starting,
              "Start waits for camera confirmation without reply");
        check(f.getAcquisition() == "acq_get=0", "Pending start retains last confirmed acquisition flag");
        f.command("pic_fac1"); check(f.replies.back() == "err_exc=runtime switching", "Facet rejected during switching");
        f.command("mlt_msr"); check(f.replies.back() == "err_exc=runtime switching", "Measurement rejected during switching");
        f.command("acq_on_"); check(f.replies.back() == "err_exc=runtime switching", "Duplicate pending start rejected");
        const auto before = f.replies.size();
        f.runtime->onOnlineCameraStarted(f.starts.back() + 1);
        check(f.replies.size() == before && f.state.state == pva::RunState::Starting, "Wrong start session ignored");
        f.runtime->onOnlineCameraStarted(f.starts.back());
        check(f.replies.back() == "acq_on_=ok" && f.state.online && f.state.state == pva::RunState::Running &&
              f.triggers == 0, "Online confirmation replies without shooting");
        check(f.getAcquisition() == "acq_get=1", "Successful start enables acquisition");
        f.command("acq_on_");
        check(f.replies.back() == "acq_on_=ok" && f.starts.size() == 1, "Stable Online start is idempotent");
        f.command("dia_msr");
        const int oldCount = f.replies.size();
        f.command("acq_off");
        check(f.stops.size() == 1 && f.state.state == pva::RunState::Stopping &&
              f.replies.size() == oldCount + 1 && f.replies.back() == "exe_err=Commn err",
              "Stop cancels measurement but waits to acknowledge acquisition");
        check(f.getAcquisition() == "acq_get=1", "Pending stop retains previously confirmed flag");
        const auto stoppingCount = f.replies.size();
        f.runtime->onOnlineCameraStopped(f.stops.back() + 1);
        check(f.replies.size() == stoppingCount, "Wrong close request ignored");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        check(f.replies.back() == "acq_off=ok" && !f.state.online && f.state.state == pva::RunState::Stopped,
              "Close completion confirms stopped Offline");
        check(f.getAcquisition() == "acq_get=0", "Successful stop disables acquisition");
        f.command("acq_off");
        check(f.replies.back() == "acq_off=ok" && f.stops.size() == 1, "Stable Offline stop is idempotent");
        QCoreApplication::processEvents();
        check(f.state.state == pva::RunState::Stopped, "Offline does not auto start image loop");
        auto changed = f.config;
        changed.runtime.loopIntervalMs += 50;
        f.command("acq_on_"); f.runtime->onOnlineCameraStarted(f.starts.back());
        f.runtime->reloadConfig(changed);
        const auto restartCount = f.starts.size();
        check(f.state.state == pva::RunState::Stopping, "Reload waits for close before restarting");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        check(f.starts.size() == restartCount + 1, "Reload restarts after matching close confirmation");
        f.runtime->onOnlineCameraStarted(f.starts.back());
        f.runtime->stopRuntime(); f.runtime->onOnlineCameraStopped(f.stops.back());
    }
    {
        Fixture f;
        f.command("acq_on_");
        const auto cancelledSession = f.starts.back();
        f.command("acq_off");
        check(f.replies.size() == 1 && f.replies.front() == "err_exc=acquisition cancelled" &&
              f.stops.size() == 1, "Stop cancels unconfirmed start exactly once");
        f.runtime->onOnlineCameraStarted(cancelledSession);
        check(f.state.state == pva::RunState::Stopping && f.replies.size() == 1, "Late cancelled start cannot resume");
        f.command("acq_off"); check(f.replies.back() == "err_exc=runtime switching", "Duplicate pending stop rejected");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        check(f.replies.back() == "acq_off=ok" && f.getAcquisition() == "acq_get=0", "Cancelled start ends Offline");
    }
    {
        Fixture f;
        f.command("acq_on_");
        f.runtime->onOnlineCameraFailed("Camera2 startup failed", f.starts.back());
        check(f.replies.size() == 1 && f.replies.front().startsWith("err_exc=") &&
              !f.replies.contains("acq_on_=ok") && f.stops.size() == 1, "Startup failure never acknowledges success");
        f.command("acq_on_"); check(f.replies.back() == "err_exc=runtime switching", "Failed startup blocks until cleanup confirmed");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        f.command("acq_on_"); f.runtime->onOnlineCameraStarted(f.starts.back());
        f.command("acq_off");
        const auto failedStop = f.stops.back();
        f.runtime->onOnlineCameraStopFailed(failedStop, "simulated close failure");
        check(f.replies.back().startsWith("err_exc=Camera close failed") && f.stops.size() == 3,
              "Close failure replies error and requests one safety close");
        const auto count = f.replies.size();
        f.runtime->onOnlineCameraStopped(failedStop);
        check(f.replies.size() == count && f.state.state == pva::RunState::Faulted, "Old failed stop cannot confirm safety close");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        check(f.replies.size() == count && !f.state.online, "Safety close never sends extra acquisition success");
    }
    {
        Fixture f;
        f.command("acq_on_");
        check(until([&] { return !f.replies.isEmpty(); }), "Real five-second startup timeout fires");
        check(f.replies.back().startsWith("err_exc=acquisition switch timed out") && f.stops.size() == 1,
              "Timeout replies once and schedules safety close");
        const auto count = f.replies.size();
        f.runtime->onOnlineCameraStarted(f.starts.back());
        check(f.replies.size() == count && f.state.state != pva::RunState::Running, "Timed-out start ignored");
        f.command("acq_on_"); check(f.replies.back() == "err_exc=runtime switching", "New start blocked during timeout cleanup");
        f.runtime->onOnlineCameraStopped(f.stops.back());
        f.command("acq_on_"); f.runtime->onOnlineCameraStarted(f.starts.back());
        f.command("acq_off");
        check(until([&] { return f.replies.back().startsWith("err_exc=acquisition switch timed out"); }),
              "Real five-second stop timeout fires");
        const auto timedOutStop = f.stops.back();
        const auto countAfterTimeout = f.replies.size();
        f.runtime->onOnlineCameraStopped(timedOutStop);
        check(f.replies.size() == countAfterTimeout && f.state.state == pva::RunState::Stopped,
              "Late stop completes cleanup without ok");
    }
    {
        Fixture f(true);
        f.command("acq_on_");
        check(f.replies.back() == "acq_on_=ok" && f.starts.isEmpty() && f.state.online, "PLC test mode starts logically");
        f.command("acq_off");
        check(f.replies.back() == "acq_off=ok" && f.stops.isEmpty() && !f.state.online &&
              f.state.state == pva::RunState::Stopped, "PLC test mode stops logically");
    }
    {
        Fixture f;
        cv::Mat pair(20, 20, CV_8U, cv::Scalar(100));
        cv::imwrite(QFile::encodeName(f.directory.filePath("pair.bmp")).constData(), pair);
        f.runtime->reloadConfig(f.config);
        f.runtime->startRuntime(false);
        const int imageIndex = f.state.imageIndex;
        check(f.state.state == pva::RunState::Running && !f.state.online, "Offline fixture runs");
        f.command("acq_on_");
        check(f.state.state == pva::RunState::Starting && f.starts.size() == 1, "PLC start ends offline loop and opens Online");
        f.runtime->onOnlineCameraStarted(f.starts.back());
        f.command("acq_off"); f.runtime->onOnlineCameraStopped(f.stops.back());
        check(f.state.imageIndex == imageIndex && f.state.state == pva::RunState::Stopped && !f.state.online,
              "PLC stop preserves current image without offline measurement");
    }
    {
        pva::CameraService cameras(pva::MeasurementConfig{});
        QList<quint64> confirmed;
        QObject::connect(&cameras, &pva::CameraService::onlineCameraStopped, &app, [&](quint64 id) { confirmed.append(id); });
        cameras.stopOnlineCameras(91); cameras.stopOnlineCameras(92);
        check(confirmed == QList<quint64>{91, 92}, "No online stream still confirms each close request ID");
    }
    return failures ? 1 : 0;
}
