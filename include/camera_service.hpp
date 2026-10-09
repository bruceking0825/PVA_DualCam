#pragma once
#include "camera_manager.hpp"
#include "config.hpp"
#include "models.hpp"
#include <QHash>
#include <QQueue>
#include <QTimer>
#include <QMutex>
namespace pva {
class DalsaCamera;
struct CameraSnapshot
{
    QStringList ids;
    QString selected;
    bool open{}, streaming{}, production{};
    double exposure{}, gain{};
    qint64 width{}, height{}, offsetX{}, offsetY{}, triggerMode{}, triggerSource{}, triggerEdge{};
};
// 所有 SDK 操作在相机服务线程串行执行，页面只提交命令与接收快照。
class CameraService final : public QObject
{
    Q_OBJECT
public:
    explicit CameraService(MeasurementConfig config, QObject *parent = nullptr);
    ~CameraService() override;
    cv::Mat takePreviewFrame();
public slots:
    void initialize();
    void reloadConfig(const MeasurementConfig &config);
    void refreshCameras();
    void selectCamera(const QString &id);
    void toggleCamera(bool checked);
    void toggleStream(bool checked);
    void softwareTrigger();
    void applyExposure(double requested);
    void applyGain(double requested);
    void applyWidth(double requested);
    void applyHeight(double requested);
    void applyOffsetX(double requested);
    void applyOffsetY(double requested);
    void applyTriggerMode(int requested);
    void applyTriggerSource(int requested);
    void applyTriggerEdge(int requested);
    void startOnlineCameras(quint64 session);
    void stopOnlineCameras();
    void triggerOnlineCameras(quint64 generation);
    void triggerOnlineFacet(int requestId);
    void applyPlcExposure(const QString &userId, double exposureUs);
    void setStage(int stage) { onlineStage_ = MeasurementStage(stage); }
    void closeAll();
signals:
    void stateChanged(const pva::CameraSnapshot &snapshot);
    void statusChanged(bool ok, const QString &message);
    void onlineCameraStarted(quint64 session);
    void onlineCameraStopped(quint64 session);
    void onlineCameraFailed(const QString &message, quint64 session);
    void onlineFacetTriggerFailed(int requestId, const QString &message);
    void onlineCaptureFailed(const QString &userId, const QString &message, quint64 session);
    void cameraFrameCaptured(const QString &userId, const cv::Mat &image, qint64 timestampNs, quint64 generation);
    void facetFrameCaptured(int requestId, const cv::Mat &image);
    void cameraExposureChanged(const QString &userId, double exposureUs);
private:
    void onFrame(const QString &userId, const cv::Mat &frame, qint64 timestampNs);
    void onCaptureFailed(const QString &userId, const QString &message);
    DalsaCamera *camera(const QString &id) const;
    bool applyConfiguredParameters(DalsaCamera &camera, const cv::Rect &roi, bool online, QString *error);
    void adjustAutoExposure(DalsaCamera &camera, const cv::Mat &frame, qint64 timestampNs);
    double loadRememberedExposure(const QString &id, double fallback) const;
    void saveRememberedExposures() const;
    void publishState();
    void setStatus(bool ok, const QString &message) { emit statusChanged(ok, message); }
    struct CaptureRequest { int facetId{}; quint64 generation{}; };
    QQueue<CaptureRequest> captureQueue_;
    std::optional<CaptureRequest> activeCapture_;
    bool gotCam1_{}, gotCam2_{};
    QTimer *captureTimeout_{};
    void pumpCapture();
    quint64 session_{};
    QMutex previewMutex_;
    cv::Mat previewFrame_;
    MeasurementConfig config_;
    std::unique_ptr<CameraManager> cameraManager_;
    DalsaCamera *current_{};
    QString streamOwner_;
    MeasurementStage onlineStage_{MeasurementStage::Melt};
    QHash<QString, qint64> lastExposureAdjustNs_, lastExposurePublishNs_;
    QHash<QString, double> plcExposureOverrides_;
    qint64 lastManualPreviewNs_{};
};
}
Q_DECLARE_METATYPE(pva::CameraSnapshot)
