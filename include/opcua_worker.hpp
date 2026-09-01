#pragma once

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <optional>

namespace pva
{
    class OpcUaWorker final : public QThread
    {
        Q_OBJECT
    public:
        explicit OpcUaWorker(QObject *parent = nullptr);
        ~OpcUaWorker() override;

        void stop();
        void queueDiameter(double diameterMm);

    signals:
        void connectionChanged(bool connected);
        void controlsChanged(int stageValue, bool shoulderTransition);
        void failed(const QString &message);

    protected:
        void run() override;

    private:
        bool waitForStop(unsigned long milliseconds);
        std::atomic_bool stopping_{false};
        QMutex mutex_;
        QMutex waitMutex_;
        QWaitCondition stopCondition_;
        std::optional<double> pendingDiameter_;
        int comValue_{-1};
    };
}
