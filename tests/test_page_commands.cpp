#include "page_home.hpp"
#include "page_camera.hpp"
#include "app_services.hpp"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <opencv2/imgcodecs.hpp>
#include <functional>
#include <iostream>

namespace {
int failures{};
void check(bool ok, const char *message)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    pva::MeasurementConfig config;
    config.runtime.offlineImageDir = directory.path();
   config.runtime.stateFile = directory.filePath("state.json");
    config.runtime.measurementImageDir = directory.filePath("measurement_images");
    config.runtime.facetteImageDir = directory.filePath("facettes");
    cv::Mat pair(80, 40, CV_8U, cv::Scalar(20));
    for (int i = 0; i < 3; ++i)
        check(cv::imwrite(QFile::encodeName(directory.filePath(QString("pair%1.bmp").arg(i))).constData(), pair),
              "Create offline pair");

    pva::AppServices services(config, nullptr, false);
    pva::PageHome home(config, services.runtime());
    pva::PageCamera camera(services.cameras());
    auto *triggerSource = camera.findChild<QComboBox *>("combTrigSource");
    auto *triggerEdge = camera.findChild<QComboBox *>("combTrigEdge");
    check(triggerSource && triggerEdge, "Camera trigger controls exist");
    if (triggerSource && triggerEdge) {
        pva::CameraSnapshot manualCamera;
        manualCamera.ids = {pva::CameraRole::Cam1};
        manualCamera.selected = pva::CameraRole::Cam1;
        manualCamera.open = true;
        manualCamera.triggerSource = 0;
        services.cameras().stateChanged(manualCamera);
        check(!triggerEdge->isEnabled(), "Software trigger disables trigger edge");
        manualCamera.triggerSource = 1;
        services.cameras().stateChanged(manualCamera);
        check(triggerEdge->isEnabled(), "Line trigger enables trigger edge");
        manualCamera.production = true;
        services.cameras().stateChanged(manualCamera);
        check(!triggerEdge->isEnabled(), "Production use disables trigger edge");
    }
    pva::RuntimeSnapshot state;
    int snapshots{}, cameraSnapshots{}, results{};
    QObject::connect(&services.runtime(), &pva::RuntimeController::stateChanged, &app,
        [&](const auto &value) { state = value; ++snapshots; });
    QObject::connect(&services.runtime(), &pva::RuntimeController::resultReady, &app,
        [&](const auto &) { ++results; });
    QObject::connect(&services.cameras(), &pva::CameraService::stateChanged, &app,
        [&](const auto &) { ++cameraSnapshots; });
    services.start();
    check(until([&] { return snapshots && cameraSnapshots; }), "Services initialize without pages owning devices");

    // 通过真实按钮触发，等待后台状态，而不是直接调用业务方法。
    const auto click = [&](const char *name) {
        auto *button = home.findChild<QPushButton *>(name);
        check(button && button->isEnabled(), name);
        if (button) button->click();
    };
    click("btnStart");
    check(until([&] { return state.state == pva::RunState::Running && results; }), "Run button starts background measurement");
    click("btnNextImage");
    click("btnNextImage");
    check(until([&] { return state.imageIndex == 2; }), "Rapid next clicks use controller index");
    click("btnFirstImage");
    check(until([&] { return state.imageIndex == 0; }), "First image binding");
    click("btnLastImage");
    check(until([&] { return state.imageIndex == 2; }), "Last image binding");
    click("btnPreviousImage");
    check(until([&] { return state.imageIndex == 1; }), "Previous image binding");
    click("btnStageBody");
    check(until([&] { return state.stage == pva::MeasurementStage::Body; }), "Stage enum crosses thread boundary");
    click("btnStart");
    check(until([&] { return state.state == pva::RunState::Stopped; }), "Stop button stops background measurement");

    // 未打开任何真实相机；选择不存在的角色仍会由服务发布状态。
    auto *combo = camera.findChild<QComboBox *>("combCameraList");
    check(combo != nullptr, "Camera selector exists");
    if (combo) {
        { QSignalBlocker blocker(combo); combo->clear(); combo->addItem("Missing", "missing-test-device"); combo->setCurrentIndex(-1); }
        const auto before = cameraSnapshots;
        combo->setCurrentIndex(0);
        check(until([&] { return cameraSnapshots > before; }), "Camera selection reaches service thread");
    }
    services.stop();
    return failures ? 1 : 0;
}
