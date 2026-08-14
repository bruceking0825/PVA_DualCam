#pragma once
#include "pva/measurement_engine.hpp"
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
        void submit(cv::Mat camera1, cv::Mat camera2, MeasurementStage stage);
        void stop();
    signals:
        void resultReady(const pva::MeasurementResult &result);
        void failed(const QString &message);

    protected:
        void run() override;

    private:
        struct Pending
        {
            cv::Mat camera1, camera2;
            MeasurementStage stage;
        };
        MeasurementEngine engine_;
        QString statePath_;
        QMutex mutex_;
        QWaitCondition condition_;
        std::optional<Pending> pending_;
        bool stopping_{false};
    };
}
