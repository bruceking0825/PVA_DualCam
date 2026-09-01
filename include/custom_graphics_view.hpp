#pragma once
#include "models.hpp"
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPoint>

class CustomGraphicsView final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CustomGraphicsView(QWidget *parent = nullptr);
    void setViewId(int id) { viewId_ = id; }
    void setText(const QString &text);
    void showImage(const cv::Mat &image, bool preserveView = false);
    void updateOverlays(const std::vector<pva::OverlayElement> &elements);
signals:
    void pixelInfoChanged(int viewId, int x, int y, int gray);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QGraphicsScene scene_;
    QGraphicsPixmapItem *imageItem_{};
    QGraphicsItemGroup *overlayGroup_{};
    QGraphicsItemGroup *cursorItem_{};
    cv::Mat image_;
    std::vector<pva::OverlayElement> overlays_;
    int viewId_{1};
    bool fitted_{false};
    bool panning_{false};
    bool suppressResizeFit_{false};
    QPoint panStart_;
    double imageScale_{1.0};
    void drawCursor(int x, int y);
    void updatePixelInfo(int x, int y);
    void fitImage();
    void savePng();
    void saveFigure();
    void loadFigure();
};
