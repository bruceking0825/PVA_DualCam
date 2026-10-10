#include "camera_service.hpp"
#include "runtime_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>

namespace pva {
struct CameraServiceTestAccess {
    static void online(CameraService &service, bool enabled) { service.streamOwner_ = enabled ? "online" : ""; }
    static std::optional<double> mean(CameraService &service, const QString &id, const cv::Mat &frame) {
        return service.autoExposureMean(id, frame);
    }
    static void overrideExposure(CameraService &service, const QString &id) { service.plcExposureOverrides_.insert(id, 3000); }
};
}
namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool equals(std::optional<double> value, double expected) {
    return value && std::abs(*value - expected) < 1e-6;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<pva::RuntimeSnapshot>("pva::RuntimeSnapshot");
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    pva::MeasurementConfig config;
    config.runtime.stateFile = directory.filePath("measurement_state.json");
    config.runtime.offlineImageDir = directory.path();
    config.runtime.facetteImageDir = directory.filePath("facettes");
    config.measurement.autoExposureRoiCamera1 = {0, 0, 4, 4};
    config.measurement.autoExposureRoiCamera2 = {4, 0, 4, 4};
    pva::CameraService cameras(config);
    using Access = pva::CameraServiceTestAccess;
    const auto cam1 = pva::CameraRole::Cam1, cam2 = pva::CameraRole::Cam2;
    cv::Mat frame(50, 80, CV_8U, cv::Scalar(20));
    frame(cv::Rect(4, 0, 4, 4)).setTo(40);
    frame(cv::Rect(18, 13, 4, 4)).setTo(100);
    frame(cv::Rect(30, 20, 4, 4)).setTo(200);
    check(!Access::mean(cameras, cam1, frame), "Offline never auto exposes");
    Access::online(cameras, true);
    for (const auto stage : {pva::MeasurementStage::Melt, pva::MeasurementStage::Dip, pva::MeasurementStage::Neck}) {
        cameras.setStage(int(stage));
        check(equals(Access::mean(cameras, cam1, frame), 20), "Enabled stage falls back to camera1 config");
        check(equals(Access::mean(cameras, cam2, frame), 40), "Camera2 config independent");
    }
    pva::MeasurementRois rois;
    rois.melt = std::array<double, 6>{20, 15, 4, 4, 12, 7};
    cameras.setMeasurementRois(rois);
    check(equals(Access::mean(cameras, cam1, frame), 100), "PLC rectangle preferred in native coordinates");
    check(equals(Access::mean(cameras, cam2, frame), 200), "Camera2 uses both PLC offsets");
    check(120 - *Access::mean(cameras, cam1, frame) > 0 &&
          120 - *Access::mean(cameras, cam2, frame) < 0, "Selected means drive opposite exposure adjustment directions");
    for (const auto stage : {pva::MeasurementStage::Crown, pva::MeasurementStage::Body}) {
        cameras.setStage(int(stage));
        check(!Access::mean(cameras, cam1, frame) && !Access::mean(cameras, cam2, frame),
              "Crown and Body do not adjust");
    }
    cameras.setStage(int(pva::MeasurementStage::Dip));
    rois.melt = std::array<double, 6>{20, 15, 4, 4, 100, 0};
    cameras.setMeasurementRois(rois);
    check(equals(Access::mean(cameras, cam1, frame), 100) &&
          equals(Access::mean(cameras, cam2, frame), 40), "Out-of-frame camera2 independently falls back");
    rois.melt = std::array<double, 6>{1, 1, 4, 4, 0, 0};
    cameras.setMeasurementRois(rois);
    frame(cv::Rect(0, 0, 3, 3)).setTo(60);
    check(equals(Access::mean(cameras, cam1, frame), 60), "Partly outside PLC rectangle clips rather than falls back");
    frame(cv::Rect(0, 0, 3, 3)).setTo(20);
    rois.melt = std::array<double, 6>{20, 15, 0.1, 0.1, 0, 0};
    cameras.setMeasurementRois(rois);
    check(equals(Access::mean(cameras, cam1, frame), 20), "Rounded empty PLC ROI falls back");
    rois.melt = std::array<double, 6>{200, 200, 4, 4, 0, 0};
    cameras.setMeasurementRois(rois);
    config.measurement.autoExposureRoiCamera1 = {18, 13, 4, 4};
    cameras.reloadConfig(config);
    check(equals(Access::mean(cameras, cam1, frame), 100), "Configuration ROI hot reload changes fallback");
    config.measurement.autoExposureRoiCamera1 = {200, 200, 4, 4};
    cameras.reloadConfig(config);
    check(!Access::mean(cameras, cam1, frame), "No valid fallback skips exposure");
    config.camera.autoExposureEnabled = false;
    cameras.reloadConfig(config);
    rois.melt = std::array<double, 6>{20, 15, 4, 4, 12, 7};
    cameras.setMeasurementRois(rois);
    check(!Access::mean(cameras, cam2, frame), "Master enable still respected");
    config.camera.autoExposureEnabled = true;
    cameras.reloadConfig(config);
    check(!Access::mean(cameras, cam2, cv::Mat{}), "Empty frame ignored");
    cv::Mat color;
    cv::cvtColor(frame, color, cv::COLOR_GRAY2BGR);
    check(equals(Access::mean(cameras, cam2, color), 200), "Color frame converted to grayscale");

    // 使用实际运行控制器快照验证启动恢复及队列中的更新先于触发。
    pva::PlcRuntimeState state;
    state.stage = pva::MeasurementStage::Melt;
    state.parameters.insert("mlt_crd", {20, 15, 4, 4, 12, 7});
    QString error;
    check(pva::PlcRuntimeStore(directory.filePath("plc_runtime_state.json")).save(state, &error), "Fixture state saved");
    pva::RuntimeController runtime(config, nullptr, false);
    QThread thread;
    cameras.moveToThread(&thread);
    QObject::connect(&runtime, &pva::RuntimeController::stateChanged, &cameras,
                     [&](const pva::RuntimeSnapshot &snapshot) { cameras.setMeasurementRois(snapshot.rois); });
    QObject::connect(&runtime, &pva::RuntimeController::onlineStageChanged, &cameras, &pva::CameraService::setStage);
    std::optional<double> startupMean;
    QObject::connect(&runtime, &pva::RuntimeController::onlineCameraStartRequested, &cameras,
                     [&](quint64) { startupMean = Access::mean(cameras, cam2, frame); });
    thread.start();
    runtime.initialize();
    std::optional<double> mean;
    QMetaObject::invokeMethod(&cameras, [&] { mean = Access::mean(cameras, cam2, frame); }, Qt::BlockingQueuedConnection);
    check(equals(mean, 200), "Restored PLC ROI delivered across threads");
    runtime.onSherlockCommand({"mlt_crd", {"2000", "1500", "400", "400", "0", "0"}, "mlt_crd"});
    QMetaObject::invokeMethod(&cameras, [&] { mean = Access::mean(cameras, cam2, frame); }, Qt::BlockingQueuedConnection);
    check(equals(mean, 100), "PLC coordinate update delivered before next camera operation");
    runtime.onSherlockCommand({"mlt_crd", {"2000", "1500", "-400", "400", "0", "0"}, "mlt_crd"});
    QMetaObject::invokeMethod(&cameras, [&] {
        mean = Access::mean(cameras, cam2, frame);
    }, Qt::BlockingQueuedConnection);
    check(equals(mean, 100), "Rejected PLC coordinates preserve previous ROI");
    runtime.startRuntime(true);
    QMetaObject::invokeMethod(&cameras, [] {}, Qt::BlockingQueuedConnection);
    check(equals(startupMean, 100), "ROI and stage arrive before production start request");
    QMetaObject::invokeMethod(&cameras, [&] {
        Access::overrideExposure(cameras, cam2);
        mean = Access::mean(cameras, cam2, frame);
        cameras.moveToThread(app.thread());
    }, Qt::BlockingQueuedConnection);
    check(!mean, "PLC exposure override retains priority");
    thread.quit(); thread.wait();
    runtime.shutdown();
    return failures ? 1 : 0;
}
