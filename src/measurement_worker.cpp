#include "measurement_worker.hpp"
#include "state_store.hpp"
#include <QMutexLocker>
#include <QElapsedTimer>
#include "offline_image_source.hpp"

namespace pva
{
    MeasurementWorker::MeasurementWorker(MeasurementEngine engine, QString statePath, QObject *parent)
        : QThread(parent), engine_(std::move(engine)), statePath_(std::move(statePath)) {}
    MeasurementWorker::~MeasurementWorker()
    {
        stop();
        wait();
    }
    void MeasurementWorker::submit(cv::Mat a, cv::Mat b, MeasurementStage stage, quint64 generation, MeasurementTaskInfo task)
    {
        // SDK 边界已持有图像内存；任务按只读方式引用，锁内仅交换句柄。
        QMutexLocker lock(&mutex_);
        if (stopping_) return;
        generation_ = generation;
        pending_ = Pending{std::move(a), std::move(b), stage, generation, {}, task};
        condition_.wakeOne();
    }
    void MeasurementWorker::submitOffline(const QString &path, MeasurementStage stage, quint64 generation, MeasurementTaskInfo task)
    {
        QMutexLocker lock(&mutex_);
        if (stopping_) return;
        generation_ = generation;
        pending_ = Pending{{}, {}, stage, generation, path, task};
        condition_.wakeOne();
    }
    void MeasurementWorker::updateConfig(MeasurementConfig config)
    {
        QMutexLocker lock(&mutex_);
        pendingConfig_ = std::move(config);
    }
    void MeasurementWorker::updateMeasurementRois(MeasurementRois rois)
    {
        QMutexLocker lock(&mutex_);
        pendingRois_ = std::move(rois);
    }
    void MeasurementWorker::invalidate(quint64 generation)
    {
        QMutexLocker lock(&mutex_);
        generation_ = generation;
        pending_.reset();
    }
    void MeasurementWorker::stop()
    {
        QMutexLocker lock(&mutex_);
        stopping_ = true;
        condition_.wakeOne();
    }
    void MeasurementWorker::run()
    {
        OfflineImageSource offline;
        std::optional<MeasurementState> pendingState;
        QElapsedTimer saveClock;
        const auto flushState = [&] {
            if (!pendingState) return;
            QString error;
            if (StateStore(statePath_).save(*pendingState, &error))
                pendingState.reset();
            else
                emit persistenceFailed("State save failed: " + error);
            saveClock.restart();
        };
        while (true)
        {
            if (pendingState && saveClock.elapsed() >= 3000)
                flushState();
            std::optional<Pending> job;
            std::optional<MeasurementConfig> updatedConfig;
            std::optional<MeasurementRois> updatedRois;
            {
                QMutexLocker lock(&mutex_);
                while (!stopping_ && !pending_) {
                    if (pendingState) {
                        const auto remaining = 3000 - saveClock.elapsed();
                        if (remaining <= 0) break;
                        condition_.wait(&mutex_, static_cast<unsigned long>(remaining));
                    } else
                        condition_.wait(&mutex_);
                }
                if (stopping_)
                {
                    lock.unlock();
                    flushState();
                    return;
                }
                if (!pending_) continue;
                job = std::move(pending_);
                pending_.reset();
                updatedConfig = std::move(pendingConfig_);
                pendingConfig_.reset();
                updatedRois = std::move(pendingRois_);
                pendingRois_.reset();
            }
            try
            {
                if (updatedConfig)
                    engine_.setConfig(std::move(*updatedConfig));
                if (updatedRois)
                    engine_.setMeasurementRois(std::move(*updatedRois));
                if (!job->path.isEmpty()) {
                    const auto images = offline.load(job->path);
                    job->camera1 = images.first;
                    job->camera2 = images.second;
                }
                auto candidate = engine_;
                auto result = candidate.process(job->camera1, job->camera2, job->stage);
                result.generation = job->generation;
                result.task = job->task;
                {
                    QMutexLocker lock(&mutex_);
                    if (stopping_ || job->generation != generation_) continue;
                    engine_ = std::move(candidate);
                }
                // PLC 结果先交付，磁盘状态写入不延迟协议回复。
                emit resultReady(result);
                if (result.valid && !statePath_.isEmpty())
                {
                    if (!pendingState) saveClock.start();
                    pendingState = engine_.state();
                }
            }
            catch (const std::exception &e)
            {
                emit failed(job->generation, QString::fromUtf8(e.what()));
            }
        }
    }
}
