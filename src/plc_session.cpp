#include "plc_session.hpp"
#include "measurement_payload.hpp"
#include "camera_manager.hpp"
#include <algorithm>
namespace pva {
PlcSession::PlcSession(QObject *parent, int timeoutMs) : QObject(parent), server_(this), timeout_(this)
{
    timeout_.setSingleShot(true); timeout_.setInterval(timeoutMs);
    connect(&server_, &SherlockTcpServer::commandReceived, this, &PlcSession::commandReceived);
    connect(&server_, &SherlockTcpServer::connectionChanged, this, &PlcSession::connectionChanged);
    connect(&server_, &SherlockTcpServer::failed, this, &PlcSession::logMessage);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!busy()) return;
        cancel(); send("exe_err=Commn err"); emit timedOut();
    });
}
bool PlcSession::start(QString *error) { if (listening_) return true; listening_ = server_.start(error); return listening_; }
void PlcSession::stop() { cancel(); server_.stop(); listening_ = false; }
bool PlcSession::begin(const QString &command, quint64 generation)
{
    if (busy()) return false;
    command_ = command; generation_ = generation; timeout_.start(); return true;
}
void PlcSession::cancel() { command_.clear(); timeout_.stop(); }
void PlcSession::send(const QByteArray &payload)
{
    emit payloadReady(payload);
    if (!listening_) return;
    QString error;
    if (server_.sendPayload(payload, &error)) emit logMessage("PLC <- " + QString::fromLatin1(payload));
    else if (!error.isEmpty()) emit logMessage(error);
}
void PlcSession::complete(const MeasurementResult &result)
{
    if (!busy() || result.generation != generation_) return;
    const auto name = command_ == "dip_msr" ? "dip" : command_ == "mlt_msr" ? "mlt" : "dia";
    cancel();
    send(result.valid ? SherlockProtocol::scaledPayload(name, measurementPayloadValues(result)) : QByteArray("exe_err=Commn err"));
}
    bool PlcSession::handleControlCommand(const SherlockCommand &command, MeasurementStage stage_, MeasurementConfig config_)
    {
        const QString &name = command.name;
        if (name == "acq_on_")
        {
            const bool previous = settings_.acquisitionEnabled;
            settings_.acquisitionEnabled = true;
            QString error;
            if (!saveStage(stage_, &error))
            {
                settings_.acquisitionEnabled = previous;
                send("err_exc=state save failed");
                emit logMessage("PLC state save failed: " + error);
                return true;
            }
            send("acq_on_=ok");
            return true;
        }
        if (name == "acq_off")
        {
            const bool previous = settings_.acquisitionEnabled;
            settings_.acquisitionEnabled = false;
            QString error;
            if (!saveStage(stage_, &error))
            {
                settings_.acquisitionEnabled = previous;
                send("err_exc=state save failed");
                emit logMessage("PLC state save failed: " + error);
                return true;
            }
            send("acq_off=ok");
            return true;
        }
        if (name == "acq_get")
        {
            send("acq_get=" + QByteArray::number(settings_.acquisitionEnabled ? 1 : 0));
            return true;
        }
        if (name == "ver_get")
        {
            send("ver_get=414");
            return true;
        }
        if (name == "rfr_get")
        {
            send(SherlockProtocol::scaledPayload("rfr_get", {settings_.refreshRate}));
            return true;
        }
        if (name == "rfr_set")
        {
            if (command.parameters.size() != 1)
            {
                send("err_prm=rfr_set requires one parameter");
                return true;
            }
            const auto value = SherlockProtocol::fromPlcNumber(command.parameters.front());
            if (!value || *value <= 0.0)
            {
                send("err_prm=invalid refresh rate");
                return true;
            }
            const double previous = settings_.refreshRate;
            settings_.refreshRate = *value;
            QString error;
            if (!saveStage(stage_, &error))
            {
                settings_.refreshRate = previous;
                send("err_exc=state save failed");
                emit logMessage("PLC state save failed: " + error);
                return true;
            }
            send("rfr_set=ok");
            return true;
        }

        const bool isModeCommand = name == "thr_rel" || name == "thr_abs";
        const bool isActionCommand = name == "cfit_ne" || name == "pfit_sb";
        if (isModeCommand || isActionCommand)
        {
            if (!command.parameters.isEmpty())
            {
                send("err_prm=" + name.toLatin1() + " requires no parameters");
                return true;
            }
            const bool oldRelative = settings_.relativeThreshold;
            const bool oldPointFit = settings_.pointFitSelected;
            const auto oldStage = stage_;
            if (isModeCommand)
                settings_.relativeThreshold = name == "thr_rel";
            else if (name == "cfit_ne" || name == "pfit_sb")
            {
                settings_.pointFitSelected = name == "pfit_sb";
                stage_ = stageForPlcCommand(name, stage_, settings_.pointFitSelected, rois_,
                                            config_.measurement.crownBodyInnerRadiusPx);
            }
            QString stateError;
            if (!saveStage(stage_, &stateError))
            {
                settings_.relativeThreshold = oldRelative;
                settings_.pointFitSelected = oldPointFit;
                stage_ = oldStage;
                send("err_exc=state save failed");
                emit logMessage("PLC state save failed: " + stateError);
                return true;
            }
            emit configurationChanged(stage_);
            if (isModeCommand)
                emit logMessage(QString("PLC diameter threshold mode: %1")
                        .arg(settings_.relativeThreshold ? "relative" : "absolute"));
            send(name.toLatin1() + "=ok");
            return true;
        }

        int expectedCount = -1;
        if (name == "mlt_crd")
            expectedCount = 6;
        else if (name == "dia_crd")
            expectedCount = 8;
        else if (name == "rec_crd")
            expectedCount = 4;
        else if (name == "dip_crd" || name == "vib_crd")
            expectedCount = 3;
        else if (name == "mlt_thr" || name == "dip_thr" || name == "dia_thr" ||
                 name == "diathr2" || name == "rec_thr" || name == "vib_thr" ||
                 name == "exptme1" || name == "exptme2")
            expectedCount = 1;
        if (expectedCount < 0)
            return false;

        QString error;
        const auto values = SherlockProtocol::parseScaledParameters(command, expectedCount, &error);
        if (!values)
        {
            send("err_prm=" + error.toLatin1());
            return true;
        }
        if ((name == "exptme1" || name == "exptme2") && values->front() <= 0.0)
        {
            send("err_prm=exposure must be positive");
            return true;
        }
        const auto oldParameters = settings_.parameters;
        const auto oldRois = rois_;
        const auto oldStage = stage_;
        if (name == "dia_crd" || name == "mlt_crd" || name == "dip_crd")
        {
            if (!setPlcRoi(rois_, name, *values, &error))
            {
                send("err_prm=" + error.toLatin1());
                return true;
            }
        }
        settings_.parameters.insert(name, *values);
        if (name == "dia_crd" || name == "mlt_crd" || name == "dip_crd")
            stage_ = stageForPlcCommand(name, stage_, settings_.pointFitSelected, rois_,
                                        config_.measurement.crownBodyInnerRadiusPx);
        if (!saveStage(stage_, &error))
        {
            settings_.parameters = oldParameters;
            rois_ = oldRois;
            stage_ = oldStage;
            send("err_exc=state save failed");
            emit logMessage("PLC state save failed: " + error);
            return true;
        }

        if (name == "exptme1" || name == "exptme2")
        {
            // 与Sherlock 4.14一致：PLC值为3000 us基准曝光的百分比。
            const double exposureUs = 3000.0 * values->front() / 100.0;
            const bool first = name == "exptme1";
            emit cameraExposureRequested(
                first ? CameraRole::Cam1 : CameraRole::Cam2, exposureUs);
        }
        emit configurationChanged(stage_);
        emit logMessage(QString("PLC setting %1 accepted (%2 parameter(s))").arg(name).arg(expectedCount));
        send(name.toLatin1() + "=ok");
        return true;
    }

    void PlcSession::applyConfigOverrides(MeasurementConfig &config_) const
    {
        const auto threshold = [this](const QString &name) -> std::optional<double>
        {
            const auto it = settings_.parameters.constFind(name);
            if (it == settings_.parameters.cend() || it.value().size() != 1)
                return {};
            return it.value().front();
        };
        if (const auto value = threshold("dia_thr"))
            config_.neck.gradientThresholdCamera1 = std::clamp(*value * 255.0 / 100.0, 0.0, 255.0);
        if (const auto value = threshold("diathr2"))
            config_.neck.gradientThresholdCamera2 = std::clamp(*value * 255.0 / 100.0, 0.0, 255.0);
        if (const auto value = threshold("exptme1"); value && *value > 0.0)
            config_.camera.initialExposureCamera1 = 3000.0 * *value / 100.0;
        if (const auto value = threshold("exptme2"); value && *value > 0.0)
            config_.camera.initialExposureCamera2 = 3000.0 * *value / 100.0;
    }

    MeasurementStage PlcSession::restoreSettings(const QString &path)
    {
        statePath_ = path;
        PlcRuntimeState saved;
        QString error;
        if (!PlcRuntimeStore(statePath_).load(&saved, &error))
        {
            emit logMessage("PLC state ignored: " + error);
            return settings_.stage;
        }
        settings_.stage = saved.stage;
        settings_.pointFitSelected = saved.pointFitSelected;
        settings_.acquisitionEnabled = saved.acquisitionEnabled;
        settings_.relativeThreshold = saved.relativeThreshold;
        settings_.refreshRate = saved.refreshRate;
        settings_.parameters = std::move(saved.parameters);
        for (const QString name : {"dia_crd", "mlt_crd", "dip_crd"})
        {
            const auto it = settings_.parameters.constFind(name);
            if (it == settings_.parameters.cend())
                continue;
            if (!setPlcRoi(rois_, name, it.value(), &error))
            {
                settings_.parameters.remove(name);
                emit logMessage("Saved PLC ROI ignored: " + error);
            }
        }
        return settings_.stage;
    }

    bool PlcSession::saveStage(MeasurementStage stage, QString *error) const
    {
        auto snapshot = settings_;
        snapshot.stage = stage;
        return PlcRuntimeStore(statePath_).save(snapshot, error);
    }
    void PlcSession::replayExposures()
    {
        for (const auto &name : {"exptme1", "exptme2"}) {
            const auto it = settings_.parameters.constFind(name);
            if (it != settings_.parameters.cend() && it.value().size() == 1)
                emit cameraExposureRequested(QString::fromLatin1(name) == "exptme1" ? CameraRole::Cam1 : CameraRole::Cam2,
                                             3000.0 * it.value().front() / 100.0);
        }
    }
}
