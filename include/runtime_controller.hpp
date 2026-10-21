#pragma once
#include <QObject>
#include "sherlock_protocol.hpp"
#include "plc_session.hpp"
#include "config.hpp"
#include "measurement_worker.hpp"
#include "plc_runtime.hpp"
#include "daily_log.hpp"
#include <QHash>
#include <QQueue>
#include <QElapsedTimer>
#include <array>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace pva
{
    class ImageStorage;

    enum class RunState { Stopped, Starting, Running, Stopping, Faulted };
    struct RuntimeSnapshot
    {
        RunState state{RunState::Stopped};
        bool online{};
        MeasurementStage stage{MeasurementStage::Melt};
        QStringList images;
        int imageIndex{-1};
        MeasurementConfig config;
        MeasurementRois rois;
        quint64 generation{};
    };
    // 运行控制不依赖 QWidget，在专属线程中串行处理业务事件。
    class RuntimeController final : public QObject
    {
        Q_OBJECT
    public:
        explicit RuntimeController(MeasurementConfig config, QObject *parent = nullptr, bool enableTcp = true);
        ~RuntimeController() override;
        std::optional<MeasurementResult> takeLatestResult();
        PlcSession &plcSession() { return *plc_; }
    public slots:
        // 页面/装配层入口：这些方法在运行线程执行。
        void initialize();
        void shutdown();
        void reloadConfig(const MeasurementConfig &config);
        void startRuntime(bool online);
        void stopRuntime();
        void changeMode(bool online);
        void selectStage(MeasurementStage stage);
        // 离线切图。
        void setImageIndex(int index);
        void stepImage(int delta);
        void submitOfflineFrame();

        // 在线相机事件。
        void triggerOnlineCapture();
        void onCameraFrame(const QString &userId, const cv::Mat &image, qint64 timestampNs, quint64 generation);
        void onFacetFrame(int requestId, const cv::Mat &image);
        void onOnlineCameraStarted(quint64 session = 0);
        void onOnlineCameraStopped(quint64 session = 0);
        void onOnlineCameraStopFailed(quint64 requestId, const QString &message);
        void onOnlineCameraFailed(const QString &message, quint64 session = 0);
        void onOnlineCaptureFailed(const QString &userId, const QString &message, quint64 session = 0);
        void onFacetTriggerFailed(int requestId, const QString &message);
        void expireFacetRequests();
        // PLC 请求与测量结果。
        void acceptResult(const pva::MeasurementResult &result);
        void onSherlockCommand(const pva::SherlockCommand &command);

    signals:
        void stateChanged(const pva::RuntimeSnapshot &snapshot);
        void resultReady(const pva::MeasurementResult &result);
        void status(const QString &message, bool ok);
        void logReady(const QString &line, bool merged);
        void facetteReady(int index, const cv::Mat &image);
        void frameDeltaChanged(double deltaMs);
        void onlineCameraStartRequested(quint64 session);
        void onlineCameraStopRequested(quint64 requestId);
        void onlineCameraTriggerRequested(quint64 generation);
        void onlineFacetTriggerRequested(int requestId);
        void onlineStageChanged(int stage);
    private:
        void publishState();
        void invalidateTasks();
        MeasurementTaskInfo taskInfo();
        bool running() const { return state_ == RunState::Running; }
        void reloadImages(bool preserve = false);
        void captureOfflineFacette(int index);
        void retryCameraClose();
        void startPlc();
        void handleAcquisitionCommand(const SherlockCommand &command);
        void completeAcquisition(bool enabled);
        void failAcquisition(const QString &message);
        QString plcStatePath() const;
        void restorePlcState();
        void log(const QString &message);

        // 运行状态只在控制线程修改；显示邮箱使用互斥锁跨线程读取。
        MeasurementConfig config_;
        QMutex displayMutex_;
        std::optional<MeasurementResult> displayResult_;
        quint64 runId_{}, nextRequestId_{}, currentRequestId_{}, configVersion_{1};
        std::optional<std::chrono::steady_clock::time_point> triggerStarted_;
        std::unique_ptr<MeasurementWorker> worker_;
        PlcSession *plc_{};
        ImageStorage *images_{};
        ImageStorage *continuousImages_{};
        QElapsedTimer continuousSaveClock_;
        QTimer *offlineTimer_{};
        QTimer *logTimer_{};
        QTimer *facetTimeoutTimer_{};
        QTimer *acquisitionTimer_{};
        QString acquisitionCommand_;
        QElapsedTimer acquisitionClock_;
        std::optional<bool> restartAfterCameraStop_;
        quint64 nextStopRequestId_{}, stopRequestId_{};
        bool awaitingCameraStop_{false}, cleanupRetryRequested_{false};
        QStringList imagePaths_;
        int imageIndex_{-1};
        MeasurementStage stage_{MeasurementStage::Melt};
        RunState state_{RunState::Stopped};
        quint64 generation_{1};
        bool networkEnabled_{true};
        bool activeOnline_{false};
        DailyLog dailyLog_;
        struct OnlineFrame { qint64 timestampNs{}; cv::Mat image; };
        QHash<QString, OnlineFrame> onlineFrames_;
        struct Camera2Request { int id{}; int facetIndex{}; qint64 deadlineMs{}; };
        QQueue<Camera2Request> camera2Requests_;
        int nextFacetRequestId_{1};
    };
}

Q_DECLARE_METATYPE(pva::RuntimeSnapshot)
