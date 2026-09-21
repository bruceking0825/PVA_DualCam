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
                       {0, 0, 255},
                       2,
                       false});
    }
    cv::Vec2i ellipseXSpan(const pva::algorithms::EllipseHit &hit, int imageWidth)
    {
        const double halfWidth = std::max(hit.ellipse.size.width, hit.ellipse.size.height) * 0.5;
        const int left = std::clamp(cvRound(hit.ellipse.center.x - halfWidth), 0, imageWidth - 1);
        const int right = std::clamp(cvRound(hit.ellipse.center.x + halfWidth), left, imageWidth - 1);
        return {left, right};
    }

    cv::Rect clippedRoi(const cv::Rect &configuredRoi, const cv::Size &imageSize)
    {
        return configuredRoi & cv::Rect(0, 0, imageSize.width, imageSize.height);
    }

    cv::Point2d roiTopCenter(const cv::Rect &configuredRoi, const cv::Size &imageSize)
    {
        const cv::Rect roi = clippedRoi(configuredRoi, imageSize);
        return {roi.x + (roi.width - 1) * 0.5, double(roi.y)};
    }

    cv::Vec2i roiXSpan(const cv::Rect &configuredRoi, const cv::Size &imageSize)
    {
        const cv::Rect roi = clippedRoi(configuredRoi, imageSize);
        return {roi.x, std::max(roi.x, roi.x + roi.width - 1)};
    }

    std::pair<bool, std::string> applyCamera1Neck(
        const pva::algorithms::EllipseHit &hit,
        const cv::Size &camera1Size,
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
        result.diagnostics["neck_edge_points_camera1"] = static_cast<double>(hit.contour.size());
        result.diagnostics["neck_center_x_camera1_px"] = hit.ellipse.center.x;
        result.diagnostics["neck_center_y_camera1_px"] = hit.ellipse.center.y;
        result.diagnostics["neck_ellipse_vertex_y_camera1_px"] =
            hit.ellipse.center.y + hit.ellipse.size.height * 0.5;
        result.diagnostics["neck_major_axis_camera1_px"] = majorAxis;
        result.diagnostics["neck_pixels_per_mm"] = config.neck.pixelsPerMm;
        result.diagnostics["raw_diameter_mm"] = rawDiameter;

        state.values.diameterMm = ema(state.values.diameterMm, rawDiameter, config.neck.diameterAlpha);
        state.mmPerPixel = ema(state.mmPerPixel, 1.0 / config.neck.pixelsPerMm,
                               config.measurement.mmPerPixelAlpha);
        state.neckCentersPx = std::array<cv::Point2d, 2>{
            hit.ellipse.center,
            roiTopCenter(config.measurement.reflectorRoiCamera2, camera2Size)};
        state.neckXSpans = std::array<cv::Vec2i, 2>{
            ellipseXSpan(hit, camera1Size.width),
            roiXSpan(config.measurement.reflectorRoiCamera2, camera2Size)};
        state.validNeck = true;
        if (resetFollowingStages)
        {
            state.crownBoundaryPointsPx.reset();
            state.bodyCentersPx.reset();
            state.bodyBoundaryPointsPx.reset();
        }
        addNeckOverlay(hit, result.overlay1);
        return {true, {}};
    }

    std::pair<bool, std::string> applyCamera2NeckReference(
        const pva::algorithms::EllipseHit &hit,
        const cv::Size &camera2Size,
        const pva::MeasurementConfig &config,
        pva::MeasurementState &state,
        pva::MeasurementResult &result)
    {
        if (hit.contour.size() < size_t(config.neck.minEdgePoints))
            return {false, "Not enough Camera 2 neck edge points"};
        if (!state.neckCentersPx || !state.neckXSpans)
            return {false, "Camera 2 neck reference requires a valid Camera 1 result"};

        const double majorAxis = std::max(hit.ellipse.size.width, hit.ellipse.size.height);
        result.diagnostics["neck_contour_area_camera2_px"] = hit.area;
        result.diagnostics["neck_edge_points_camera2"] = static_cast<double>(hit.contour.size());
        result.diagnostics["neck_center_x_camera2_px"] = hit.ellipse.center.x;
        result.diagnostics["neck_center_y_camera2_px"] = hit.ellipse.center.y;
        result.diagnostics["neck_ellipse_vertex_y_camera2_px"] =
            hit.ellipse.center.y + hit.ellipse.size.height * 0.5;
        result.diagnostics["neck_major_axis_camera2_px"] = majorAxis;

        // Camera 2 只提供过渡阶段的空间参考；直径和毫米比例始终由 Camera 1 更新。
        (*state.neckCentersPx)[1] = hit.ellipse.center;
        (*state.neckXSpans)[1] = ellipseXSpan(hit, camera2Size.width);
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
                       {0, 255, 0}, 2, true});
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
        addRoiOverlay(config_.measurement.reflectorRoiCamera1, result.preview1.size(), result.overlay1);
        addRoiOverlay(config_.measurement.reflectorRoiCamera2, result.preview2.size(), result.overlay2);
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
                                                 std::chrono::steady_clock::now() - started)
                                                 .count();
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
                                             std::chrono::steady_clock::now() - started)
                                             .count();
        return result;
    }

    std::pair<bool, std::string> MeasurementEngine::processNeck(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        auto first = algorithms::findNeckEllipse(a, config_.measurement.reflectorRoiCamera1, config_.neck.gradientThresholdCamera1, config_.neck.minContourAreaPx, config_.neck.startSearchRatio, config_.neck.stopSearchRatio, {});
        if (!first)
            return {false, detectionFailure("Camera 1 neck meniscus detection failed", first.error)};
        const auto updated = applyCamera1Neck(*first, a.size(), b.size(), config_, state_, r, true);
        if (!updated.first)
            return updated;
        return {true, "Neck measurement updated"};
    }

    std::pair<bool, std::string> MeasurementEngine::processCrown(const cv::Mat &a, const cv::Mat &b, MeasurementResult &r)
    {
        if (!(config_.crown.diameterThreshold2Mm > config_.crown.diameterThreshold1Mm))
            return {false, "Crown diameter threshold 2 must be greater than threshold 1"};

        r.diagnostics["crown_diameter_threshold1_mm"] = config_.crown.diameterThreshold1Mm;
        r.diagnostics["crown_diameter_threshold2_mm"] = config_.crown.diameterThreshold2Mm;
        const bool neckTrackingActive = !state_.values.diameterMm ||
                                        *state_.values.diameterMm <= config_.crown.diameterThreshold2Mm;
        bool neckTrackingValid = false;
        std::string neckTrackingError;
        if (neckTrackingActive)
        {
            const auto neck = algorithms::findNeckEllipse(
                a, config_.measurement.reflectorRoiCamera1,
                config_.neck.gradientThresholdCamera1, config_.neck.minContourAreaPx,
                config_.neck.startSearchRatio, config_.neck.stopSearchRatio, {});
            if (neck)
            {
                const auto updated = applyCamera1Neck(*neck, a.size(), b.size(), config_, state_, r, false);
                neckTrackingValid = updated.first;
                neckTrackingError = updated.second;
            }
            else
            {
                neckTrackingError = detectionFailure(
                    "Camera 1 neck meniscus detection failed during Crown transition",
                    neck.error);
            }
        }

        bool camera2NeckTrackingActive = false;
        bool camera2NeckTrackingValid = false;
        std::string camera2NeckTrackingError;
        if (neckTrackingValid && state_.values.diameterMm)
        {
            camera2NeckTrackingActive =
                *state_.values.diameterMm > config_.crown.diameterThreshold1Mm &&
                *state_.values.diameterMm <= config_.crown.diameterThreshold2Mm;
            if (camera2NeckTrackingActive)
            {
                const auto neck = algorithms::findNeckEllipse(
                    b, config_.measurement.reflectorRoiCamera2,
                    config_.neck.gradientThresholdCamera2, config_.neck.minContourAreaPx,
                    config_.neck.startSearchRatio, config_.neck.stopSearchRatio, {});
                if (neck)
                {
                    const auto updated = applyCamera2NeckReference(
                        *neck, b.size(), config_, state_, r);
                    camera2NeckTrackingValid = updated.first;
                    camera2NeckTrackingError = updated.second;
                }
                else
                {
                    camera2NeckTrackingError = detectionFailure(
                        "Camera 2 neck meniscus detection failed during Crown transition",
                        neck.error);
                }
            }
        }
        r.diagnostics["crown_neck_tracking_active"] = neckTrackingActive;
        r.diagnostics["crown_neck_tracking_valid"] = neckTrackingValid;
        r.diagnostics["crown_camera2_neck_tracking_active"] = camera2NeckTrackingActive;
        r.diagnostics["crown_camera2_neck_tracking_valid"] = camera2NeckTrackingValid;
        r.diagnostics["neck_diameter_source_camera"] = 1;

        if (!state_.values.diameterMm)
            return {false, neckTrackingError.empty() ? "Crown transition requires a Camera 1 neck diameter" : neckTrackingError};
        if (*state_.values.diameterMm <= config_.crown.diameterThreshold1Mm)
        {
            if (!neckTrackingValid)
                return {false, neckTrackingError};
            return {true, "Crown diameter updated from Camera 1 neck ellipse"};
        }
        if (camera2NeckTrackingActive && !camera2NeckTrackingValid)
            return {false, camera2NeckTrackingError};
        if (!state_.validNeck || !state_.neckCentersPx)
            return {false, "Crown meniscus requires a valid Camera 1 neck reference"};

        std::optional<double> p1, p2;
        if (config_.crown.usePreviousBoundaryY && state_.crownBoundaryPointsPx)
        {
            p1 = (*state_.crownBoundaryPointsPx)[0].y;
            p2 = (*state_.crownBoundaryPointsPx)[1].y;
        }
        r.diagnostics["crown_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["crown_edge_model"] = "maximum_negative_gradient_quadratic";
        const auto &centers = *state_.neckCentersPx;
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findCrownMeniscus(a, config_.measurement.reflectorRoiCamera1, centers[0], config_.crown, p1);
        auto second = algorithms::findCrownMeniscus(b, config_.measurement.reflectorRoiCamera2, centers[1], config_.crown, p2);
        if (!first || !second)
            return {false, stereoDetectionFailure("Crown", first, second)};
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
        if (!state_.validNeck || !state_.neckCentersPx)
            return {false, "Body mode requires a valid Idle/Neck result"};
        std::optional<double> p1, p2;
        if (config_.body.usePreviousBoundaryY && state_.bodyBoundaryPointsPx)
        {
            p1 = (*state_.bodyBoundaryPointsPx)[0].y;
            p2 = (*state_.bodyBoundaryPointsPx)[1].y;
        }
        r.diagnostics["body_edge_previous_tracking_active"] = p1.has_value() && p2.has_value();
        r.diagnostics["body_edge_model"] = "maximum_brightness_quadratic";
        const auto &centers = *state_.neckCentersPx;
        addStoredNeckCenterOverlays(state_, r);
        auto first = algorithms::findBodyMeniscus(a, config_.measurement.reflectorRoiCamera1, centers[0], config_.body, config_.body.brightnessOffsetCamera1, p1);
        auto second = algorithms::findBodyMeniscus(b, config_.measurement.reflectorRoiCamera2, centers[1], config_.body, config_.body.brightnessOffsetCamera2, p2);
        if (!first || !second)
            return {false, stereoDetectionFailure("Body", first, second)};
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
        state_.bodyCentersPx = state_.neckCentersPx;
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
            return {false, detectionFailure("Camera 2 endcone detection failed", hit.error)};
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
