#pragma once
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
        void onlineStageChanged(int stage);
        void onlineCameraStarted();
        void onlineCameraStopped();
        void onlineCameraFailed(const QString &message);
        void cameraFrameCaptured(const QString &role, const cv::Mat &image);
        void cameraExposureChanged(const QString &role, double exposureUs);
        void appClose();
    };
}
