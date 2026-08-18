#include "pva/measurement_engine.hpp"
#include "pva/algorithms/detectors.hpp"
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
        // 与 Python 版本一致：边界轮廓、轴对齐拟合椭圆、中心和下顶点分层显示。
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
                       {cv::Point2d(hit.ellipse.center.x, hit.ellipse.center.y + radiusY)},
                       {0, 0, 255}, 2, false});
    }
    void addReflectorOverlay(const pva::ReflectorRoi &roi, std::vector<pva::OverlayElement> &out, bool showCenter)
    {
        const double markerHeight = std::max(30.0, std::abs(roi.rightBoundary.x - roi.leftBoundary.x) * 0.12);
        out.push_back({pva::OverlayType::Line,
                       {{roi.leftBoundary.x, std::max(0.0, roi.leftBoundary.y - markerHeight)}, roi.leftBoundary},
                       {255, 0, 0}, 3, false});
        out.push_back({pva::OverlayType::Line,
                       {{roi.rightBoundary.x, std::max(0.0, roi.rightBoundary.y - markerHeight)}, roi.rightBoundary},
                       {255, 0, 0}, 3, false});
        out.push_back({pva::OverlayType::Polyline, roi.bottomCurve, {255, 0, 0}, 3, false});
        if (showCenter)
            out.push_back({pva::OverlayType::Cross, {roi.center}, {0, 255, 255}, 2, false});
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

    void initializeCrownDiagnostics(pva::MeasurementResult &result)
    {
        static const std::array<const char *, 17> cameraKeys{
            "crown_bottom_margin_camera%1_px", "crown_tracking_half_height_camera%1_px",
            "crown_column_strengths_mean_camera%1", "crown_column_strengths_maximum_camera%1",
            "crown_minimum_strength_camera%1", "crown_kept_column_count_camera%1",
            "crown_seed_y_camera%1_px", "crown_edge_point_count_camera%1",
            "crown_residual_limit_camera%1_px", "crown_robust_inlier_count_camera%1",
            "crown_sagitta_camera%1_px", "crown_center_camera%1_px",
            "crown_boundary_camera%1_px", "crown_edge_seed_y_camera%1_px",
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
        static const std::array<const char *, 17> cameraKeys{
            "body_search_start_y_camera%1_px", "body_search_stop_y_camera%1_px",
            "body_bottom_margin_camera%1_px", "body_tracking_half_height_camera%1_px",
            "body_brightness_offset_camera%1", "body_threshold_crossing_count_camera%1",
            "body_column_maximum_p90_camera%1", "body_column_maximum_maximum_camera%1",
            "body_residual_limit_camera%1_px", "body_robust_inlier_count_camera%1",
            "body_coverage_ratio_camera%1", "body_sagitta_camera%1_px",
            "body_center_camera%1_px", "body_boundary_camera%1_px",
            "body_edge_seed_y_camera%1_px", "body_edge_coverage_camera%1",
            "body_fit_strength_mean_camera%1"};
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
}

namespace pva
{
    MeasurementEngine::MeasurementEngine(MeasurementConfig config, MeasurementState state) : config_(std::move(config)), state_(std::move(state)) {}

    MeasurementResult MeasurementEngine::process(const cv::Mat &a, const cv::Mat &b, MeasurementStage stage)
    {
        const auto started = std::chrono::steady_clock::now();
        MeasurementResult result;
        result.stage = stage;
        const auto effective = stage == MeasurementStage::Idle ? MeasurementStage::Neck : stage;
        if (effective == MeasurementStage::Crown)
            initializeCrownDiagnostics(result);
        else if (effective == MeasurementStage::Body)
            initializeBodyDiagnostics(result);
        result.preview1 = algorithms::normalizeGray8(a);
        result.preview2 = algorithms::normalizeGray8(b);
        if (result.preview1.empty() || result.preview2.empty())
        {
            result.message = "Camera image is empty";
            result.diagnostics["cycle_ms"] = 0.0;
            return result;
        }
        double light1 = 0, light2 = 0;
        cv::minMaxLoc(result.preview1, nullptr, &light1);
        cv::minMaxLoc(result.preview2, nullptr, &light2);
        state_.filteredLight[0] = ema(state_.filteredLight[0], light1, config_.measurement.lightAlpha);
        state_.filteredLight[1] = ema(state_.filteredLight[1], light2, config_.measurement.lightAlpha);
        result.diagnostics["light_camera1"] = state_.filteredLight[0];
        result.diagnostics["light_camera2"] = state_.filteredLight[1];
        if (state_.filteredLight[0] < config_.measurement.brightnessMin || state_.filteredLight[0] > config_.measurement.brightnessMax || state_.filteredLight[1] < config_.measurement.brightnessMin || state_.filteredLight[1] > config_.measurement.brightnessMax)
        {
            result.message = "Brightness is outside configured limits";
            result.values = state_.values;
            result.diagnostics["cycle_ms"] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            return result;
        }
        std::pair<bool, std::string> outcome;
        if (effective == MeasurementStage::Neck)
            outcome = processNeck(result.preview1, result.preview2, result);
        else if (effective == MeasurementStage::Crown)
            outcome = processCrown(result.preview1, result.preview2, result);
        else if (effective == MeasurementStage::Body)
            outcome = processBody(result.preview1, result.preview2, result);
        else if (effective == MeasurementStage::Endcone)
            outcome = processEndcone(result.preview1, result.preview2, result);
        else
            outcome = {false, "Unsupported stage"};
        result.valid = outcome.first;
        result.message = outcome.second;
        result.values = state_.values;
        result.diagnostics["cycle_ms"] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }

    std::pair<bool, std::string> MeasurementEngine::processNeck(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        const auto reflector1 = algorithms::findReflectorBottom(a, config_.neck.reflectorThresholdCamera1, config_.neck);
        const auto reflector2 = algorithms::findReflectorBottom(b, config_.neck.reflectorThresholdCamera2, config_.neck);
        if (!reflector1 || !reflector2)
        {
            // 与 Python 版本一致：单帧检测失败只报告本帧无效，不能销毁最后一次
            // 有效的 Neck/reflector 状态，否则 Crown/Body 会因瞬时丢边而失去 ROI。
            return {false, "Reflector bottom not found for Crown/Body ROI"};
        }
        addReflectorOverlay(reflector1->roi, r.overlay1, true);
        addReflectorOverlay(reflector2->roi, r.overlay2, true);
        r.diagnostics["reflector_left_x_camera1_px"] = reflector1->roi.leftBoundary.x;
        r.diagnostics["reflector_right_x_camera1_px"] = reflector1->roi.rightBoundary.x;
        r.diagnostics["reflector_center_x_camera1_px"] = reflector1->roi.center.x;
        r.diagnostics["reflector_center_y_camera1_px"] = reflector1->roi.center.y;
        r.diagnostics["reflector_left_x_camera2_px"] = reflector2->roi.leftBoundary.x;
        r.diagnostics["reflector_right_x_camera2_px"] = reflector2->roi.rightBoundary.x;
        r.diagnostics["reflector_center_x_camera2_px"] = reflector2->roi.center.x;
        r.diagnostics["reflector_center_y_camera2_px"] = reflector2->roi.center.y;
        auto first = algorithms::findNeckEllipse(a, config_.neck.gradientThresholdCamera1, config_.neck.minContourAreaPx, config_.neck.startSearchRatio, config_.neck.stopSearchRatio, reflector1->roi.center.x);
        auto second = algorithms::findNeckEllipse(b, config_.neck.gradientThresholdCamera2, config_.neck.minContourAreaPx, config_.neck.startSearchRatio, config_.neck.stopSearchRatio, reflector2->roi.center.x);
        if (!first || !second)
            return {false, "Neck meniscus not found"};
        const double majorAxis1 = std::max(first->ellipse.size.width, first->ellipse.size.height);
        const double majorAxis2 = std::max(second->ellipse.size.width, second->ellipse.size.height);
        r.diagnostics["neck_contour_area_camera1_px"] = first->area;
        r.diagnostics["neck_contour_area_camera2_px"] = second->area;
        r.diagnostics["neck_edge_points_camera1"] = static_cast<double>(first->contour.size());
        r.diagnostics["neck_edge_points_camera2"] = static_cast<double>(second->contour.size());
        r.diagnostics["neck_center_x_camera1_px"] = first->ellipse.center.x;
        r.diagnostics["neck_center_y_camera1_px"] = first->ellipse.center.y;
        r.diagnostics["neck_center_x_camera2_px"] = second->ellipse.center.x;
        r.diagnostics["neck_center_y_camera2_px"] = second->ellipse.center.y;
        r.diagnostics["neck_ellipse_vertex_y_camera1_px"] = first->ellipse.center.y + first->ellipse.size.height * 0.5;
        r.diagnostics["neck_ellipse_vertex_y_camera2_px"] = second->ellipse.center.y + second->ellipse.size.height * 0.5;
        r.diagnostics["neck_major_axis_camera1_px"] = majorAxis1;
        r.diagnostics["neck_major_axis_camera2_px"] = majorAxis2;
        r.diagnostics["neck_pixels_per_mm"] = config_.neck.pixelsPerMm;
        if (std::min(first->contour.size(), second->contour.size()) < size_t(config_.neck.minEdgePoints))
            return {false, "Not enough neck edge points"};
        double raw = majorAxis2 / config_.neck.pixelsPerMm;
        if (!(raw > config_.measurement.diameterMinMm && raw < config_.measurement.diameterMaxMm))
            return {false, "Neck diameter is outside physical limits"};
        state_.values.diameterMm = ema(state_.values.diameterMm, raw, config_.neck.diameterAlpha);
        state_.mmPerPixel = ema(state_.mmPerPixel, 1 / config_.neck.pixelsPerMm, config_.measurement.mmPerPixelAlpha);
        state_.neckCentersPx = std::array<cv::Point2d, 2>{first->ellipse.center, second->ellipse.center};
        state_.neckReflectorRois = std::array<ReflectorRoi, 2>{reflector1->roi, reflector2->roi};
        state_.neckXSpans = std::array<cv::Vec2i, 2>{
            cv::Vec2i(cvRound(reflector1->roi.leftBoundary.x), cvRound(reflector1->roi.rightBoundary.x)),
            cv::Vec2i(cvRound(reflector2->roi.leftBoundary.x), cvRound(reflector2->roi.rightBoundary.x))};
        state_.validNeck = true;
        state_.crownBoundaryPointsPx.reset();
        state_.bodyCentersPx.reset();
        state_.bodyBoundaryPointsPx.reset();
        addNeckOverlay(*first, r.overlay1);
        addNeckOverlay(*second, r.overlay2);
        r.diagnostics["raw_diameter_mm"] = raw;
        return {true, "Neck measurement updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processCrown(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        if (!state_.validNeck || !state_.neckReflectorRois || !state_.neckCentersPx)
            return {false, "Crown mode requires reflector ROI from a valid Idle/Neck result"};
        std::optional<double> p1, p2;
        if (config_.crown.usePreviousBoundaryY && state_.crownBoundaryPointsPx)
        {
            p1 = (*state_.crownBoundaryPointsPx)[0].y;
            p2 = (*state_.crownBoundaryPointsPx)[1].y;
        }
        r.diagnostics["crown_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["crown_edge_model"] = "maximum_negative_gradient_quadratic";
        const auto &rois = *state_.neckReflectorRois;
        const auto &centers = *state_.neckCentersPx;
        addReflectorOverlay(rois[0], r.overlay1, false);
        addReflectorOverlay(rois[1], r.overlay2, false);
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findCrownMeniscus(a, rois[0], centers[0], config_.crown, p1);
        auto second = algorithms::findCrownMeniscus(b, rois[1], centers[1], config_.crown, p2);
        if (!first || !second)
            return {false, "Crown meniscus curve not found in reflector ROI"};
        const auto addDiagnostics = [&r, this](const algorithms::CurveHit &hit, int camera)
        {
            const std::string suffix = "_camera" + std::to_string(camera);
            r.diagnostics["crown_bottom_margin" + suffix + "_px"] = config_.crown.bottomMarginPx;
            r.diagnostics["crown_tracking_half_height" + suffix + "_px"] = config_.crown.searchHalfHeightPx;
            r.diagnostics["crown_column_strengths_mean" + suffix] = hit.columnStrengthsMean;
            r.diagnostics["crown_column_strengths_maximum" + suffix] = hit.columnStrengthsMaximum;
            r.diagnostics["crown_minimum_strength" + suffix] = hit.minimumStrength;
            r.diagnostics["crown_kept_column_count" + suffix] = hit.keptColumnCount;
            r.diagnostics["crown_seed_y" + suffix + "_px"] = hit.seedY;
            r.diagnostics["crown_edge_point_count" + suffix] = hit.edgePointCount;
            r.diagnostics["crown_residual_limit" + suffix + "_px"] = hit.residualLimitPx;
            r.diagnostics["crown_robust_inlier_count" + suffix] = hit.robustInlierCount;
            r.diagnostics["crown_sagitta" + suffix + "_px"] = hit.sagittaPx;
            r.diagnostics["crown_center" + suffix + "_px"] = pointValue(hit.center);
            r.diagnostics["crown_boundary" + suffix + "_px"] = pointValue(hit.boundary);
            r.diagnostics["crown_edge_seed_y" + suffix + "_px"] = hit.seedY;
            r.diagnostics["crown_edge_coverage" + suffix] = hit.coverage;
            r.diagnostics["crown_edge_fit_error" + suffix + "_px"] = hit.fitErrorPx;
            r.diagnostics["crown_fit_strengths_mean" + suffix] = hit.fitStrengthMean;
        };
        addDiagnostics(*first, 1);
        addDiagnostics(*second, 2);
        state_.crownBoundaryPointsPx = std::array<cv::Point2d, 2>{first->boundary, second->boundary};
        addCurveOverlay(*first, r.overlay1);
        addCurveOverlay(*second, r.overlay2);
        return {true, "Crown meniscus lower vertices updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processBody(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        if (!state_.validNeck || !state_.neckReflectorRois || !state_.neckCentersPx)
            return {false, "Body mode requires reflector ROI from a valid Idle/Neck result"};
        std::optional<double> p1, p2;
        if (config_.body.usePreviousBoundaryY && state_.bodyBoundaryPointsPx)
        {
            p1 = (*state_.bodyBoundaryPointsPx)[0].y;
            p2 = (*state_.bodyBoundaryPointsPx)[1].y;
        }
        r.diagnostics["body_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["body_edge_model"] = "maximum_brightness_quadratic";
        const auto &rois = *state_.neckReflectorRois;
        const auto &centers = *state_.neckCentersPx;
        addReflectorOverlay(rois[0], r.overlay1, false);
        addReflectorOverlay(rois[1], r.overlay2, false);
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findBodyMeniscus(a, rois[0], centers[0], config_.body, config_.body.brightnessOffsetCamera1, p1);
        auto second = algorithms::findBodyMeniscus(b, rois[1], centers[1], config_.body, config_.body.brightnessOffsetCamera2, p2);
        if (!first || !second)
            return {false, "Body meniscus curve not found in reflector ROI"};
        const auto addDiagnostics = [&r](const algorithms::CurveHit &hit, int camera)
        {
            const std::string suffix = "_camera" + std::to_string(camera);
            r.diagnostics["body_search_start_y" + suffix + "_px"] = hit.searchStartY;
            r.diagnostics["body_search_stop_y" + suffix + "_px"] = hit.searchStopY;
            r.diagnostics["body_bottom_margin" + suffix + "_px"] = hit.bottomMarginPx;
            r.diagnostics["body_tracking_half_height" + suffix + "_px"] = hit.trackingHalfHeightPx;
            r.diagnostics["body_brightness_offset" + suffix] = hit.brightnessOffset;
            r.diagnostics["body_threshold_crossing_count" + suffix] = hit.thresholdCrossingCount;
            r.diagnostics["body_column_maximum_p90" + suffix] = hit.columnMaximumP90;
            r.diagnostics["body_column_maximum_maximum" + suffix] = hit.columnMaximumMaximum;
            r.diagnostics["body_residual_limit" + suffix + "_px"] = hit.residualLimitPx;
            r.diagnostics["body_robust_inlier_count" + suffix] = hit.robustInlierCount;
            r.diagnostics["body_coverage_ratio" + suffix] = hit.coverage;
            r.diagnostics["body_sagitta" + suffix + "_px"] = hit.sagittaPx;
            r.diagnostics["body_center" + suffix + "_px"] = pointValue(hit.center);
            r.diagnostics["body_boundary" + suffix + "_px"] = pointValue(hit.boundary);
            r.diagnostics["body_edge_seed_y" + suffix + "_px"] = hit.seedY;
            r.diagnostics["body_edge_coverage" + suffix] = hit.coverage;
            r.diagnostics["body_fit_strength_mean" + suffix] = hit.fitStrengthMean;
        };
        addDiagnostics(*first, 1);
        addDiagnostics(*second, 2);
        state_.bodyCentersPx = state_.neckCentersPx.value_or(
            std::array<cv::Point2d, 2>{rois[0].center, rois[1].center});
        state_.bodyBoundaryPointsPx = std::array<cv::Point2d, 2>{first->boundary, second->boundary};
        addCurveOverlay(*first, r.overlay1);
        addCurveOverlay(*second, r.overlay2);
        return {true, "Body meniscus lower vertices updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processEndcone(const cv::Mat &, const cv::Mat &b, MeasurementResult &r)
    {
        if (!state_.validNeck || !state_.bodyCentersPx || !state_.mmPerPixel)
            return {false, "Endcone requires valid neck and body state"};
        cv::Vec2i span = state_.neckXSpans ? (*state_.neckXSpans)[1] : cv::Vec2i(0, b.cols - 1);
        auto hit = algorithms::findEndcone(b, (*state_.bodyCentersPx)[1], span, *state_.mmPerPixel, config_.endcone);
        if (!hit)
            return {false, "Endcone search area is invalid"};
        if (!(hit->diameterMm > config_.measurement.diameterMinMm && hit->diameterMm < config_.measurement.diameterMaxMm))
            return {false, "Endcone diameter is outside physical limits"};
        state_.values.diameterMm = ema(state_.values.diameterMm, hit->diameterMm, config_.endcone.diameterAlpha);
        r.overlay2.push_back({OverlayType::Line, {{double(hit->x0), hit->boundaryY}, {double(hit->x1 - 1), hit->boundaryY}}, {0, 0, 255}, 4, false});
        r.overlay2.push_back({OverlayType::Cross, {(*state_.bodyCentersPx)[1]}, {255, 0, 255}, 2, false});
        r.diagnostics["boundary_y_px"] = hit->boundaryY;
        r.diagnostics["raw_diameter_mm"] = hit->diameterMm;
        return {true, "Endcone measurement updated"};
    }
}
