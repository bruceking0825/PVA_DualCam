#pragma once
#include "camera_manager.hpp"
#include <QObject>
#include <opencv2/core.hpp>

Q_DECLARE_METATYPE(cv::Mat)

namespace pva
{
    class AppSignals final : public QObject
    {
        Q_OBJECT
    public:
        static AppSignals &instance();
    signals:
        void status(const QString &device, const QString &state, const QString &type, const QString &message);
        void onlineCameraStartRequested();
        void onlineCameraStopRequested();
        void onlineCameraTriggerRequested();
        void onlineFacetTriggerRequested(int requestId);
        void onlineFacetTriggerFailed(int requestId, const QString &message);
        void onlineCaptureFailed(const QString &userId, const QString &message);
        void plcCameraExposureRequested(const QString &userId, double exposureUs);
        void onlineStageChanged(int stage);
        void onlineCameraStarted();
        void onlineCameraStopped();
        void onlineCameraFailed(const QString &message);
        void cameraFrameCaptured(const QString &userId, const cv::Mat &image, qint64 timestampNs);
        void cameraExposureChanged(const QString &userId, double exposureUs);
        void appClose();
    };
}
