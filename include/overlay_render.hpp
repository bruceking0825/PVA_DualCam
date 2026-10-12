#pragma once
#include "config.hpp"
#include "measurement_geometry.hpp"
#include <QImage>
#include <QPainterPath>
#include <QPen>

namespace pva {
QPen overlayPen(const OverlayElement &element);
QPainterPath overlayPath(const OverlayElement &element);
// Same display-only ROI composition for the page and measurement export.
void appendDisplayRois(const MeasurementConfig &config, const MeasurementRois &rois,
                       MeasurementStage stage, cv::Size size1, cv::Size size2,
                       std::vector<OverlayElement> &overlay1, std::vector<OverlayElement> &overlay2);
QImage renderOverlayImage(const cv::Mat &image, const std::vector<OverlayElement> &overlay);
}
