#include "config.hpp"
#include "measurement_payload.hpp"
#include "measurement_engine.hpp"
#include "state_store.hpp"
#include "plc_runtime.hpp"
#include "daily_log.hpp"
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
        check(parsed.measurement.diaRectHeightPx > 0.0 &&
                  parsed.measurement.crownBodyInnerRadiusPx >= 0.0,
              "PLC diameter ROI dimensions parsed");

        pva::MeasurementRois plcRois;
        QString plcError;
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 0, 90, 400, 500, 8, -6}, &plcError),
              "Diameter PLC ROI accepted");
        check(pva::diameterStage(true, plcRois, 400) == pva::MeasurementStage::Crown,
              "Inner radius equal to threshold stays Crown");
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 0, 90, 401, 500, 8, -6}, &plcError) &&
                  pva::diameterStage(true, plcRois, 400) == pva::MeasurementStage::Body,
              "Inner radius above threshold selects Body");
        check(pva::diameterStage(false, plcRois, 400) == pva::MeasurementStage::Neck,
              "Circle fit selects Neck");
        check(pva::stageForPlcCommand("cfit_ne", pva::MeasurementStage::Body,
                                      false, plcRois, 400) == pva::MeasurementStage::Neck &&
                  pva::stageForPlcCommand("pfit_sb", pva::MeasurementStage::Neck,
                                           true, plcRois, 400) == pva::MeasurementStage::Body &&
                  pva::stageForPlcCommand("dia_msr", pva::MeasurementStage::Body,
                                           true, plcRois, 400) == pva::MeasurementStage::Body &&
                  pva::stageForPlcCommand("mlt_msr", pva::MeasurementStage::Body,
                                           true, plcRois, 400) == pva::MeasurementStage::Melt &&
                  pva::stageForPlcCommand("dip_msr", pva::MeasurementStage::Melt,
                                           true, plcRois, 400) == pva::MeasurementStage::Dip,
              "PLC fit and measurement commands select and preserve stages");
        check(!pva::setPlcRoi(plcRois, "dia_crd",
                              {100, 100, 0, 90, 500, 400, 8, -6}, &plcError),
              "Invalid diameter radii rejected");
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 0, 90, 400, 500, 8, -6}, &plcError),
              "Diameter ROI at threshold accepted");
        check(pva::stageForPlcCommand("pfit_sb", pva::MeasurementStage::Neck,
                                      true, plcRois, 400) == pva::MeasurementStage::Crown,
              "Point fit remains Crown at threshold");
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 0, 90, 401, 500, 8, -6}, &plcError) &&
                  pva::stageForPlcCommand("dia_crd", pva::MeasurementStage::Crown,
                                           true, plcRois, 400) == pva::MeasurementStage::Body,
              "Later diameter coordinates switch Crown to Body");
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 0, 90, 10, 20, 8, -6}, &plcError),
              "Diameter ROI restored for geometry checks");
        std::vector<pva::OverlayElement> roiOverlay1, roiOverlay2;
        pva::appendPlcRoiOverlays(plcRois, pva::MeasurementStage::Crown, 80,
                                  roiOverlay1, roiOverlay2);
        check(roiOverlay1.size() == 2 && roiOverlay2.size() == 2 &&
                  std::abs(roiOverlay1[0].points.front().x - 100.0) < 1e-8 &&
                  std::abs(roiOverlay1[0].points.front().y - 120.0) < 1e-8 &&
                  std::abs(roiOverlay1[0].points[roiOverlay1[0].points.size() / 2 - 1].x - 80.0) < 1e-8 &&
                  std::abs(roiOverlay2[0].points.front().x - 108.0) < 1e-8 &&
                  roiOverlay1[1].points[0] == cv::Point2d(80, 60) &&
                  roiOverlay1[1].points[2] == cv::Point2d(90, 140),
              "Diameter sector angles, offsets and rectangle geometry");
        check(pva::setPlcRoi(plcRois, "dia_crd",
                             {100, 100, 350, 10, 10, 20, 0, 0}, &plcError),
              "Sector crossing zero degrees accepted");
        roiOverlay1.clear();
        roiOverlay2.clear();
        pva::appendPlcRoiOverlays(plcRois, pva::MeasurementStage::Crown, 80,
                                  roiOverlay1, roiOverlay2);
        check(roiOverlay1.size() == 2 &&
                  roiOverlay1[0].points.front().x > 100 &&
                  roiOverlay1[0].points[roiOverlay1[0].points.size() / 2 - 1].x < 100,
              "Sector angle sweep crosses zero in clockwise direction");

        check(pva::setPlcRoi(plcRois, "mlt_crd", {5, 5, 4, 4, 2, 1}, &plcError) &&
                  pva::setPlcRoi(plcRois, "dip_crd", {5, 5, 5}, &plcError),
              "Melt and Dip PLC coordinates accepted");
        check(pva::stageForPlcCommand("mlt_crd", pva::MeasurementStage::Neck,
                                      false, plcRois, 400) == pva::MeasurementStage::Melt &&
                  pva::stageForPlcCommand("dip_crd", pva::MeasurementStage::Melt,
                                           false, plcRois, 400) == pva::MeasurementStage::Dip &&
                  pva::stageForPlcCommand("dia_crd", pva::MeasurementStage::Dip,
                                           true, plcRois, 400) == pva::MeasurementStage::Crown,
              "PLC coordinates immediately select their display stage");
        cv::Mat sample1(12, 12, CV_8UC1, cv::Scalar(10));
        cv::Mat sample2(12, 12, CV_8UC1, cv::Scalar(20));
        sample1.at<uchar>(5, 5) = 30;
        const auto melt1 = pva::meltRoiStats(sample1, *plcRois.melt, 1);
        const auto melt2 = pva::meltRoiStats(sample2, *plcRois.melt, 2);
        const auto dip = pva::dipLineMean(sample1, *plcRois.dip);
        check(melt1 && melt2 && melt1->maximum == 30 && melt2->average == 20 &&
                  dip && *dip > 10 && *dip < 30,
              "Melt rectangles and Dip vertical line measure image pixels");
        pva::MeasurementConfig statsConfig = parsed;
        statsConfig.measurement.brightnessMin = 0;
        statsConfig.measurement.brightnessMax = 255;
        pva::MeasurementEngine statsEngine(statsConfig);
        statsEngine.setMeasurementRois(plcRois);
        const auto meltResult = statsEngine.process(sample1, sample2, pva::MeasurementStage::Melt);
        const auto dipResult = statsEngine.process(sample1, sample2, pva::MeasurementStage::Dip);
        check(meltResult.valid && meltResult.diagnostics.contains("melt_average_camera1") &&
                  dipResult.valid && dipResult.diagnostics.contains("dip_line_average_camera1"),
              "Melt and Dip stages produce measurement diagnostics");
        check(pva::measurementPayloadValues(meltResult).size() == 7 &&
                  pva::measurementPayloadValues(meltResult)[0] == 0.0 &&
                  std::abs(pva::measurementPayloadValues(meltResult)[1] - melt1->average) < 0.001 &&
                  pva::measurementPayloadValues(meltResult)[2] == melt1->maximum &&
                  pva::measurementPayloadValues(meltResult)[3] == melt1->minimum &&
                  std::abs(pva::measurementPayloadValues(meltResult)[4] - melt2->average) < 0.001 &&
                  pva::measurementPayloadValues(meltResult)[5] == melt2->maximum &&
                  pva::measurementPayloadValues(meltResult)[6] == melt2->minimum &&
                  pva::measurementPayloadValues(dipResult).size() == 1 &&
                  std::abs(pva::measurementPayloadValues(dipResult)[0] - *dip) < 0.001,
              "Melt and Dip PLC fields are calculated by the measurement engine");

        QTemporaryDir plcStateDir;
        pva::PlcRuntimeState stateToSave;
        stateToSave.stage = pva::MeasurementStage::Body;
        stateToSave.pointFitSelected = true;
        stateToSave.acquisitionEnabled = false;
        stateToSave.relativeThreshold = true;
        stateToSave.refreshRate = 2.5;
        stateToSave.parameters.insert("dia_crd", {100, 100, 0, 90, 401, 500, 8, -6});
        stateToSave.parameters.insert("mlt_crd", {5, 5, 4, 4, 2, 1});
        stateToSave.parameters.insert("dip_crd", {5, 5, 5});
        stateToSave.parameters.insert("dia_thr", {70});
        stateToSave.parameters.insert("exptme1", {120});
        pva::PlcRuntimeStore plcStore(plcStateDir.filePath("plc_runtime_state.json"));
        pva::PlcRuntimeState loadedState;
        check(plcStore.save(stateToSave, &plcError) &&
                  plcStore.load(&loadedState, &plcError) &&
                  loadedState.stage == pva::MeasurementStage::Body &&
                  loadedState.pointFitSelected && !loadedState.acquisitionEnabled &&
                  loadedState.relativeThreshold && loadedState.refreshRate == 2.5 &&
                  loadedState.parameters.value("dia_crd").size() == 8 &&
                  loadedState.parameters.value("exptme1").size() == 1 &&
                  loadedState.parameters.value("exptme1").front() == 120,
              "PLC mode, ROI and settings survive save and reload");

        QTemporaryDir logDir;
        pva::DailyLog dailyLog;
        dailyLog.setDirectory(logDir.path());
        const QDateTime firstTime(QDate(2026, 10, 1), QTime(10, 0, 0));
        pva::DailyLogResult repeated;
        for (int index = 0; index < 10; ++index)
            repeated = dailyLog.append(firstTime.addSecs(index), "msg");
        const auto changed = dailyLog.append(firstTime.addSecs(11), "next");
        QFile firstDay(logDir.filePath("2026-10-01.log"));
        check(firstDay.open(QIODevice::ReadOnly), "Daily log file opens");
        const QByteArray firstDayContent = firstDay.readAll();
        check(repeated.written && repeated.merged &&
                  firstDayContent.contains("msg x 10\n") &&
                  firstDayContent.contains("next\n") &&
                  firstDayContent.count('\n') == 2 &&
                  changed.written && !changed.merged,
              "Ten consecutive identical messages merge into one file record");
        const auto nextDay = dailyLog.append(firstTime.addDays(1), "next");
        QFile secondDay(logDir.filePath("2026-10-02.log"));
        check(nextDay.written && !nextDay.merged && secondDay.open(QIODevice::ReadOnly) &&
                  secondDay.readAll().contains("next\n"),
              "Daily log rotates at midnight");
        check(parsed.measurement.reflectorRoiCamera1.width > 0 &&
                  parsed.measurement.reflectorRoiCamera1.height > 0 &&
                  parsed.measurement.reflectorRoiCamera2.width > 0 &&
                  parsed.measurement.reflectorRoiCamera2.height > 0,
              "Manual reflector ROIs parsed");
        check(parsed.crown.diameterThreshold2Mm > parsed.crown.diameterThreshold1Mm,
              "Crown transition diameter thresholds parsed");
        check(parsed.crown.rowMaxFactor > 0 && parsed.crown.searchHalfWidthPx > 0 &&
                  parsed.crown.verticalMarginPx > 0 && parsed.crown.leftMarginPx > 0 &&
                  parsed.body.searchHalfWidthPx > 0 && parsed.body.verticalMarginPx > 0 &&
                  parsed.body.leftMarginPx >= 0,
              "Native-coordinate Crown and Body configuration keys loaded");
        check(QDir::isAbsolutePath(parsed.runtime.offlineImageDir), "Offline directory resolved relative to cnf");
        check(QDir::isAbsolutePath(parsed.runtime.facetteImageDir) &&
                  parsed.runtime.facetteImageDir.contains("FACETTES", Qt::CaseInsensitive),
              "Facette output directory loaded from cnf");
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
        auto renamedConfig = parsed;
        check(pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_row_max_factor", "0.3",
                                    &update, &configError) && update.recognized &&
                  renamedConfig.crown.rowMaxFactor == 0.3 &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_use_previous_boundary_x", "1",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.crown.usePreviousBoundaryX &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_search_half_width_px", "60",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.crown.searchHalfWidthPx == 60 &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_vertical_margin_px", "21",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.crown.verticalMarginPx == 21 &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_left_margin_px", "22",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.crown.leftMarginPx == 22,
              "Crown native-coordinate keys support live updates");
        check(pva::applyConfigEntry(renamedConfig, cnf, "Body", "body_edge_use_previous_boundary_x", "1",
                                    &update, &configError) && update.recognized &&
                  renamedConfig.body.usePreviousBoundaryX &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Body", "body_edge_search_half_width_px", "61",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.body.searchHalfWidthPx == 61 &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Body", "body_edge_vertical_margin_px", "23",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.body.verticalMarginPx == 23 &&
                  pva::applyConfigEntry(renamedConfig, cnf, "Body", "body_edge_left_margin_px", "24",
                                        &update, &configError) && update.recognized &&
                  renamedConfig.body.leftMarginPx == 24,
              "Body native-coordinate keys support live updates");
        check(pva::applyConfigEntry(renamedConfig, cnf, "Crown", "crown_edge_use_previous_boundary_y", "1",
                                    &update, &configError) && !update.recognized,
              "Renamed Crown key has no legacy alias");
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

        // 用现场目录验证：上下拆分后直接提交原始方向的相机图像。
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
            check(!composite.empty() && composite.rows % 2 == 0, "Offline composite height is even");
            if (!composite.empty() && composite.rows % 2 == 0)
            {
                const int middle = composite.rows / 2;
                const cv::Mat camera1 = composite.rowRange(0, middle);
                const cv::Mat camera2 = composite.rowRange(middle, composite.rows);
                auto offlineConfig = parsed;
                offlineConfig.neck.startSearchRatio = 0.0;
                offlineConfig.neck.stopSearchRatio = 1.0;
                pva::MeasurementEngine offlineEngine(offlineConfig);
                const auto result = offlineEngine.process(
                    camera1, camera2,
                    pva::MeasurementStage::Neck);
                check(result.preview1.cols == composite.cols && result.preview2.cols == composite.cols &&
                          result.preview1.rows == middle && result.preview2.rows == middle,
                      "Offline stereo pair submitted to measurement engine");
                check(result.diagnostics.contains("cycle_ms"),
                      "Real offline frame produces Cycle diagnostics");
                check(result.diagnostics.contains("light_camera1") && result.diagnostics.contains("light_camera2"),
                      "Real offline frame produces Process diagnostics");
                check(result.valid, "Real offline Neck frame detects meniscus");

                // 在现场的上下拼接图片上验证新增阶段确实按 PLC 像素坐标取样。
                pva::MeasurementRois liveRois;
                const double cx = camera1.cols / 2.0;
                const double cy = camera1.rows / 2.0;
                check(pva::setPlcRoi(liveRois, "mlt_crd",
                                      {cx, cy, 20, 20, 0, 0}, &plcError) &&
                          pva::setPlcRoi(liveRois, "dip_crd",
                                          {cx, cy, 20}, &plcError),
                      "Real offline image PLC ROIs accepted");
                auto liveConfig = parsed;
                liveConfig.measurement.brightnessMin = 0;
                liveConfig.measurement.brightnessMax = 255;
                pva::MeasurementEngine liveStatsEngine(liveConfig);
                liveStatsEngine.setMeasurementRois(liveRois);
                const auto liveMelt = liveStatsEngine.process(
                    camera1, camera2, pva::MeasurementStage::Melt);
                const auto liveDip = liveStatsEngine.process(
                    camera1, camera2, pva::MeasurementStage::Dip);
                check(liveMelt.valid && liveDip.valid &&
                          liveMelt.diagnostics.contains("melt_average_camera1") &&
                          liveDip.diagnostics.contains("dip_line_average_camera1"),
                      "Real offline stereo image supports Melt and Dip PLC statistics");
                std::vector<pva::OverlayElement> meltOverlay1, meltOverlay2;
                pva::appendPlcRoiOverlays(liveRois, pva::MeasurementStage::Melt, 80,
                                          meltOverlay1, meltOverlay2);
                check(meltOverlay1.size() == 1 && meltOverlay2.size() == 1,
                      "Real offline stereo views each receive a Melt rectangle");
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
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(0, 0, 400, 400);
    config.neck.pixelsPerMm = 10;
    cv::Mat neck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(neck, {200, 180}, {80, 40}, 0, 0, 360, cv::Scalar(220), 5);
    cv::Mat camera2WithoutMeniscus(400, 400, CV_8U, cv::Scalar(100));
    pva::MeasurementEngine engine(config);
    auto neckResult = engine.process(neck, camera2WithoutMeniscus, pva::MeasurementStage::Neck);
    check(neckResult.valid, "Neck synthetic measurement uses Camera 1 without a Camera 2 meniscus");
    check(neckResult.values.diameterMm && std::abs(*neckResult.values.diameterMm - 16.0) < 2.0, "Neck diameter keeps pixels-per-mm calculation");
    check(neckResult.overlay1.size() >= 5 && neckResult.overlay2.size() == 1,
          "Neck overlays Camera 1 detection and only the Camera 2 manual ROI");
    check(neckResult.diagnostics.contains("cycle_ms") && neckResult.diagnostics.at("cycle_ms").toDouble() >= 0.0,
          "Cycle diagnostic populated");
    check(neckResult.diagnostics.contains("neck_major_axis_camera1_px"),
          "Neck process diagnostics populated");
    check(!neckResult.diagnostics.contains("neck_ellipse_vertex_x_camera2_px") &&
              pva::measurementPayloadValues(neckResult).size() == 14 && pva::measurementPayloadValues(neckResult)[3] == 0.0,
          "Neck Camera 2 detection failure reports zero vertex without failing measurement");
    check(std::abs(pva::measurementPayloadValues(neckResult)[2] -
                       neckResult.diagnostics.at("neck_ellipse_vertex_x_camera1_px").toDouble()) < 0.01 &&
              pva::measurementPayloadValues(neckResult)[0] ==
                  neckResult.diagnostics.at("neck_major_axis_camera1_px").toDouble() &&
              pva::measurementPayloadValues(neckResult)[1] == pva::measurementPayloadValues(neckResult)[0] &&
              pva::measurementPayloadValues(neckResult)[4] == cv::mean(neck)[0] &&
              pva::measurementPayloadValues(neckResult)[5] == 220.0 &&
              pva::measurementPayloadValues(neckResult)[6] == 0.0 &&
              pva::measurementPayloadValues(neckResult)[7] == 100.0 &&
              pva::measurementPayloadValues(neckResult)[8] == 100.0 &&
              pva::measurementPayloadValues(neckResult)[9] == 100.0 &&
              pva::measurementPayloadValues(neckResult)[10] ==
                  neckResult.diagnostics.at("neck_center_x_camera1_px").toDouble() &&
              pva::measurementPayloadValues(neckResult)[11] ==
                  neckResult.diagnostics.at("neck_center_y_camera1_px").toDouble() &&
              pva::measurementPayloadValues(neckResult)[12] == 200.0 &&
              pva::measurementPayloadValues(neckResult)[13] == 200.0,
          "Neck diameter payload uses Camera 1 left ellipse vertex");
    auto idleResult = engine.process(neck, camera2WithoutMeniscus, pva::MeasurementStage::Idle);
    check(idleResult.stage == pva::MeasurementStage::Idle && idleResult.overlay1.size() >= 4,
          "Idle uses Neck overlays while preserving Idle stage");

    // 构造带明显内凹缺口的 meniscus，确认拟合点集采用开放的外侧凸弧。
    cv::Mat concaveNeck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(concaveNeck, {200, 190}, {100, 65}, 0, 0, 360, cv::Scalar(220), cv::FILLED);
    cv::rectangle(concaveNeck, {188, 110}, {212, 185}, cv::Scalar(0), cv::FILLED);
    const auto concaveHit = pva::algorithms::findNeckEllipse(concaveNeck, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {});
    check(concaveHit.has_value(),
          "Neck ellipse remains valid for a concave contour");
    const auto croppedNeckHit = pva::algorithms::findNeckEllipse(
        concaveNeck, cv::Rect(80, 80, 240, 220), 10, 80, 0, 1, {});
    check(croppedNeckHit && std::abs(croppedNeckHit->ellipse.center.x - 200.0) < 10.0 &&
              std::abs(croppedNeckHit->ellipse.center.y - 190.0) < 10.0,
          "Neck detection uses the manual reflector ROI and restores full-image coordinates");
    const auto invalidNeckRoiHit = pva::algorithms::findNeckEllipse(
        concaveNeck, cv::Rect(-20, -20, 2, 2), 10, 80, 0, 1, {});
    check(!invalidNeckRoiHit && invalidNeckRoiHit.error.find("ROI") != std::string::npos,
          "Neck detector returns a detailed invalid-ROI reason");
    const auto nativeNeckRange = pva::algorithms::findNeckEllipse(
        neck, cv::Rect(0, 0, 400, 400), 10, 80, .1, .9, {});
    const auto emptyNeckRange = pva::algorithms::findNeckEllipse(
        neck, cv::Rect(300, 0, 100, 400), 10, 80, 0, .5, {});
    const auto invalidNeckRange = pva::algorithms::findNeckEllipse(
        neck, cv::Rect(0, 0, 400, 400), 10, 80, .9, .1, {});
    check(nativeNeckRange && std::abs(nativeNeckRange->ellipse.center.x - 200.0) < 10.0 &&
              !emptyNeckRange && emptyNeckRange.error.find("ROI") != std::string::npos &&
              !invalidNeckRange && invalidNeckRange.error.find("ratios") != std::string::npos,
          "Neck search ratios use native x and reject empty or reversed ranges");
    const auto invalidNeckRatioHit = pva::algorithms::findNeckEllipse(
        neck, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {}, 0.0);
    check(!invalidNeckRatioHit && invalidNeckRatioHit.error.find("ratio") != std::string::npos,
          "Neck detector rejects an invalid constrained axis ratio");
    auto invalidNeckConfig = config;
    invalidNeckConfig.measurement.reflectorRoiCamera1 = cv::Rect(-20, -20, 2, 2);
    const auto invalidNeckResult = pva::MeasurementEngine(invalidNeckConfig).process(
        neck, camera2WithoutMeniscus, pva::MeasurementStage::Neck);
    check(!invalidNeckResult.valid &&
              invalidNeckResult.message.find("Camera 1") != std::string::npos &&
              invalidNeckResult.message.find("ROI") != std::string::npos,
          "Neck detector failure reason reaches MeasurementResult message");

    // Neck 中两台相机均检出时，两侧顶点分别来自本次拟合。
    cv::Mat camera2Neck = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(camera2Neck, {240, 180}, {110, 45}, 0, 0, 360, cv::Scalar(220), 5);
    pva::MeasurementEngine stereoNeckEngine(config);
    const auto stereoNeckResult = stereoNeckEngine.process(
        neck, camera2Neck, pva::MeasurementStage::Neck);
    check(stereoNeckResult.valid &&
              stereoNeckResult.diagnostics.contains("neck_major_axis_camera2_px") &&
              stereoNeckResult.overlay2.size() >= 5 &&
              pva::measurementPayloadValues(stereoNeckResult).size() == 14 &&
              std::abs(pva::measurementPayloadValues(stereoNeckResult)[3] -
                       stereoNeckResult.diagnostics.at("neck_ellipse_vertex_x_camera2_px").toDouble()) < 0.01 &&
              pva::measurementPayloadValues(stereoNeckResult)[1] ==
                  stereoNeckResult.diagnostics.at("neck_major_axis_camera2_px").toDouble() &&
              pva::measurementPayloadValues(stereoNeckResult)[12] ==
                  stereoNeckResult.diagnostics.at("neck_center_x_camera2_px").toDouble(),
          "Neck Camera 2 detection adds overlay and its left ellipse vertex");
    const auto camera1Hit = pva::algorithms::findNeckEllipse(
        neck, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {});
    const auto constrainedCamera2Hit = camera1Hit ? pva::algorithms::findNeckEllipse(
        camera2Neck, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {},
        config.neck.ellipseWidthHeightRatioCamera2) :
        pva::algorithms::DetectionResult<pva::algorithms::EllipseHit>{};
    check(camera1Hit && constrainedCamera2Hit &&
              std::abs(double(constrainedCamera2Hit->ellipse.size.width) /
                           constrainedCamera2Hit->ellipse.size.height -
                       config.neck.ellipseWidthHeightRatioCamera2) < 1e-5 &&
              std::abs(constrainedCamera2Hit->ellipse.center.x - 240.0) < 20.0 &&
              std::abs(constrainedCamera2Hit->ellipse.center.y - 180.0) < 20.0 &&
              std::abs(constrainedCamera2Hit->ellipse.size.width - camera1Hit->ellipse.size.width) > 10.0 &&
              std::abs(pva::measurementPayloadValues(stereoNeckResult)[3] -
                       (constrainedCamera2Hit->ellipse.center.x - constrainedCamera2Hit->ellipse.size.width * 0.5)) < 0.01,
          "Camera 2 uses configured axis ratio and its own center, size and PLC vertex");

    cv::Mat verticalCamera1 = cv::Mat::zeros(400, 400, CV_8U);
    cv::Mat verticalCamera2 = cv::Mat::zeros(400, 400, CV_8U);
    cv::ellipse(verticalCamera1, {180, 180}, {40, 80}, 0, 0, 360, cv::Scalar(220), 5);
    cv::ellipse(verticalCamera2, {230, 175}, {55, 105}, 0, 0, 360, cv::Scalar(220), 5);
    config.neck.ellipseWidthHeightRatioCamera2 = .7;
    const auto verticalResult = pva::MeasurementEngine(config).process(
        verticalCamera1, verticalCamera2, pva::MeasurementStage::Neck);
    const auto verticalFirst = pva::algorithms::findNeckEllipse(
        verticalCamera1, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {});
    const auto verticalSecond = verticalFirst ? pva::algorithms::findNeckEllipse(
        verticalCamera2, cv::Rect(0, 0, 400, 400), 10, 80, 0, 1, {},
        config.neck.ellipseWidthHeightRatioCamera2) :
        pva::algorithms::DetectionResult<pva::algorithms::EllipseHit>{};
    check(verticalResult.valid && verticalFirst && verticalSecond &&
              verticalFirst->ellipse.size.height > verticalFirst->ellipse.size.width &&
              verticalSecond->ellipse.size.height > verticalSecond->ellipse.size.width &&
              std::abs(config.neck.ellipseWidthHeightRatioCamera2 -
                       double(verticalSecond->ellipse.size.width) / verticalSecond->ellipse.size.height) < 1e-5,
          "Camera 2 uses its configured vertical axis ratio independently of Camera 1");

    cv::Mat meniscus(300, 260, CV_8U, cv::Scalar(20));
    for (int y = 20; y < 280; ++y)
    {
        const int boundary = static_cast<int>(140 - .002 * (y - 150) * (y - 150));
        meniscus(cv::Rect(260 - boundary, y, std::max(boundary, 1), 1)).setTo(220);
    }
    config.crown.verticalMarginPx = 10;
    config.crown.leftMarginPx = 10;
    config.crown.minEdgePoints = 20;
    config.crown.rowMaxFactor = .2;
    config.measurement.reflectorRoiCamera1 = config.measurement.reflectorRoiCamera2 = cv::Rect(29, 20, 231, 261);
    pva::MeasurementState crownState;
    crownState.validNeck = true;
    crownState.values.diameterMm = 5.0;
    crownState.neckCentersPx = std::array<cv::Point2d, 2>{cv::Point2d(239, 140), cv::Point2d(239, 160)};
    pva::MeasurementEngine crownEngine(config, crownState);
    auto crownResult = crownEngine.process(meniscus, meniscus, pva::MeasurementStage::Crown);
    check(crownResult.valid, "Crown manual-ROI lower vertex valid");
    check(!crownResult.diagnostics.contains("neck_major_axis_camera1_px") &&
              !crownResult.diagnostics.contains("neck_major_axis_camera2_px") &&
              crownResult.values.diameterMm &&
              *crownResult.values.diameterMm == 5.0,
          "Crown fits curves even below former diameter threshold without updating Neck diameter");
    check(pva::measurementPayloadValues(crownResult).size() == 14 &&
              pva::measurementPayloadValues(crownResult)[2] ==
                  crownResult.diagnostics.at("crown_boundary_camera1_px").toList()[0].toDouble() &&
              pva::measurementPayloadValues(crownResult)[3] ==
                  crownResult.diagnostics.at("crown_boundary_camera2_px").toList()[0].toDouble() &&
              pva::measurementPayloadValues(crownResult)[0] == 5.0 && pva::measurementPayloadValues(crownResult)[1] == 5.0,
          "Crown diameter payload vertices use both fitted curve boundary x coordinates");
    const auto missingNeckCrown = pva::MeasurementEngine(config).process(
        meniscus, meniscus, pva::MeasurementStage::Crown);
    check(!missingNeckCrown.valid &&
              missingNeckCrown.message.find("valid Camera 1 neck reference") != std::string::npos,
          "Crown requires an existing Neck reference");
    const auto hasStoredNeckCenter = [](const std::vector<pva::OverlayElement> &overlays,
                                        cv::Point2d expectedCenter)
    {
        return std::ranges::any_of(overlays, [expectedCenter](const pva::OverlayElement &element)
                                  { return element.type == pva::OverlayType::Cross &&
                                           !element.points.empty() &&
                                           cv::norm(element.points.front() - expectedCenter) < 0.01 &&
                                           element.colorBgr == cv::Scalar(0, 255, 0); });
    };
    check(hasStoredNeckCenter(crownResult.overlay1, {239, 140}) &&
              hasStoredNeckCenter(crownResult.overlay2, {239, 160}),
          "Crown overlays retain both stored Neck centers");
    check(std::ranges::any_of(crownResult.overlay1, [](const pva::OverlayElement &element)
                             { return element.type == pva::OverlayType::Polyline && element.closed &&
                                      element.colorBgr == cv::Scalar(0, 255, 0); }),
          "Crown draws the manual reflector ROI as a green rectangle");
    check(crownEngine.state().crownBoundaryPointsPx &&
              std::abs((*crownEngine.state().crownBoundaryPointsPx)[0].y - 140.0) < 0.01 &&
              std::abs((*crownEngine.state().crownBoundaryPointsPx)[1].y - 160.0) < 0.01 &&
              (*crownEngine.state().crownBoundaryPointsPx)[0].x > 90 &&
              (*crownEngine.state().crownBoundaryPointsPx)[0].x < 150,
          "Crown lower vertices use the stored Neck center y coordinates");
    check(crownResult.diagnostics.size() >= 40 &&
              crownResult.diagnostics.contains("crown_boundary_camera1_px") &&
              crownResult.diagnostics.contains("crown_row_strengths_maximum_camera2") &&
              crownResult.diagnostics.contains("crown_edge_model"),
          "Crown Process diagnostics match the Python field set");
    auto invalidCrownConfig = config;
    invalidCrownConfig.measurement.reflectorRoiCamera1 = cv::Rect(0, 0, 1, 1);
    const auto invalidCrownResult = pva::MeasurementEngine(invalidCrownConfig, crownState).process(
        meniscus, meniscus, pva::MeasurementStage::Crown);
    check(!invalidCrownResult.valid &&
              invalidCrownResult.message.find("Crown meniscus detection failed") != std::string::npos &&
              invalidCrownResult.message.find("Camera 1") != std::string::npos,
          "Crown detector failure identifies the failed camera and reason");
    config.body.verticalMarginPx = 10;
    config.body.leftMarginPx = 10;
    config.body.minEdgePoints = 20;
    config.body.startSearchRatio = 0;
    config.body.stopSearchRatio = 1;
    config.body.minCoverageRatio = .5;
    config.body.brightnessOffsetCamera1 = config.body.brightnessOffsetCamera2 = 20;
    pva::MeasurementEngine bodyEngine(config, crownState);
    auto bodyResult = bodyEngine.process(meniscus, meniscus, pva::MeasurementStage::Body);
    check(bodyResult.valid, "Body manual-ROI lower vertex valid");
    check(pva::measurementPayloadValues(bodyResult).size() == 14 &&
              pva::measurementPayloadValues(bodyResult)[2] ==
                  bodyResult.diagnostics.at("body_boundary_camera1_px").toList()[0].toDouble() &&
              pva::measurementPayloadValues(bodyResult)[3] ==
                  bodyResult.diagnostics.at("body_boundary_camera2_px").toList()[0].toDouble(),
          "Body diameter payload vertices use both fitted curve boundary x coordinates");
    check(hasStoredNeckCenter(bodyResult.overlay1, {239, 140}) &&
              hasStoredNeckCenter(bodyResult.overlay2, {239, 160}),
          "Body overlays retain both stored Neck centers");
    check(bodyEngine.state().bodyBoundaryPointsPx &&
              std::abs((*bodyEngine.state().bodyBoundaryPointsPx)[0].y - 140.0) < 0.01 &&
              std::abs((*bodyEngine.state().bodyBoundaryPointsPx)[1].y - 160.0) < 0.01 &&
              (*bodyEngine.state().bodyBoundaryPointsPx)[0].x > 90 &&
              (*bodyEngine.state().bodyBoundaryPointsPx)[0].x < 150,
          "Body lower vertices use the stored Neck center y coordinates");
    check(bodyResult.diagnostics.size() >= 40 &&
              bodyResult.diagnostics.contains("body_boundary_camera1_px") &&
              bodyResult.diagnostics.contains("body_row_maximum_p90_camera2") &&
              bodyResult.diagnostics.contains("body_edge_model"),
          "Body Process diagnostics match the Python field set");
    auto invalidBodyConfig = config;
    invalidBodyConfig.measurement.reflectorRoiCamera2 = cv::Rect(0, 0, 1, 1);
    const auto invalidBodyResult = pva::MeasurementEngine(invalidBodyConfig, crownState).process(
        meniscus, meniscus, pva::MeasurementStage::Body);
    check(!invalidBodyResult.valid &&
              invalidBodyResult.message.find("Body meniscus detection failed") != std::string::npos &&
              invalidBodyResult.message.find("Camera 2") != std::string::npos,
          "Body detector failure identifies the failed camera and reason");

    // 非正方形图像与偏移 ROI：边界、拟合曲线和历史跟踪均保持当前图像坐标。
    cv::Mat shiftedMeniscus(420, 340, CV_8U, cv::Scalar(20));
    meniscus.copyTo(shiftedMeniscus(cv::Rect(35, 45, meniscus.cols, meniscus.rows)));
    const cv::Rect shiftedRoi(64, 65, 231, 261);
    const cv::Point2d shiftedCenter(274, 185);
    const auto shiftedCrown = pva::algorithms::findCrownMeniscus(
        shiftedMeniscus, shiftedRoi, shiftedCenter, config.crown, {});
    const auto trackedCrown = shiftedCrown ? pva::algorithms::findCrownMeniscus(
        shiftedMeniscus, shiftedRoi, shiftedCenter, config.crown, shiftedCrown->boundary.x) :
        pva::algorithms::DetectionResult<pva::algorithms::CurveHit>{};
    auto nativeBodySettings = config.body;
    nativeBodySettings.startSearchRatio = .2;
    nativeBodySettings.stopSearchRatio = .8;
    const auto shiftedBody = pva::algorithms::findBodyMeniscus(
        shiftedMeniscus, shiftedRoi, shiftedCenter, nativeBodySettings, 20, {});
    const auto trackedBody = shiftedBody ? pva::algorithms::findBodyMeniscus(
        shiftedMeniscus, shiftedRoi, shiftedCenter, nativeBodySettings, 20, shiftedBody->boundary.x) :
        pva::algorithms::DetectionResult<pva::algorithms::CurveHit>{};
    check(shiftedCrown && trackedCrown && shiftedBody && trackedBody &&
              std::abs(shiftedCrown->boundary.x - 155.0) < 10.0 &&
              std::abs(shiftedBody->boundary.x - 155.0) < 10.0 &&
              shiftedCrown->boundary.y == shiftedCenter.y &&
              shiftedBody->boundary.y == shiftedCenter.y &&
              cv::norm(trackedCrown->boundary - shiftedCrown->boundary) < 2.0 &&
              cv::norm(trackedBody->boundary - shiftedBody->boundary) < 2.0 &&
              !shiftedCrown->edges.empty() && !shiftedBody->curve.empty() &&
              shiftedCrown->edges.front().y >= shiftedRoi.y &&
              shiftedBody->curve.front().y >= shiftedRoi.y &&
              shiftedBody->searchStartX == 68 && shiftedBody->searchStopX == 272,
          "Crown and Body use native coordinates on shifted non-square images, including previous-boundary tracking");
    // ROI 外亮度改变不应影响 ROI 内滤波、边界点及曲线拟合。
    cv::Mat outsideChanged(shiftedMeniscus.size(), CV_8U, cv::Scalar(255));
    shiftedMeniscus(shiftedRoi).copyTo(outsideChanged(shiftedRoi));
    const auto isolatedCrown = pva::algorithms::findCrownMeniscus(
        outsideChanged, shiftedRoi, shiftedCenter, config.crown, {});
    const auto isolatedBody = pva::algorithms::findBodyMeniscus(
        outsideChanged, shiftedRoi, shiftedCenter, nativeBodySettings, 20, {});
    check(shiftedCrown && isolatedCrown && shiftedBody && isolatedBody &&
              cv::norm(shiftedCrown->boundary - isolatedCrown->boundary) < 1e-6 &&
              cv::norm(shiftedBody->boundary - isolatedBody->boundary) < 1e-6 &&
              shiftedCrown->edges == isolatedCrown->edges && shiftedBody->edges == isolatedBody->edges,
          "Crown and Body preprocessing is isolated from pixels outside the configured ROI");
    nativeBodySettings.startSearchRatio = .8;
    nativeBodySettings.stopSearchRatio = 1.0;
    const auto emptyBodyRange = pva::algorithms::findBodyMeniscus(
        shiftedMeniscus, cv::Rect(0, 65, 80, 261), shiftedCenter, nativeBodySettings, 20, {});
    check(!emptyBodyRange && emptyBodyRange.error.find("horizontal search range") != std::string::npos,
          "Body rejects a native-x ratio window that does not intersect the ROI");

    pva::MeasurementState state;
    state.validNeck = true;
    state.mmPerPixel = .1;
    state.neckYSpans = std::array<cv::Vec2i, 2>{cv::Vec2i(50, 150), cv::Vec2i(50, 150)};
    state.bodyCentersPx = std::array<cv::Point2d, 2>{cv::Point2d(149, 100), cv::Point2d(149, 100)};
    cv::Mat endcone(200, 250, CV_8U, cv::Scalar(200));
    endcone.colRange(0, 100).setTo(20);
    const auto roiEndcone = pva::algorithms::findEndcone(
        endcone, cv::Rect(75, 60, 100, 80), {149, 100}, {50, 150}, .1, config.endcone);
    const auto emptyEndcone = pva::algorithms::findEndcone(
        endcone, cv::Rect(180, 60, 60, 80), {149, 100}, {50, 150}, .1, config.endcone);
    check(roiEndcone && roiEndcone->boundaryX == 99 && roiEndcone->y0 == 60 &&
              roiEndcone->y1 == 140 && !emptyEndcone,
          "Endcone clips search to ROI and keeps native output coordinates");
    pva::MeasurementEngine endconeEngine(config, state);
    auto endconeResult = endconeEngine.process(endcone, endcone, pva::MeasurementStage::Endcone);
    check(endconeResult.valid, "Endcone state-based measurement valid");
    check(endconeResult.values.diameterMm && std::abs(*endconeResult.values.diameterMm - 4.9) < .3, "Endcone diameter unchanged");
    check(endconeResult.diagnostics.contains("boundary_x_px") &&
              std::abs(endconeResult.diagnostics.at("boundary_x_px").toDouble() - 99.0) < 1.0,
          "Endcone boundary is expressed in the native x coordinate");
    auto invalidEndconeState = state;
    invalidEndconeState.neckYSpans = std::array<cv::Vec2i, 2>{cv::Vec2i(50, 150), cv::Vec2i(100, 100)};
    const auto invalidEndconeResult = pva::MeasurementEngine(config, invalidEndconeState).process(
        endcone, endcone, pva::MeasurementStage::Endcone);
    check(!invalidEndconeResult.valid &&
              invalidEndconeResult.message.find("Camera 2") != std::string::npos &&
              invalidEndconeResult.message.find("y-span") != std::string::npos,
          "Endcone detector failure reason reaches MeasurementResult message");

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
