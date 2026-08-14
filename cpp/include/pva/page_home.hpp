#pragma once
#include "pva/config.hpp"
#include "pva/measurement_worker.hpp"
#include <QWidget>
#include <QElapsedTimer>
#include <QHash>
#include <array>
#include <memory>
#include <optional>

class QTimer;
class QLabel;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class PageHome;
}
QT_END_NAMESPACE

namespace pva
{
    class OpcUaWorker;

    class PageHome final : public QWidget
    {
        Q_OBJECT
    public:
        explicit PageHome(MeasurementConfig config, QWidget *parent = nullptr);
        ~PageHome() override;
        void reloadConfig(const MeasurementConfig &config);
    private slots:
        void toggleRuntime();
        void toggleOnline(bool online);
        void selectStage();
        void firstImage();
        void previousImage();
        void nextImage();
        void lastImage();
        void submitOfflineFrame();
        void showResult(const pva::MeasurementResult &result);
        void triggerOnlineCapture();
        void onCameraFrame(const QString &role, const cv::Mat &image);
        void onCameraExposure(const QString &role, double exposureUs);
        void onOnlineCameraStarted();
        void onOnlineCameraStopped();
        void onOnlineCameraFailed(const QString &message);
        void onPlcControls(int stageValue, bool shoulderTransition);

    private:
        std::unique_ptr<Ui::PageHome> ui_;
        MeasurementConfig config_;
        std::unique_ptr<MeasurementWorker> worker_;
        std::unique_ptr<OpcUaWorker> plcWorker_;
        QTimer *offlineTimer_{};
        QTimer *onlineTimer_{};
        QStringList imagePaths_;
        int imageIndex_{-1};
        MeasurementStage stage_{MeasurementStage::Idle};
        bool running_{false};
        bool activeOnline_{false};
        struct OnlineFrame { qint64 timestampNs{}; cv::Mat image; };
        QHash<QString, OnlineFrame> onlineFrames_;
        QElapsedTimer onlineClock_;
        struct ViewInfo
        {
            int x{};
            int y{};
            int gray{};
            std::optional<double> light;
            std::optional<double> exposureUs;
            std::optional<double> roiMean;
        };
        std::array<ViewInfo, 2> viewInfo_{};
        void reloadImages(bool preserve = false);
        void setImageIndex(int index);
        void refreshControls();
        void log(const QString &message);
        void setStatus(const QString &message, bool ok);
        static cv::Mat readImage(const QString &path);
        void updateProcessDiagnostics(const MeasurementResult &result);
        void startRuntime(bool online);
        void stopRuntime();
        int onlineSampleInterval() const;
        void setConnectionLed(QLabel *label, bool connected);
        void addAutoExposureRoi(std::vector<OverlayElement> &elements, const cv::Rect &roi, const cv::Size &size) const;
        static double roiMean(const cv::Mat &image, const cv::Rect &roi);
        void updateViewInfo(int viewId);
        void startPlc();
        void stopPlc();
        void applyStageToUi();
    };
}
