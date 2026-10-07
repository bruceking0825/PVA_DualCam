#pragma once
#include "base_page.hpp"
#include "camera_service.hpp"
#include <memory>
#include <QThreadPool>
QT_BEGIN_NAMESPACE
namespace Ui { class PageCamera; }
QT_END_NAMESPACE
namespace pva {
class PageCamera final : public BasePage {
    Q_OBJECT
public:
    PageCamera(CameraService &service, QWidget *parent = nullptr);
    ~PageCamera() override;
private:
    void showEvent(QShowEvent *event) override;
    void startPreview();
    void initializeState() override;
    void setupPageUi() override;
    void bindEvents() override;
    void bindSignals() override;
    void onReady() override;
    void openImage();
    void refreshCameras();
    void selectCamera(int index);
    void toggleCamera(bool checked);
    void toggleStream(bool checked);
    void softwareTrigger();
    void applyExposure(); void applyGain(); void applyWidth(); void applyHeight(); void applyOffsetX(); void applyOffsetY();
    void applyTriggerMode(int); void applyTriggerSource(int); void applyTriggerEdge(int);
    void runPreviewPipeline(); void loadPipeline(); void savePipeline();
    void refreshUi(); void setManualControlsEnabled(bool enabled); void setStatus(bool ok, const QString &message);
    std::unique_ptr<Ui::PageCamera> ui_;
    CameraService &service_;
    CameraSnapshot snapshot_;
    cv::Mat originalImage_;
    QString graphPath_;
    QString pendingImagePath_;
    QThreadPool previewPool_;
    quint64 previewRevision_ = 0;
    bool previewBusy_ = false;
    bool previewPending_ = false;
};
}
