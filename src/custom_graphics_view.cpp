#include "custom_graphics_view.hpp"
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace
{
    QPixmap toPixmap(const cv::Mat &source)
    {
        cv::Mat rgb;
        if (source.channels() == 1)
            cv::cvtColor(source, rgb, cv::COLOR_GRAY2RGB);
        else
            cv::cvtColor(source, rgb, source.channels() == 4 ? cv::COLOR_BGRA2RGBA : cv::COLOR_BGR2RGB);
        const auto format = rgb.channels() == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        return QPixmap::fromImage(QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), format).copy());
    }
    QColor color(const cv::Scalar &bgr) { return QColor(cvRound(bgr[2]), cvRound(bgr[1]), cvRound(bgr[0])); }
}

CustomGraphicsView::CustomGraphicsView(QWidget *parent) : QGraphicsView(parent), scene_(this)
{
    setScene(&scene_);
    imageItem_ = scene_.addPixmap({});
    overlayGroup_ = new QGraphicsItemGroup();
    scene_.addItem(overlayGroup_);
    overlayGroup_->setZValue(1);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorUnderMouse);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
}
void CustomGraphicsView::setText(const QString &text)
{
    scene_.clear();
    scene_.addText(text)->setDefaultTextColor(Qt::white);
    imageItem_ = nullptr;
    overlayGroup_ = nullptr;
    cursorItem_ = nullptr;
    image_.release();
    fitted_ = false;
}
void CustomGraphicsView::showImage(const cv::Mat &image, bool preserveView)
{
    if (!imageItem_)
    {
        scene_.clear();
        imageItem_ = scene_.addPixmap({});
        overlayGroup_ = new QGraphicsItemGroup();
        scene_.addItem(overlayGroup_);
        overlayGroup_->setZValue(1);
        cursorItem_ = nullptr;
    }
    image_ = image.clone();
    imageItem_->setPixmap(toPixmap(image_));
    scene_.setSceneRect(imageItem_->boundingRect());
    if (!preserveView || !fitted_)
    {
        fitImage();
        fitted_ = true;
    }
}
void CustomGraphicsView::updateOverlays(const std::vector<pva::OverlayElement> &elements)
{
    overlays_ = elements;
    if (!overlayGroup_)
        return;
    for (auto *item : overlayGroup_->childItems())
        delete item;
    for (const auto &e : elements)
    {
        // 与 Python ContourItem 一致：线宽使用屏幕像素，不随视图缩放。
        QPen pen(color(e.colorBgr));
        pen.setWidthF(e.width);
        pen.setCosmetic(true);
        if (e.type == pva::OverlayType::Cross && !e.points.empty())
        {
            auto p = e.points[0];
            auto *a = new QGraphicsLineItem(-3, 0, 3, 0, overlayGroup_);
            a->setPos(p.x + 0.5, p.y + 0.5);
            a->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            a->setPen(pen);
            auto *b = new QGraphicsLineItem(0, -3, 0, 3, overlayGroup_);
            b->setPos(p.x + 0.5, p.y + 0.5);
            b->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            b->setPen(pen);
        }
        else if (e.points.size() > 1)
        {
            QPainterPath path;
            path.moveTo(e.points[0].x + 0.5, e.points[0].y + 0.5);
            for (size_t i = 1; i < e.points.size(); ++i)
                path.lineTo(e.points[i].x + 0.5, e.points[i].y + 0.5);
            if (e.closed)
                path.closeSubpath();
            auto *item = new QGraphicsPathItem(path, overlayGroup_);
            item->setPen(pen);
        }
    }
}
void CustomGraphicsView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    auto *png = menu.addAction("Save as PNG");
    auto *figure = menu.addAction("Save as Figure");
    auto *load = menu.addAction("Load Figure");
    const auto *selected = menu.exec(event->globalPos());
    if (selected == png)
        savePng();
    else if (selected == figure)
        saveFigure();
    else if (selected == load)
        loadFigure();
}
void CustomGraphicsView::savePng()
{
    if (image_.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Save PNG", {}, "PNG Files (*.png)");
    if (path.isEmpty())
        return;
    QImage output(sceneRect().size().toSize(), QImage::Format_ARGB32);
    output.fill(Qt::black);
    QPainter painter(&output);
    scene_.render(&painter);
    output.save(path);
}
void CustomGraphicsView::saveFigure()
{
    if (image_.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Save Figure", {}, "Figure Files (*.json)");
    if (path.isEmpty())
        return;
    std::vector<uchar> encoded;
    cv::imencode(".png", image_, encoded);
    QJsonArray elements;
    for (const auto &element : overlays_)
    {
        QJsonArray points;
        for (const auto &p : element.points)
            points.append(QJsonArray{p.x, p.y});
        elements.append(QJsonObject{{"type", int(element.type)}, {"points", points}, {"color", QJsonArray{element.colorBgr[0], element.colorBgr[1], element.colorBgr[2]}}, {"width", element.width}, {"closed", element.closed}});
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"image_png", QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(encoded.data()), qsizetype(encoded.size())).toBase64())}, {"overlays", elements}}).toJson(QJsonDocument::Indented));
}
void CustomGraphicsView::loadFigure()
{
    const QString path = QFileDialog::getOpenFileName(this, "Load Figure", {}, "Figure Files (*.json)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const QByteArray encoded = QByteArray::fromBase64(object.value("image_png").toString().toLatin1());
    const cv::Mat buffer(1, encoded.size(), CV_8U, const_cast<char *>(encoded.constData()));
    const cv::Mat image = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
    if (image.empty())
        return;
    std::vector<pva::OverlayElement> elements;
    for (const auto &entry : object.value("overlays").toArray())
    {
        const auto value = entry.toObject();
        pva::OverlayElement element;
        element.type = pva::OverlayType(value.value("type").toInt());
        for (const auto &pointValue : value.value("points").toArray())
        {
            const auto point = pointValue.toArray();
            if (point.size() >= 2)
                element.points.emplace_back(point[0].toDouble(), point[1].toDouble());
        }
        const auto color = value.value("color").toArray();
        if (color.size() >= 3)
            element.colorBgr = {color[0].toDouble(), color[1].toDouble(), color[2].toDouble()};
        element.width = value.value("width").toInt(2);
        element.closed = value.value("closed").toBool(false);
        elements.push_back(std::move(element));
    }
    showImage(image);
    updateOverlays(elements);
}
void CustomGraphicsView::wheelEvent(QWheelEvent *e)
{
    const double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double targetScale = transform().m11() * factor;
    if (targetScale < 0.001 || targetScale > 20.0)
    {
        e->accept();
        return;
    }
    suppressResizeFit_ = true;
    scale(factor, factor);
    imageScale_ = transform().m11();
    QTimer::singleShot(0, this, [this]
                       { suppressResizeFit_ = false; });
    e->accept();
}
void CustomGraphicsView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && !image_.empty())
    {
        auto p = mapToScene(e->pos());
        int x = int(p.x()), y = int(p.y());
        if (x >= 0 && x < image_.cols && y >= 0 && y < image_.rows)
        {
            drawCursor(x, y);
            updatePixelInfo(x, y);
            panning_ = true;
            panStart_ = e->pos();
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}
void CustomGraphicsView::mouseMoveEvent(QMouseEvent *e)
{
    if (panning_)
    {
        const QPoint delta = e->pos() - panStart_;
        panStart_ = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}
void CustomGraphicsView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && panning_)
    {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}
void CustomGraphicsView::keyPressEvent(QKeyEvent *e)
{
    if (!cursorItem_ || image_.empty())
    {
        QGraphicsView::keyPressEvent(e);
        return;
    }
    int dx = 0, dy = 0;
    if (e->key() == Qt::Key_Left)
        dx = -1;
    else if (e->key() == Qt::Key_Right)
        dx = 1;
    else if (e->key() == Qt::Key_Up)
        dy = -1;
    else if (e->key() == Qt::Key_Down)
        dy = 1;
    else
    {
        QGraphicsView::keyPressEvent(e);
        return;
    }
    const int x = qRound(cursorItem_->pos().x() - 0.5) + dx;
    const int y = qRound(cursorItem_->pos().y() - 0.5) + dy;
    if (x >= 0 && x < image_.cols && y >= 0 && y < image_.rows)
    {
        drawCursor(x, y);
        updatePixelInfo(x, y);
    }
    e->accept();
}
void CustomGraphicsView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    if (!image_.empty() && !suppressResizeFit_)
        fitImage();
}
void CustomGraphicsView::drawCursor(int x, int y)
{
    if (cursorItem_)
    {
        scene_.removeItem(cursorItem_);
        delete cursorItem_;
    }
    cursorItem_ = new QGraphicsItemGroup();
    QPen pen(QColor(0, 255, 0), 2);
    auto *horizontal = new QGraphicsLineItem(-9, 0, 9, 0, cursorItem_);
    auto *vertical = new QGraphicsLineItem(0, -9, 0, 9, cursorItem_);
    horizontal->setPen(pen);
    vertical->setPen(pen);
    cursorItem_->setPos(x + 0.5, y + 0.5);
    cursorItem_->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    cursorItem_->setZValue(1000);
    scene_.addItem(cursorItem_);
}
void CustomGraphicsView::updatePixelInfo(int x, int y)
{
    cv::Mat gray;
    if (image_.channels() == 1)
        gray = image_;
    else
        cv::cvtColor(image_, gray, image_.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    emit pixelInfoChanged(viewId_, x, y, gray.at<uchar>(y, x));
}
void CustomGraphicsView::fitImage()
{
    if (!imageItem_ || imageItem_->pixmap().isNull())
        return;
    fitInView(imageItem_, Qt::KeepAspectRatio);
    imageScale_ = transform().m11();
}
