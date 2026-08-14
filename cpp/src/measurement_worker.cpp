#include "pva/measurement_worker.hpp"
#include "pva/state_store.hpp"
#include <QMutexLocker>

namespace pva
{
    MeasurementWorker::MeasurementWorker(MeasurementEngine engine, QString statePath, QObject *parent)
        : QThread(parent), engine_(std::move(engine)), statePath_(std::move(statePath)) {}
    MeasurementWorker::~MeasurementWorker()
    {
        stop();
        wait();
    }
    void MeasurementWorker::submit(cv::Mat a, cv::Mat b, MeasurementStage stage)
    {
        QMutexLocker lock(&mutex_);
        pending_ = Pending{a.clone(), b.clone(), stage};
        condition_.wakeOne();
    }
    void MeasurementWorker::stop()
    {
        QMutexLocker lock(&mutex_);
        stopping_ = true;
        condition_.wakeOne();
    }
    void MeasurementWorker::run()
    {
        while (true)
        {
            std::optional<Pending> job;
            {
                QMutexLocker lock(&mutex_);
                while (!stopping_ && !pending_)
                    condition_.wait(&mutex_);
                if (stopping_)
                    return;
                job = std::move(pending_);
                pending_.reset();
            }
            try
            {
                auto result = engine_.process(job->camera1, job->camera2, job->stage);
                if (result.valid && !statePath_.isEmpty())
                {
                    QString error;
                    if (!StateStore(statePath_).save(engine_.state(), &error))
                        emit failed("State save failed: " + error);
                }
                emit resultReady(result);
            }
            catch (const std::exception &e)
            {
                emit failed(QString::fromUtf8(e.what()));
            }
        }
    }
}
