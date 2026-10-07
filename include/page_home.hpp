#pragma once
#include "base_page.hpp"
#include "runtime_controller.hpp"
#include <QElapsedTimer>
#include <QHash>
#include <memory>
class QTimer;
class QLabel;
class QTreeWidgetItem;
QT_BEGIN_NAMESPACE
namespace Ui { class PageHome; }
QT_END_NAMESPACE
namespace pva
{
    // 页面只保存显示快照，不拥有采集、PLC 或测量运行状态。
    class PageHome final : public BasePage
    {
        Q_OBJECT
    public:
        PageHome(MeasurementConfig config, RuntimeController &runtime, QWidget *parent = nullptr);
        ~PageHome() override;
        void reloadConfig(const MeasurementConfig &config);
    public slots:
        void onCameraExposure(const QString &userId, double exposureUs);
    private slots:
        void toggleRuntime();
        void toggleOnline(bool online);
        void selectStage();
        void firstImage();
        void previousImage();
        void nextImage();
        void lastImage();
        void showResult(const MeasurementResult &result);
        void onSherlockCommand(const SherlockCommand &command);
        void renderLatest();
    private:
        void initializeState() override;
        void setupPageUi() override;
        void bindEvents() override;
        void bindSignals() override;
        void onReady() override;
        void refreshControls();
        void applyStageToUi();
        void updateProcessDiagnostics(const MeasurementResult &result);
        void setConnectionLed(QLabel *label, bool connected);
        void addAutoExposureRoi(std::vector<OverlayElement> &elements, const cv::Rect &roi, const cv::Size &size) const;
        static double roiMean(const cv::Mat &image, const cv::Rect &roi);
        void updateViewInfo(int viewId);
        void paintResult(const MeasurementResult &result);
        std::unique_ptr<Ui::PageHome> ui_;
        RuntimeController &runtime_;
        RuntimeSnapshot snapshot_;
        QTimer *renderTimer_{};
        QElapsedTimer diagnosticClock_;
        std::optional<MeasurementResult> latestResult_;
        bool frameDirty_{false};
        bool diagnosticDirty_{false};
        std::array<cv::Mat, 4> facetteImages_;
        std::array<bool, 4> facetteDirty_{};
        QHash<QString, QTreeWidgetItem *> diagnosticItems_;
        struct ViewInfo
        {
            int x{}, y{}, gray{};
            std::optional<double> light, exposureUs, roiMean;
        };
        std::array<ViewInfo, 2> viewInfo_{};
    };
}
