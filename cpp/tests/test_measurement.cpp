#include "pva/config.hpp"
#include "pva/measurement_engine.hpp"
#include "pva/state_store.hpp"
#include "pva/algorithms/detectors.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
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
        check(parsed.neck.reflectorThresholdCamera1 == 50, "Reflector ROI parameters parsed");
        check(QDir::isAbsolutePath(parsed.runtime.offlineImageDir), "Offline directory resolved relative to cnf");
        check(QDir(parsed.runtime.offlineImageDir).exists(), "Configured offline directory exists");

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
                check(result.valid, "Real offline Neck frame detects reflector and meniscus");
                check(!result.valid || offlineEngine.state().neckReflectorRois.has_value(),
                      "Valid real Neck frame stores reflector ROIs");
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
    config.neck.reflectorThresholdCamera1 = config.neck.reflectorThresholdCamera2 = 50;
    config.neck.reflectorBottomSearchTopRatio = .65;
    config.neck.reflectorBottomSearchBottomRatio = .98;
    config.neck.pixelsPerMm = 10;
    cv::Mat neck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(neck, {200, 180}, {80, 40}, 0, 0, 360, cv::Scalar(220), 5);
    cv::rectangle(neck, {50, 270}, {350, 340}, cv::Scalar(220), cv::FILLED);
    pva::MeasurementEngine engine(config);
    auto neckResult = engine.process(neck, neck, pva::MeasurementStage::Neck);
    check(neckResult.valid, "Neck synthetic measurement valid");
    check(engine.state().neckReflectorRois.has_value(),
          "Neck stores reflector ROIs for Crown and Body");
    check(neckResult.values.diameterMm && std::abs(*neckResult.values.diameterMm - 16.0) < 2.0, "Neck diameter keeps pixels-per-mm calculation");
    check(neckResult.overlay1.size() >= 4 && neckResult.overlay2.size() >= 4,
          "Neck overlays include contour ellipse center and lower vertex");
    check(neckResult.diagnostics.contains("cycle_ms") && neckResult.diagnostics.at("cycle_ms") >= 0.0,
          "Cycle diagnostic populated");
    check(neckResult.diagnostics.contains("neck_major_axis_camera1_px"),
          "Neck process diagnostics populated");
    auto idleResult = engine.process(neck, neck, pva::MeasurementStage::Idle);
    check(idleResult.stage == pva::MeasurementStage::Idle && idleResult.overlay1.size() >= 4,
          "Idle uses Neck overlays while preserving Idle stage");

    // 回归：与 Python 一致，瞬时丢失 reflector 不能破坏上一帧的有效动态 ROI。
    cv::Mat noReflector(400, 400, CV_8U, cv::Scalar(100));
    auto lostReflectorResult = engine.process(noReflector, noReflector, pva::MeasurementStage::Neck);
    check(!lostReflectorResult.valid && engine.state().validNeck && engine.state().neckReflectorRois.has_value(),
          "Lost reflector preserves the previous valid dynamic ROI");

    // 构造带明显内凹缺口的 meniscus，确认拟合点集采用开放的外侧凸弧。
    cv::Mat concaveNeck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(concaveNeck, {200, 190}, {100, 65}, 0, 0, 360, cv::Scalar(220), cv::FILLED);
    cv::rectangle(concaveNeck, {188, 110}, {212, 185}, cv::Scalar(0), cv::FILLED);
    const auto concaveHit = pva::algorithms::findNeckEllipse(concaveNeck, 10, 80, 0, 1, {});
    check(concaveHit && !concaveHit->contourClosed,
          "Neck ellipse uses the open outer convex arc for a concave contour");

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
    pva::ReflectorRoi dynamicRoi;
    dynamicRoi.leftBoundary = {20, 230};
    dynamicRoi.rightBoundary = {280, 230};
    dynamicRoi.center = {150, 230};
    dynamicRoi.bottomCurve = {dynamicRoi.leftBoundary, dynamicRoi.rightBoundary};
    pva::MeasurementState crownState;
    crownState.validNeck = true;
    crownState.neckReflectorRois = std::array<pva::ReflectorRoi, 2>{dynamicRoi, dynamicRoi};
    crownState.neckCentersPx = std::array<cv::Point2d, 2>{cv::Point2d(150, 20), cv::Point2d(150, 20)};
    pva::MeasurementEngine crownEngine(config, crownState);
    auto crownResult = crownEngine.process(meniscus, meniscus, pva::MeasurementStage::Crown);
    check(crownResult.valid, "Crown reflector-ROI lower vertex valid");
    check(crownEngine.state().crownBoundaryPointsPx && std::abs((*crownEngine.state().crownBoundaryPointsPx)[0].x - 149.5) < 1, "Crown stores only centered lower vertex");
    config.body.horizontalMarginPx = 10;
    config.body.bottomMarginPx = 10;
    config.body.minEdgePoints = 20;
    config.body.startSearchRatio = 0;
    config.body.stopSearchRatio = 1;
    config.body.minCoverageRatio = .5;
    config.body.brightnessOffsetCamera1 = config.body.brightnessOffsetCamera2 = 20;
    pva::MeasurementEngine bodyEngine(config, crownState);
    auto bodyResult = bodyEngine.process(meniscus, meniscus, pva::MeasurementStage::Body);
    check(bodyResult.valid, "Body reflector-ROI lower vertex valid");
    check(bodyEngine.state().bodyBoundaryPointsPx && std::abs((*bodyEngine.state().bodyBoundaryPointsPx)[0].x - 149.5) < 1, "Body stores only centered lower vertex");

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
    check(restored.validNeck && restored.neckReflectorRois.has_value(), "Dynamic reflector ROIs restored");
    check(restored.values.diameterMm && std::abs(*restored.values.diameterMm - 18.25) < 1e-6,
          "Diameter value restored");
    check(restored.mmPerPixel && std::abs(*restored.mmPerPixel - 0.041) < 1e-9,
          "Millimetres-per-pixel restored");
    if (failures == 0)
        std::cout << "All C++ measurement tests passed\n";
    return failures == 0 ? 0 : 1;
}
