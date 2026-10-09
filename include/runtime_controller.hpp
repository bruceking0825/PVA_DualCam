#pragma once
#include <QObject>
#include "sherlock_protocol.hpp"
#include "plc_session.hpp"
#include "camera_manager.hpp"
#include "config.hpp"
#include "measurement_worker.hpp"
#include "plc_runtime.hpp"
#include "daily_log.hpp"
#include <QHash>
#include <QQueue>
#include <array>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace pva
{
    class SherlockTcpServer;
    class ImageStorage;
    struct SherlockCommand;

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
        void reloadConfig(const MeasurementConfig &config);
    public slots:
        void initialize();
        void selectStage(MeasurementStage stage);
        void acceptResult(const pva::MeasurementResult &result);
        void submitOfflineFrame();
        void triggerOnlineCapture();
        void onCameraFrame(const QString &userId, const cv::Mat &image, qint64 timestampNs, quint64 generation);
        void onFacetFrame(int requestId, const cv::Mat &image);
        void onOnlineCameraStarted(quint64 session = 0);
        void onOnlineCameraStopped(quint64 session = 0);
        void onOnlineCameraFailed(const QString &message, quint64 session = 0);
        void onOnlineCaptureFailed(const QString &userId, const QString &message, quint64 session = 0);
        void onFacetTriggerFailed(int requestId, const QString &message);
        void expireFacetRequests();
        void onSherlockCommand(const pva::SherlockCommand &command);

    signals:
        void stateChanged(const pva::RuntimeSnapshot &snapshot);
        void resultReady(const pva::MeasurementResult &result);
        void status(const QString &message, bool ok);
        void logReady(const QString &line, bool merged);
        void facetteReady(int index, const cv::Mat &image);
        void cameraConnectionChanged(int camera, bool connected);
        void plcConnectionChanged(bool connected);
        void frameDeltaChanged(double deltaMs);
        void payloadReady(const QByteArray &payload);
        void onlineCameraStartRequested(quint64 session);
        void onlineCameraStopRequested();
        void onlineCameraTriggerRequested(quint64 generation);
        void onlineFacetTriggerRequested(int requestId);
        void onlineStageChanged(int stage);
        void plcCameraExposureRequested(const QString &userId, double exposureUs);
    private:
        void publishState();
        void invalidateTasks();
        void submitImages(const cv::Mat &a, const cv::Mat &b, MeasurementStage stage);
        MeasurementTaskInfo taskInfo();
        MeasurementConfig baseConfig_;
        MeasurementConfig config_;
        QMutex displayMutex_;
        std::optional<MeasurementResult> displayResult_;
        quint64 runId_{}, nextRequestId_{}, currentRequestId_{}, configVersion_{1};
        std::unique_ptr<MeasurementWorker> worker_;
        PlcSession *plc_{};
        ImageStorage *images_{};
        QTimer *offlineTimer_{};
        QTimer *facetTimeoutTimer_{};
        QStringList imagePaths_;
        int imageIndex_{-1};
        MeasurementStage stage_{MeasurementStage::Melt};
        RunState state_{RunState::Stopped};
        quint64 generation_{1};
        bool networkEnabled_{true};
        bool running() const { return state_ == RunState::Running; }
        bool activeOnline_{false};
        DailyLog dailyLog_;
        struct OnlineFrame { qint64 timestampNs{}; cv::Mat image; };
        QHash<QString, OnlineFrame> onlineFrames_;
        struct Camera2Request { int id{}; int facetIndex{}; qint64 deadlineMs{}; };
        QQueue<Camera2Request> camera2Requests_;
        int nextFacetRequestId_{1};
        void reloadImages(bool preserve = false);
    public slots:
        void setImageIndex(int index);
        void stepImage(int delta);
    private:
        void log(const QString &message);
        QString plcStatePath() const;
        bool persistPlcState(QString *error = nullptr) const;
        void restorePlcState();
        void applyPlcConfigOverrides();
        void setStatus(const QString &message, bool ok);
        void loadFacette(int index);
        void captureOfflineFacette(int index);
    public slots:
        void startRuntime(bool online);
        void stopRuntime();
    private:
        void startPlc();
        void stopPlc();
        void sendPlcPayload(const QByteArray &payload);
    };
}

Q_DECLARE_METATYPE(pva::RuntimeSnapshot)
