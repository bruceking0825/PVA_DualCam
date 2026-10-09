#include "measurement_engine.hpp"
#include "algorithms/detectors.hpp"
#include <opencv2/core.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    double ema(double previous, double raw, double alpha)
    {
        alpha = std::clamp(alpha, 0.0, 1.0);
        return !std::isfinite(previous) || previous == 0 ? raw : previous + (1 - alpha) * (raw - previous);
    }
    double ema(std::optional<double> previous, double raw, double alpha) { return previous ? ema(*previous, raw, alpha) : raw; }
    void addCurveOverlay(const pva::algorithms::CurveHit &hit, std::vector<pva::OverlayElement> &out)
    {
        out.push_back({pva::OverlayType::Polyline, hit.edges, {0, 150, 0}, 1, false});
        out.push_back({pva::OverlayType::Polyline, hit.curve, {0, 255, 0}, 2, false});
        out.push_back({pva::OverlayType::Cross, {hit.boundary}, {0, 0, 255}, 2, false});
    }
    void addNeckOverlay(const pva::algorithms::EllipseHit &hit, std::vector<pva::OverlayElement> &out)
    {
        // 边界轮廓、轴对齐拟合椭圆、中心和原始方向的左顶点分层显示。
        std::vector<cv::Point2d> points;
        for (auto p : hit.contour)
            points.emplace_back(p);
        out.push_back({pva::OverlayType::Polyline, points, {0, 255, 0}, 2, hit.contourClosed});

        std::vector<cv::Point2d> ellipsePoints;
        ellipsePoints.reserve(181);
        const double radiusX = hit.ellipse.size.width * 0.5;
        const double radiusY = hit.ellipse.size.height * 0.5;
        for (int index = 0; index < 181; ++index)
        {
            const double angle = 2.0 * CV_PI * index / 181.0;
            ellipsePoints.emplace_back(
                hit.ellipse.center.x + radiusX * std::cos(angle),
                hit.ellipse.center.y + radiusY * std::sin(angle));
        }
        out.push_back({pva::OverlayType::Polyline, ellipsePoints, {0, 255, 0}, 2, true});
        out.push_back({pva::OverlayType::Cross, {hit.ellipse.center}, {0, 255, 0}, 2, false});
        out.push_back({pva::OverlayType::Cross,
                       {cv::Point2d(hit.ellipse.center.x - radiusX, hit.ellipse.center.y)},
                       {0, 0, 255},
                       2,
                       false});
    }
    cv::Rect clippedRoi(const cv::Rect &configuredRoi, const cv::Size &imageSize)
    {
        return configuredRoi & cv::Rect(0, 0, imageSize.width, imageSize.height);
    }

    cv::Point2d roiRightCenter(const cv::Rect &configuredRoi, const cv::Size &imageSize)
    {
        const cv::Rect roi = clippedRoi(configuredRoi, imageSize);
        return {double(roi.x + roi.width - 1), roi.y + (roi.height - 1) * 0.5};
    }

    std::pair<bool, std::string> applyCamera1Neck(
        const pva::algorithms::EllipseHit &hit,
        const cv::Size &camera2Size,
        const pva::MeasurementConfig &config,
        pva::MeasurementState &state,
        pva::MeasurementResult &result,
        bool resetFollowingStages)
    {
        if (hit.contour.size() < size_t(config.neck.minEdgePoints))
            return {false, "Not enough Camera 1 neck edge points"};
        if (!(config.neck.pixelsPerMm > 0.0))
            return {false, "Neck pixels-per-mm must be positive"};

        const double majorAxis = std::max(hit.ellipse.size.width, hit.ellipse.size.height);
        const double rawDiameter = majorAxis / config.neck.pixelsPerMm;
        if (!(rawDiameter > config.measurement.diameterMinMm &&
              rawDiameter < config.measurement.diameterMaxMm))
            return {false, "Neck diameter is outside physical limits"};

        result.diagnostics["neck_contour_area_camera1_px"] = hit.area;
        result.diagnostics["neck_gradient_maximum_camera1"] = hit.maximumGradient;
        result.diagnostics["neck_edge_points_camera1"] = static_cast<double>(hit.contour.size());
        result.diagnostics["neck_center_x_camera1_px"] = hit.ellipse.center.x;
        result.diagnostics["neck_center_y_camera1_px"] = hit.ellipse.center.y;
        result.diagnostics["neck_ellipse_vertex_x_camera1_px"] =
            hit.ellipse.center.x - hit.ellipse.size.width * 0.5;
        result.diagnostics["neck_major_axis_camera1_px"] = majorAxis;
        result.diagnostics["neck_pixels_per_mm"] = config.neck.pixelsPerMm;
        result.diagnostics["raw_diameter_mm"] = rawDiameter;

        state.values.diameterMm = ema(state.values.diameterMm, rawDiameter, config.neck.diameterAlpha);
        state.neckCentersPx = std::array<cv::Point2d, 2>{
            hit.ellipse.center,
            roiRightCenter(config.measurement.reflectorRoiCamera2, camera2Size)};
        state.validNeck = true;
        if (resetFollowingStages)
        {
            state.crownBoundaryPointsPx.reset();
            state.bodyBoundaryPointsPx.reset();
        }
        addNeckOverlay(hit, result.overlay1);
        return {true, {}};
    }

    std::pair<bool, std::string> applyCamera2NeckReference(
        const pva::algorithms::EllipseHit &hit,
        const pva::MeasurementConfig &config,
        pva::MeasurementState &state,
        pva::MeasurementResult &result)
    {
        if (hit.contour.size() < size_t(config.neck.minEdgePoints))
            return {false, "Not enough Camera 2 neck edge points"};
        if (!state.neckCentersPx)
            return {false, "Camera 2 neck reference requires a valid Camera 1 result"};

        const double majorAxis = std::max(hit.ellipse.size.width, hit.ellipse.size.height);
        result.diagnostics["neck_contour_area_camera2_px"] = hit.area;
        result.diagnostics["neck_gradient_maximum_camera2"] = hit.maximumGradient;
        result.diagnostics["neck_edge_points_camera2"] = static_cast<double>(hit.contour.size());
        result.diagnostics["neck_center_x_camera2_px"] = hit.ellipse.center.x;
        result.diagnostics["neck_center_y_camera2_px"] = hit.ellipse.center.y;
        result.diagnostics["neck_ellipse_vertex_x_camera2_px"] =
            hit.ellipse.center.x - hit.ellipse.size.width * 0.5;
        result.diagnostics["neck_major_axis_camera2_px"] = majorAxis;

        // Camera 2 只提供过渡阶段的空间参考；直径和毫米比例始终由 Camera 1 更新。
        (*state.neckCentersPx)[1] = hit.ellipse.center;
        addNeckOverlay(hit, result.overlay2);
        return {true, {}};
    }

    void addStoredNeckCenterOverlays(const pva::MeasurementState &state, pva::MeasurementResult &result)
    {
        if (!state.neckCentersPx)
            return;

        // Crown/Body 沿用最近一次有效 Neck 拟合中心；检测失败时也保留该参考标记。
        result.overlay1.push_back(
            {pva::OverlayType::Cross, {(*state.neckCentersPx)[0]}, {0, 255, 0}, 2, false});
        result.overlay2.push_back(
            {pva::OverlayType::Cross, {(*state.neckCentersPx)[1]}, {0, 255, 0}, 2, false});
    }

    void addRoiOverlay(const cv::Rect &configuredRoi, const cv::Size &imageSize,
                       std::vector<pva::OverlayElement> &out)
    {
        const cv::Rect roi = configuredRoi & cv::Rect(0, 0, imageSize.width, imageSize.height);
        if (roi.width <= 0 || roi.height <= 0)
            return;
        const double right = roi.x + roi.width - 1;
        const double bottom = roi.y + roi.height - 1;
        out.push_back({pva::OverlayType::Polyline,
                       {{double(roi.x), double(roi.y)}, {right, double(roi.y)},
                        {right, bottom}, {double(roi.x), bottom}},
                       {0, 255, 0}, 1, true, true});
    }

    void initializeCrownDiagnostics(pva::MeasurementResult &result)
    {
        static const std::array<const char *, 17> cameraKeys{
            "crown_left_margin_camera%1_px", "crown_tracking_half_width_camera%1_px",
            "crown_row_strengths_mean_camera%1", "crown_row_strengths_maximum_camera%1",
            "crown_minimum_strength_camera%1", "crown_kept_row_count_camera%1",
            "crown_seed_x_camera%1_px", "crown_edge_point_count_camera%1",
            "crown_residual_limit_camera%1_px", "crown_robust_inlier_count_camera%1",
            "crown_sagitta_camera%1_px", "crown_center_camera%1_px",
            "crown_boundary_camera%1_px", "crown_edge_seed_x_camera%1_px",
            "crown_edge_coverage_camera%1", "crown_edge_fit_error_camera%1_px",
            "crown_fit_strengths_mean_camera%1"};
        for (int camera = 1; camera <= 2; ++camera)
            for (const char *pattern : cameraKeys)
                result.diagnostics.emplace(QString::fromLatin1(pattern).arg(camera).toStdString(), QVariant{});
        result.diagnostics.emplace("source", QVariant{});
        result.diagnostics.emplace("crown_edge_previous_tracking_active", QVariant{});
        result.diagnostics.emplace("crown_edge_model", QVariant{});
    }

    void initializeBodyDiagnostics(pva::MeasurementResult &result)
    {
        static const std::array<const char *, 19> cameraKeys{
            "body_search_start_x_camera%1_px", "body_search_stop_x_camera%1_px",
            "body_left_margin_camera%1_px", "body_tracking_half_width_camera%1_px",
            "body_brightness_threshold_percent_camera%1", "body_threshold_crossing_count_camera%1",
            "body_row_maximum_p90_camera%1", "body_row_maximum_maximum_camera%1",
            "body_residual_limit_camera%1_px", "body_robust_inlier_count_camera%1",
            "body_coverage_ratio_camera%1", "body_sagitta_camera%1_px",
            "body_center_camera%1_px", "body_boundary_camera%1_px",
            "body_edge_seed_x_camera%1_px", "body_edge_coverage_camera%1",
            "body_fit_strength_mean_camera%1", "body_edge_fit_error_camera%1_px",
            "body_edge_point_count_camera%1"};
        for (int camera = 1; camera <= 2; ++camera)
            for (const char *pattern : cameraKeys)
                result.diagnostics.emplace(QString::fromLatin1(pattern).arg(camera).toStdString(), QVariant{});
        result.diagnostics.emplace("source", QVariant{});
        result.diagnostics.emplace("body_edge_previous_tracking_active", QVariant{});
        result.diagnostics.emplace("body_edge_model", QVariant{});
    }

    QVariant pointValue(const cv::Point2d &point)
    {
        return QVariantList{point.x, point.y};
    }

    std::string detectionFailure(const std::string &context, const std::string &reason)
    {
        return context + ": " + (reason.empty() ? "no failure detail was provided" : reason);
    }

    template <typename FirstResult, typename SecondResult>
    std::string stereoDetectionFailure(const char *stage,
                                       const FirstResult &first,
                                       const SecondResult &second)
    {
        std::string message = std::string(stage) + " meniscus detection failed";
        if (!first)
            message += "; Camera 1: " +
                       (first.error.empty() ? "no failure detail was provided" : first.error);
        if (!second)
            message += "; Camera 2: " +
                       (second.error.empty() ? "no failure detail was provided" : second.error);
        return message;
    }
}

namespace pva
{
    MeasurementEngine::MeasurementEngine(MeasurementConfig config, MeasurementState state) : config_(std::move(config)), state_(std::move(state)) {}

    MeasurementResult MeasurementEngine::process(const cv::Mat &a, const cv::Mat &b, MeasurementStage stage)
    {
        const auto started = std::chrono::steady_clock::now();
        MeasurementResult result;
        result.stage = stage;
        if (stage == MeasurementStage::Crown)
            initializeCrownDiagnostics(result);
        else if (stage == MeasurementStage::Body)
            initializeBodyDiagnostics(result);
        result.preview1 = algorithms::normalizeGray8(a);
        result.preview2 = algorithms::normalizeGray8(b);
        if (result.preview1.empty() || result.preview2.empty())
        {
            result.message = "Camera image is empty";
            result.diagnostics["cycle_ms"] = 0.0;
            return result;
        }
        addRoiOverlay(config_.measurement.reflectorRoiCamera1, result.preview1.size(), result.overlay1);
        addRoiOverlay(config_.measurement.reflectorRoiCamera2, result.preview2.size(), result.overlay2);
        double minimum1 = 0, light1 = 0, minimum2 = 0, light2 = 0;
        cv::minMaxLoc(result.preview1, &minimum1, &light1);
        cv::minMaxLoc(result.preview2, &minimum2, &light2);
        state_.filteredLight[0] = ema(state_.filteredLight[0], light1, config_.measurement.lightAlpha);
        state_.filteredLight[1] = ema(state_.filteredLight[1], light2, config_.measurement.lightAlpha);
        result.diagnostics["light_camera1"] = state_.filteredLight[0];
        result.diagnostics["light_camera2"] = state_.filteredLight[1];
        if (state_.filteredLight[0] < config_.measurement.brightnessMin || state_.filteredLight[0] > config_.measurement.brightnessMax || state_.filteredLight[1] < config_.measurement.brightnessMin || state_.filteredLight[1] > config_.measurement.brightnessMax)
        {
            result.message = "Brightness is outside configured limits";
            result.values = state_.values;
            result.diagnostics["cycle_ms"] = std::chrono::duration<double, std::milli>(
                                                 std::chrono::steady_clock::now() - started)
                                                 .count();
            return result;
        }
        if (stage == MeasurementStage::Neck || stage == MeasurementStage::Crown ||
            stage == MeasurementStage::Body)
        {
            const double previousDiameter = state_.values.diameterMm.value_or(0.0);
            result.data.hasDiameter = true;
            result.data.cameras[0] = {previousDiameter, 0.0, cv::mean(result.preview1)[0], light1, minimum1,
                                      {result.preview1.cols * 0.5, result.preview1.rows * 0.5}};
            result.data.cameras[1] = {previousDiameter, 0.0, cv::mean(result.preview2)[0], light2, minimum2,
                                      {result.preview2.cols * 0.5, result.preview2.rows * 0.5}};
        }
        std::pair<bool, std::string> outcome;
        if (stage == MeasurementStage::Melt)
            outcome = processMelt(result.preview1, result.preview2, result);
        else if (stage == MeasurementStage::Dip)
            outcome = processDip(result.preview1, result);
        else if (stage == MeasurementStage::Neck)
            outcome = processNeck(result.preview1, result.preview2, result);
        else if (stage == MeasurementStage::Crown)
            outcome = processCrown(result.preview1, result.preview2, result);
        else if (stage == MeasurementStage::Body)
            outcome = processBody(result.preview1, result.preview2, result);
        else
            outcome = {false, "Unsupported stage"};
        result.valid = outcome.first;
        result.message = outcome.second;
        if (!result.valid)
            result.data = {};
        result.values = state_.values;
        result.diagnostics["cycle_ms"] = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - started)
                                             .count();
        return result;
    }

    std::pair<bool, std::string> MeasurementEngine::processMelt(
        const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        if (!rois_.melt)
            return {false, "Melt requires mlt_crd from PLC"};
        const auto first = meltRoiStats(a, *rois_.melt, 1);
        const auto second = meltRoiStats(b, *rois_.melt, 2);
        if (!first || !second)
            return {false, "Melt ROI is outside camera image"};
        r.diagnostics["melt_average_camera1"] = first->average;
        r.diagnostics["melt_maximum_camera1"] = first->maximum;
        r.diagnostics["melt_minimum_camera1"] = first->minimum;
        r.diagnostics["melt_average_camera2"] = second->average;
        r.diagnostics["melt_maximum_camera2"] = second->maximum;
        r.diagnostics["melt_minimum_camera2"] = second->minimum;
        r.data.hasMelt = true;
        r.data.cameras[0] = {0, 0, first->average, first->maximum, first->minimum, {}};
        r.data.cameras[1] = {0, 0, second->average, second->maximum, second->minimum, {}};
        return {true, "Melt ROI statistics updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processDip(
        const cv::Mat &a, MeasurementResult &r)
    {
        if (!rois_.dip)
            return {false, "Dip requires dip_crd from PLC"};
        const auto average = dipLineMean(a, *rois_.dip);
        if (!average)
            return {false, "Dip line is outside Camera 1 image"};
        r.diagnostics["dip_line_average_camera1"] = *average;
        r.data.hasDip = true;
        r.data.dipAverage = *average;
        return {true, "Dip line statistics updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processNeck(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        auto first = algorithms::findNeckEllipse(a, config_.measurement.reflectorRoiCamera1, config_.neck.gradientThresholdPercentCamera1, config_.neck.minContourAreaPx, config_.neck.startSearchRatio, config_.neck.stopSearchRatio, {});
        if (!first)
            return {false, detectionFailure("Camera 1 neck meniscus detection failed", first.error)};
        const auto updated = applyCamera1Neck(*first, b.size(), config_, state_, r, true);
        if (!updated.first)
            return updated;
        const double majorAxis1 = std::max(first->ellipse.size.width, first->ellipse.size.height);
        r.data.cameras[0].diameter = majorAxis1;
        r.data.cameras[1].diameter = majorAxis1;
        r.data.cameras[0].boundaryX = first->ellipse.center.x - first->ellipse.size.width * 0.5;
        r.data.cameras[0].center.x = first->ellipse.center.x;
        r.data.cameras[0].center.y = first->ellipse.center.y;
        // 相机 2 仅补充空间参考和 PLC 顶点；未检出不影响相机 1 的 Neck 测量。
        const auto second = algorithms::findNeckEllipse(
            b, config_.measurement.reflectorRoiCamera2,
            config_.neck.gradientThresholdPercentCamera2, config_.neck.minContourAreaPx,
            config_.neck.startSearchRatio, config_.neck.stopSearchRatio, {},
            config_.neck.ellipseWidthHeightRatioCamera2);
        if (second && applyCamera2NeckReference(*second, config_, state_, r).first)
        {
            r.data.cameras[1].diameter = std::max(second->ellipse.size.width, second->ellipse.size.height);
            r.data.cameras[1].boundaryX = second->ellipse.center.x - second->ellipse.size.width * 0.5;
            r.data.cameras[1].center.x = second->ellipse.center.x;
            r.data.cameras[1].center.y = second->ellipse.center.y;
        }
        return {true, "Neck measurement updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processCrown(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        // Crown 始终拟合双相机曲线，不在此阶段重新检测 Neck 椭圆。
        if (!state_.validNeck || !state_.neckCentersPx)
            return {false, "Crown meniscus requires a valid Camera 1 neck reference"};

        std::optional<double> p1, p2;
        if (config_.crown.usePreviousBoundaryX && state_.crownBoundaryPointsPx)
        {
            p1 = (*state_.crownBoundaryPointsPx)[0].x;
            p2 = (*state_.crownBoundaryPointsPx)[1].x;
        }
        r.diagnostics["crown_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["crown_edge_model"] = "maximum_positive_x_gradient_quadratic";
        const auto &centers = *state_.neckCentersPx;
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findCrownMeniscus(a, config_.measurement.reflectorRoiCamera1, centers[0], config_.crown, p1);
        auto second = algorithms::findCrownMeniscus(b, config_.measurement.reflectorRoiCamera2, centers[1], config_.crown, p2);
        if (!first || !second)
            return {false, stereoDetectionFailure("Crown", first, second)};
        const auto addDiagnostics = [&r, this](const algorithms::CurveHit &hit, int camera)
        {
            const std::string suffix = "_camera" + std::to_string(camera);
            r.diagnostics["crown_left_margin" + suffix + "_px"] = config_.crown.leftMarginPx;
            r.diagnostics["crown_tracking_half_width" + suffix + "_px"] = config_.crown.searchHalfWidthPx;
            r.diagnostics["crown_row_strengths_mean" + suffix] = hit.rowStrengthsMean;
            r.diagnostics["crown_row_strengths_maximum" + suffix] = hit.rowStrengthsMaximum;
            r.diagnostics["crown_minimum_strength" + suffix] = hit.minimumStrength;
            r.diagnostics["crown_kept_row_count" + suffix] = hit.keptRowCount;
            r.diagnostics["crown_seed_x" + suffix + "_px"] = hit.seedX;
            r.diagnostics["crown_edge_point_count" + suffix] = hit.edgePointCount;
            r.diagnostics["crown_residual_limit" + suffix + "_px"] = hit.residualLimitPx;
            r.diagnostics["crown_robust_inlier_count" + suffix] = hit.robustInlierCount;
            r.diagnostics["crown_sagitta" + suffix + "_px"] = hit.sagittaPx;
            r.diagnostics["crown_center" + suffix + "_px"] = pointValue(hit.center);
            r.diagnostics["crown_boundary" + suffix + "_px"] = pointValue(hit.boundary);
            r.diagnostics["crown_edge_seed_x" + suffix + "_px"] = hit.seedX;
            r.diagnostics["crown_edge_coverage" + suffix] = hit.coverage;
            r.diagnostics["crown_edge_fit_error" + suffix + "_px"] = hit.fitErrorPx;
            r.diagnostics["crown_fit_strengths_mean" + suffix] = hit.fitStrengthMean;
        };
        addDiagnostics(*first, 1);
        addDiagnostics(*second, 2);
        state_.crownBoundaryPointsPx = std::array<cv::Point2d, 2>{first->boundary, second->boundary};
        r.data.cameras[0].boundaryX = first->boundary.x;
        r.data.cameras[1].boundaryX = second->boundary.x;
        addCurveOverlay(*first, r.overlay1);
        addCurveOverlay(*second, r.overlay2);
        return {true, "Crown meniscus lower vertices updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processBody(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        if (!state_.validNeck || !state_.neckCentersPx)
            return {false, "Body mode requires a valid Neck result"};
        std::optional<double> p1, p2;
        if (config_.body.usePreviousBoundaryX && state_.bodyBoundaryPointsPx)
        {
            p1 = (*state_.bodyBoundaryPointsPx)[0].x;
            p2 = (*state_.bodyBoundaryPointsPx)[1].x;
        }
        r.diagnostics["body_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["body_edge_model"] = "maximum_brightness_quadratic";
        const auto &centers = *state_.neckCentersPx;
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findBodyMeniscus(a, config_.measurement.reflectorRoiCamera1, centers[0], config_.body, config_.body.brightnessThresholdPercentCamera1, p1);
        auto second = algorithms::findBodyMeniscus(b, config_.measurement.reflectorRoiCamera2, centers[1], config_.body, config_.body.brightnessThresholdPercentCamera2, p2);
        if (!first || !second)
            return {false, stereoDetectionFailure("Body", first, second)};
        const auto addDiagnostics = [&r](const algorithms::CurveHit &hit, int camera)
        {
            const std::string suffix = "_camera" + std::to_string(camera);
            r.diagnostics["body_search_start_x" + suffix + "_px"] = hit.searchStartX;
            r.diagnostics["body_search_stop_x" + suffix + "_px"] = hit.searchStopX;
            r.diagnostics["body_left_margin" + suffix + "_px"] = hit.leftMarginPx;
            r.diagnostics["body_tracking_half_width" + suffix + "_px"] = hit.trackingHalfWidthPx;
            r.diagnostics["body_brightness_threshold_percent" + suffix] = hit.brightnessThresholdPercent;
            r.diagnostics["body_threshold_crossing_count" + suffix] = hit.thresholdCrossingCount;
            r.diagnostics["body_row_maximum_p90" + suffix] = hit.rowMaximumP90;
            r.diagnostics["body_row_maximum_maximum" + suffix] = hit.rowMaximumMaximum;
            r.diagnostics["body_residual_limit" + suffix + "_px"] = hit.residualLimitPx;
            r.diagnostics["body_robust_inlier_count" + suffix] = hit.robustInlierCount;
            r.diagnostics["body_coverage_ratio" + suffix] = hit.coverage;
            r.diagnostics["body_sagitta" + suffix + "_px"] = hit.sagittaPx;
            r.diagnostics["body_center" + suffix + "_px"] = pointValue(hit.center);
            r.diagnostics["body_boundary" + suffix + "_px"] = pointValue(hit.boundary);
            r.diagnostics["body_edge_seed_x" + suffix + "_px"] = hit.seedX;
            r.diagnostics["body_edge_coverage" + suffix] = hit.coverage;
            r.diagnostics["body_edge_fit_error" + suffix + "_px"] = hit.fitErrorPx;
            r.diagnostics["body_edge_point_count" + suffix] = hit.edgePointCount;
            r.diagnostics["body_fit_strength_mean" + suffix] = hit.fitStrengthMean;
        };
        addDiagnostics(*first, 1);
        addDiagnostics(*second, 2);
        state_.bodyBoundaryPointsPx = std::array<cv::Point2d, 2>{first->boundary, second->boundary};
        r.data.cameras[0].boundaryX = first->boundary.x;
        r.data.cameras[1].boundaryX = second->boundary.x;
        addCurveOverlay(*first, r.overlay1);
        addCurveOverlay(*second, r.overlay2);
        return {true, "Body meniscus lower vertices updated"};
    }


}
