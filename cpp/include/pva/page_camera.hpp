#pragma once

#include "pva/config.hpp"
#include "pva/measurement_engine.hpp"
#include <QElapsedTimer>
#include <QHash>
#include <QWidget>
#include <map>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class PageCamera; }
QT_END_NAMESPACE

namespace pva
{
    class DalsaCamera;

    class PageCamera final : public QWidget
    {
        Q_OBJECT
    public:
        explicit PageCamera(MeasurementConfig config, QWidget *parent = nullptr);
        ~PageCamera() override;
        void reloadConfig(const MeasurementConfig &config);

    private slots:
        void openImage();
        void refreshCameras();
        void selectCamera(int index);
        void toggleCamera(bool checked);
        void toggleStream(bool checked);
        void softwareTrigger();
        void applyExposure();
        void applyGain();
        void applyWidth();
        void applyHeight();
        void applyOffsetX();
        void applyOffsetY();
        void applyTriggerMode(int index);
        void applyTriggerSource(int index);
        void applyTriggerEdge(int index);
        void startOnlineCameras();
        void stopOnlineCameras();
        void triggerOnlineCameras();
        void onFrame(const QString &role, const cv::Mat &frame, qint64 timestampNs);
        void onCaptureFailed(const QString &role, const QString &message);
        void runPreviewPipeline();
        void loadPipeline();
        void savePipeline();

    private:
        std::unique_ptr<Ui::PageCamera> ui_;
        MeasurementConfig config_;
        std::map<QString, std::unique_ptr<DalsaCamera>> cameras_;
        DalsaCamera *current_{};
        QString streamOwner_;
        MeasurementStage onlineStage_{MeasurementStage::Idle};
        cv::Mat originalImage_;
        QString graphPath_;
        QHash<QString, qint64> lastExposureAdjustNs_;
        QHash<QString, qint64> lastExposurePublishNs_;
        QElapsedTimer clock_;
        qint64 lastManualPreviewNs_{};
        bool sdkInitialized_{false};
        bool cameraDiscoveryRunning_{false};

        DalsaCamera *camera(const QString &role) const;
        bool applyConfiguredParameters(DalsaCamera &camera, const cv::Rect &roi, bool online, QString *error);
        void closeAll();
        void refreshUi();
        void setManualControlsEnabled(bool enabled);
        void setStatus(bool ok, const QString &message);
        void adjustAutoExposure(DalsaCamera &camera, const cv::Mat &frame, qint64 timestampNs);
        double loadRememberedExposure(const QString &role, double fallback) const;
        void saveRememberedExposures() const;
    };
}
