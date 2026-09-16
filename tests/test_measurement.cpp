#include "config.hpp"
#include "measurement_engine.hpp"
#include "state_store.hpp"
#include "algorithms/detectors.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool condition, const char *name)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString cnf = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("../src/cnf.ini");
    try
    {
        auto parsed = pva::MeasurementConfig::loadIni(cnf);
        check(parsed.measurement.reflectorRoiCamera1.width > 0 &&
                  parsed.measurement.reflectorRoiCamera1.height > 0 &&
                  parsed.measurement.reflectorRoiCamera2.width > 0 &&
                  parsed.measurement.reflectorRoiCamera2.height > 0,
              "Manual reflector ROIs parsed");
        check(QDir::isAbsolutePath(parsed.runtime.offlineImageDir), "Offline directory resolved relative to cnf");
        check(QDir(parsed.runtime.offlineImageDir).exists(), "Configured offline directory exists");

        pva::ConfigEntryUpdate update;
        QString configError;
        check(pva::applyConfigEntry(parsed, cnf, "Camera", "auto_exposure_target", "999",
                                    &update, &configError) &&
                  update.recognized && update.changed && parsed.camera.autoExposureTarget == 254,
              "Shared config registry applies and clamps live numeric edits");
        check(pva::applyConfigEntry(parsed, cnf, "Measurement", "auto_exposure_roi_camera1",
                                    "10,20,30,40", &update, &configError) &&
                  update.recognized && update.changed &&
                  parsed.measurement.autoExposureRoiCamera1 == cv::Rect(10, 20, 30, 40),
              "Shared config registry parses live ROI edits");
        check(pva::applyConfigEntry(parsed, cnf, "Custom", "future_parameter", "value",
                                    &update, &configError) &&
                  !update.recognized && !update.changed,
              "Unknown INI entries remain accepted");
        const double previousTarget = parsed.camera.autoExposureTarget;
        check(!pva::applyConfigEntry(parsed, cnf, "Camera", "auto_exposure_target", "bad",
                                     &update, &configError) &&
                  parsed.camera.autoExposureTarget == previousTarget &&
                  configError.contains("Camera.auto_exposure_target"),
              "Invalid registered edits are rejected without changing runtime config");

        // 用现场目录验证：读取合成图、左右拆分、提交测量引擎。
        QDir offlineDirectory(parsed.runtime.offlineImageDir);
        const QFileInfoList images = offlineDirectory.entryInfoList(
            {"*.bmp", "*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}, QDir::Files, QDir::Name);
        if (!images.isEmpty())
        {
            QFile imageFile(images.first().absoluteFilePath());
            check(imageFile.open(QIODevice::ReadOnly), "Offline image file opened");
            const QByteArray encoded = imageFile.readAll();
            const cv::Mat buffer(1, encoded.size(), CV_8U, const_cast<char *>(encoded.constData()));
            const cv::Mat composite = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
            check(!composite.empty(), "Offline image decoded");
            check(!composite.empty() && composite.cols % 2 == 0, "Offline composite width is even");
            if (!composite.empty() && composite.cols % 2 == 0)
            {
                const int middle = composite.cols / 2;
                pva::MeasurementEngine offlineEngine(parsed);
                const auto result = offlineEngine.process(
                    composite.colRange(0, middle), composite.colRange(middle, composite.cols),
                    pva::MeasurementStage::Neck);
                check(result.preview1.cols == middle && result.preview2.cols == middle,
                      "Offline stereo pair submitted to measurement engine");
                check(result.diagnostics.contains("cycle_ms"),
                      "Real offline frame produces Cycle diagnostics");
                check(result.diagnostics.contains("light_camera1") && result.diagnostics.contains("light_camera2"),
                      "Real offline frame produces Process diagnostics");
                check(result.valid, "Real offline Neck frame detects meniscus");
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        ++failures;
    }

    pva::MeasurementConfig config;
    config.measurement.brightnessMin = 1;
    config.neck.gradientThresholdCamera1 = 10;
    config.neck.gradientThresholdCamera2 = 10;
    config.neck.stopSearchRatio = 1;
    config.neck.stopSearchRatio = .6;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(0, 0, 400, 400);
    config.neck.pixelsPerMm = 10;
    cv::Mat neck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(neck, {200, 180}, {80, 40}, 0, 0, 360, cv::Scalar(220), 5);
    pva::MeasurementEngine engine(config);
    auto neckResult = engine.process(neck, neck, pva::MeasurementStage::Neck);
    check(neckResult.valid, "Neck synthetic measurement valid");
    check(neckResult.values.diameterMm && std::abs(*neckResult.values.diameterMm - 16.0) < 2.0, "Neck diameter keeps pixels-per-mm calculation");
    check(neckResult.overlay1.size() >= 4 && neckResult.overlay2.size() >= 4,
          "Neck overlays include contour ellipse center and lower vertex");
    check(neckResult.diagnostics.contains("cycle_ms") && neckResult.diagnostics.at("cycle_ms").toDouble() >= 0.0,
          "Cycle diagnostic populated");
    check(neckResult.diagnostics.contains("neck_major_axis_camera1_px"),
          "Neck process diagnostics populated");
    auto idleResult = engine.process(neck, neck, pva::MeasurementStage::Idle);
    check(idleResult.stage == pva::MeasurementStage::Idle && idleResult.overlay1.size() >= 4,
          "Idle uses Neck overlays while preserving Idle stage");

    // 构造带明显内凹缺口的 meniscus，确认拟合点集采用开放的外侧凸弧。
    cv::Mat concaveNeck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(concaveNeck, {200, 190}, {100, 65}, 0, 0, 360, cv::Scalar(220), cv::FILLED);
    cv::rectangle(concaveNeck, {188, 110}, {212, 185}, cv::Scalar(0), cv::FILLED);
    const auto concaveHit = pva::algorithms::findNeckEllipse(concaveNeck, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {});
    check(concaveHit && !concaveHit->contourClosed,
          "Neck ellipse uses the open outer convex arc for a concave contour");
    const auto croppedNeckHit = pva::algorithms::findNeckEllipse(
        concaveNeck, cv::Rect(80, 80, 240, 220), 10, 80, 0, 1, {});
    check(croppedNeckHit && std::abs(croppedNeckHit->ellipse.center.x - 200.0) < 10.0 &&
              std::abs(croppedNeckHit->ellipse.center.y - 190.0) < 10.0,
          "Neck detection uses the manual reflector ROI and restores full-image coordinates");

    cv::Mat meniscus(260, 300, CV_8U, cv::Scalar(20));
    for (int x = 20; x < 280; ++x)
    {
        const int boundary = static_cast<int>(140 - .002 * (x - 150) * (x - 150));
        meniscus(cv::Rect(x, 0, 1, std::max(boundary, 1))).setTo(220);
    }
    config.crown.horizontalMarginPx = 10;
    config.crown.bottomMarginPx = 10;
    config.crown.minEdgePoints = 20;
    config.crown.columnMaxFactor = .2;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(20, 0, 261, 231);
    pva::MeasurementState crownState;
    crownState.validNeck = true;
    crownState.neckCentersPx = std::array<cv::Point2d, 2>{cv::Point2d(140, 20), cv::Point2d(160, 20)};
    pva::MeasurementEngine crownEngine(config, crownState);
    auto crownResult = crownEngine.process(meniscus, meniscus, pva::MeasurementStage::Crown);
    check(crownResult.valid, "Crown manual-ROI lower vertex valid");
    const auto hasStoredNeckCenter = [](const std::vector<pva::OverlayElement> &overlays,
                                        cv::Point2d expectedCenter)
    {
        return std::ranges::any_of(overlays, [expectedCenter](const pva::OverlayElement &element)
                                  { return element.type == pva::OverlayType::Cross &&
                                           !element.points.empty() &&
                                           cv::norm(element.points.front() - expectedCenter) < 0.01 &&
                                           element.colorBgr == cv::Scalar(0, 255, 0); });
    };
    check(hasStoredNeckCenter(crownResult.overlay1, {140, 20}) &&
              hasStoredNeckCenter(crownResult.overlay2, {160, 20}),
          "Crown overlays retain both stored Neck centers");
    check(std::ranges::any_of(crownResult.overlay1, [](const pva::OverlayElement &element)
                             { return element.type == pva::OverlayType::Polyline && element.closed &&
                                      element.colorBgr == cv::Scalar(0, 255, 0); }),
          "Crown draws the manual reflector ROI as a green rectangle");
    check(crownEngine.state().crownBoundaryPointsPx &&
              std::abs((*crownEngine.state().crownBoundaryPointsPx)[0].x - 140.0) < 0.01 &&
              std::abs((*crownEngine.state().crownBoundaryPointsPx)[1].x - 160.0) < 0.01,
          "Crown lower vertices use the stored Neck center x coordinates");
    check(crownResult.diagnostics.size() >= 40 &&
              crownResult.diagnostics.contains("crown_boundary_camera1_px") &&
              crownResult.diagnostics.contains("crown_column_strengths_maximum_camera2") &&
              crownResult.diagnostics.contains("crown_edge_model"),
          "Crown Process diagnostics match the Python field set");
    config.body.horizontalMarginPx = 10;
    config.body.bottomMarginPx = 10;
    config.body.minEdgePoints = 20;
    config.body.startSearchRatio = 0;
    config.body.stopSearchRatio = 1;
    config.body.minCoverageRatio = .5;
    config.body.brightnessOffsetCamera1 = config.body.brightnessOffsetCamera2 = 20;
    pva::MeasurementEngine bodyEngine(config, crownState);
    auto bodyResult = bodyEngine.process(meniscus, meniscus, pva::MeasurementStage::Body);
    check(bodyResult.valid, "Body manual-ROI lower vertex valid");
    check(hasStoredNeckCenter(bodyResult.overlay1, {140, 20}) &&
              hasStoredNeckCenter(bodyResult.overlay2, {160, 20}),
          "Body overlays retain both stored Neck centers");
    check(bodyEngine.state().bodyBoundaryPointsPx &&
              std::abs((*bodyEngine.state().bodyBoundaryPointsPx)[0].x - 140.0) < 0.01 &&
              std::abs((*bodyEngine.state().bodyBoundaryPointsPx)[1].x - 160.0) < 0.01,
          "Body lower vertices use the stored Neck center x coordinates");
    check(bodyResult.diagnostics.size() >= 40 &&
              bodyResult.diagnostics.contains("body_boundary_camera1_px") &&
              bodyResult.diagnostics.contains("body_column_maximum_p90_camera2") &&
              bodyResult.diagnostics.contains("body_edge_model"),
          "Body Process diagnostics match the Python field set");

    pva::MeasurementState state;
    state.validNeck = true;
    state.mmPerPixel = .1;
    state.neckXSpans = std::array<cv::Vec2i, 2>{cv::Vec2i(50, 150), cv::Vec2i(50, 150)};
    state.bodyCentersPx = std::array<cv::Point2d, 2>{cv::Point2d(100, 100), cv::Point2d(100, 100)};
    cv::Mat endcone(250, 200, CV_8U, cv::Scalar(200));
    endcone.rowRange(150, 250).setTo(20);
    pva::MeasurementEngine endconeEngine(config, state);
    auto endconeResult = endconeEngine.process(endcone, endcone, pva::MeasurementStage::Endcone);
    check(endconeResult.valid, "Endcone state-based measurement valid");
    check(endconeResult.values.diameterMm && std::abs(*endconeResult.values.diameterMm - 4.9) < .3, "Endcone diameter unchanged");

    QTemporaryDir stateDirectory;
    pva::MeasurementState persisted = crownState;
    persisted.values.diameterMm = 18.25;
    persisted.mmPerPixel = 0.041;
    const QString statePath = stateDirectory.filePath("measurement_state.json");
    QString stateError;
    check(pva::StateStore(statePath).save(persisted, &stateError), "Measurement state saved atomically");
    const auto restored = pva::StateStore(statePath).load(&stateError);
    check(restored.validNeck && restored.neckCentersPx.has_value(), "Neck state restored without calculated reflector data");
    check(restored.values.diameterMm && std::abs(*restored.values.diameterMm - 18.25) < 1e-6,
          "Diameter value restored");
    check(restored.mmPerPixel && std::abs(*restored.mmPerPixel - 0.041) < 1e-9,
          "Millimetres-per-pixel restored");
    if (failures == 0)
        std::cout << "All C++ measurement tests passed\n";
    return failures == 0 ? 0 : 1;
}
