#pragma once
#include "models.hpp"
#include "plc_runtime.hpp"
#include "config.hpp"
#include "sherlock_tcp_server.hpp"
#include <QTimer>
namespace pva {
// 每个 PLC 会话只允许一个待回复测量；超时、完成和取消在同一线程串行执行。
class PlcSession final : public QObject {
    Q_OBJECT
public:
    explicit PlcSession(QObject *parent = nullptr, int timeoutMs = 5000);
    MeasurementStage restoreSettings(const QString &path);
    bool saveStage(MeasurementStage stage, QString *error = nullptr) const;
    bool handleControlCommand(const SherlockCommand &command, MeasurementStage stage, MeasurementConfig config);
    void applyConfigOverrides(MeasurementConfig &config) const;
    void replayExposures();
    bool acquisitionEnabled() const { return settings_.acquisitionEnabled; }
    bool pointFitSelected() const { return settings_.pointFitSelected; }
    const MeasurementRois &rois() const { return rois_; }
    void setStatePath(QString path) { statePath_ = std::move(path); }
    bool start(QString *error = nullptr);
    void stop();
    bool busy() const { return !command_.isEmpty(); }
    bool begin(const QString &command, quint64 generation);
    void complete(const MeasurementResult &result);
    void cancel();
    void send(const QByteArray &payload);
signals:
    void configurationChanged(pva::MeasurementStage stage);
    void cameraExposureRequested(const QString &userId, double exposureUs);
    void commandReceived(const pva::SherlockCommand &command);
    void connectionChanged(bool connected);
    void payloadReady(const QByteArray &payload);
    void logMessage(const QString &message);
    void timedOut();
private:
    PlcRuntimeState settings_;
    MeasurementRois rois_;
    QString statePath_;
    SherlockTcpServer server_;
    QTimer timeout_;
    QString command_;
    quint64 generation_{};
    bool listening_{};
};
}
