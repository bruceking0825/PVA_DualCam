#include "overlay_render.hpp"
#include "plc_runtime.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <opencv2/imgproc.hpp>

namespace pva
{
    QPen overlayPen(const OverlayElement &e)
    {
        QPen pen(QColor(cvRound(e.colorBgr[2]), cvRound(e.colorBgr[1]), cvRound(e.colorBgr[0])));
        pen.setStyle(e.dashed ? Qt::DashLine : Qt::SolidLine);
        pen.setWidthF(e.width);
        pen.setCosmetic(true);
        return pen;
    }
    QPainterPath overlayPath(const OverlayElement &e)
    {
        QPainterPath path;
        if (e.points.empty())
            return path;
        if (e.type == OverlayType::Cross)
        {
            path.moveTo(-3, 0);
            path.lineTo(3, 0);
            path.moveTo(0, -3);
            path.lineTo(0, 3);
        }
        else
        {
            path.moveTo(e.points[0].x + .5, e.points[0].y + .5);
            for (size_t i = 1; i < e.points.size(); ++i)
                path.lineTo(e.points[i].x + .5, e.points[i].y + .5);
            if (e.closed)
                path.closeSubpath();
        }
        return path;
    }
    void appendDisplayRois(const MeasurementConfig &config, const MeasurementRois &rois,
                           MeasurementStage stage, cv::Size size1, cv::Size size2,
                           std::vector<OverlayElement> &overlay1, std::vector<OverlayElement> &overlay2)
    {
        if (stage == MeasurementStage::Melt || stage == MeasurementStage::Dip || stage == MeasurementStage::Neck)
        {
            for (int camera = 1; camera <= 2; ++camera)
            {
                bool fromPlc = false;
                const auto roi = effectiveAutoExposureRoi(rois, camera,
                                                          camera == 1 ? config.measurement.autoExposureRoiCamera1 : config.measurement.autoExposureRoiCamera2,
                                                          camera == 1 ? size1 : size2, &fromPlc);
                if (roi.empty())
                    continue;
                auto &overlay = camera == 1 ? overlay1 : overlay2;
                overlay.push_back({OverlayType::Polyline,
                                   {{double(roi.x), double(roi.y)}, {double(roi.x + roi.width), double(roi.y)}, {double(roi.x + roi.width), double(roi.y + roi.height)}, {double(roi.x), double(roi.y + roi.height)}},
                                   fromPlc ? cv::Scalar(0, 255, 0) : cv::Scalar(255, 0, 0),
                                   2,
                                   true});
            }
        }
        if (stage != MeasurementStage::Melt)
            appendPlcRoiOverlays(rois, stage, config.measurement.diaRectHeightPx, overlay1, overlay2);
    }
    QString regionStatisticsText(const std::optional<GrayStats> &statistics)
    {
        if (!statistics) return "average: --\nmax: --\nmin: --";
        return QString("average: %1\nmax: %2\nmin: %3")
            .arg(statistics->average, 0, 'f', 1).arg(statistics->maximum, 0, 'f', 1).arg(statistics->minimum, 0, 'f', 1);
    }
    void drawRegionStatistics(QPainter &painter, const std::optional<GrayStats> &statistics, int pointSize)
    {
        painter.save();
        const QFont font("Segoe UI", pointSize);
        painter.setFont(font);
        const auto text = regionStatisticsText(statistics);
        QRect bounds = QFontMetrics(font, painter.device()).boundingRect(QRect(0, 0, 1000, 1000), Qt::AlignLeft | Qt::AlignTop, text);
        bounds.moveTopLeft(QPoint(12, 12));
        painter.setPen(Qt::green);
        painter.drawText(bounds, Qt::AlignLeft | Qt::AlignTop, text);
        painter.restore();
    }
    QImage renderOverlayImage(const cv::Mat &image, const std::vector<OverlayElement> &overlay,
                              const std::optional<GrayStats> *statistics)
    {
        if (image.empty())
            return {};
        cv::Mat pixels = image, rgb;
        if (pixels.depth() != CV_8U)
            image.convertTo(pixels, CV_8U, image.depth() == CV_16U ? 1.0 / 256.0 : 1.0);
        cv::cvtColor(pixels, rgb, pixels.channels() == 1 ? cv::COLOR_GRAY2RGB : pixels.channels() == 4 ? cv::COLOR_BGRA2RGB
                                                                                                       : cv::COLOR_BGR2RGB);
        QImage output = QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
        QPainter painter(&output);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const auto &e : overlay)
        {
            if (e.points.empty())
                continue;
            painter.save();
            painter.setPen(overlayPen(e));
            if (e.type == OverlayType::Cross)
                painter.translate(e.points[0].x + .5, e.points[0].y + .5);
            painter.drawPath(overlayPath(e));
            painter.restore();
        }
        if (statistics) drawRegionStatistics(painter, *statistics, 36);
        painter.end();
        return output;
    }
}
