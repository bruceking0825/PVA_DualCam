#pragma once
#include "measurement_engine.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

namespace pva
{
    class MeasurementWorker final : public QThread
    {
        Q_OBJECT
    public:
        explicit MeasurementWorker(MeasurementEngine engine, QString statePath = {}, QObject *parent = nullptr);
        ~MeasurementWorker() override;
        void submit(cv::Mat camera1, cv::Mat camera2, MeasurementStage stage, quint64 generation = 0, MeasurementTaskInfo task = {});
        void submitOffline(const QString &path, MeasurementStage stage, quint64 generation, MeasurementTaskInfo task = {});
        void updateConfig(MeasurementConfig config);
        void updateMeasurementRois(MeasurementRois rois);
        void stop();
        void invalidate(quint64 generation);
    signals:
        void resultReady(const pva::MeasurementResult &result);
        void failed(quint64 generation, const QString &message);
        void persistenceFailed(const QString &message);

    protected:
        void run() override;

    private:
        struct Pending
        {
            cv::Mat camera1, camera2;
            MeasurementStage stage;
            quint64 generation{};
            QString path;
            MeasurementTaskInfo task;
        };
        MeasurementEngine engine_;
        QString statePath_;
        QMutex mutex_;
        QWaitCondition condition_;
        std::optional<Pending> pending_;
        std::optional<MeasurementConfig> pendingConfig_;
        std::optional<MeasurementRois> pendingRois_;
        bool stopping_{false};
        quint64 generation_{};
    };
}
