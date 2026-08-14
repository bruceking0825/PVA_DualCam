#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>

class SapXferCallbackInfo;

namespace pva
{
    class DalsaCamera final : public QObject
    {
        Q_OBJECT
    public:
        explicit DalsaCamera(QString role, QObject *parent = nullptr);
        ~DalsaCamera() override;

        static bool initialize(QString *error = nullptr);
        static void shutdown();
        static QStringList enumerate(QString *error = nullptr);

        [[nodiscard]] QString userId() const { return role_; }
        [[nodiscard]] bool isOpen() const { return open_; }
        [[nodiscard]] bool isStreaming() const { return streaming_; }
        bool open(QString *error = nullptr);
        void close();
        bool startStream(QString *error = nullptr);
        void stopStream();
        bool softwareTrigger(QString *error = nullptr);
        void frameConsumed();

        bool setTriggerMode(bool enabled, QString *error = nullptr);
        bool setTriggerSource(qint64 value, QString *error = nullptr);
        bool setTriggerEdge(qint64 value, QString *error = nullptr);
        bool setExposure(double value, QString *error = nullptr);
        bool setGain(double value, QString *error = nullptr);
        bool setWidth(qint64 value, QString *error = nullptr);
        bool setHeight(qint64 value, QString *error = nullptr);
        bool setOffsetX(qint64 value, QString *error = nullptr);
        bool setOffsetY(qint64 value, QString *error = nullptr);
        [[nodiscard]] qint64 triggerMode() const;
        [[nodiscard]] qint64 triggerSource() const;
        [[nodiscard]] qint64 triggerEdge() const;
        [[nodiscard]] double exposure() const;
        [[nodiscard]] double gain() const;
        [[nodiscard]] qint64 width() const;
        [[nodiscard]] qint64 height() const;
        [[nodiscard]] qint64 offsetX() const;
        [[nodiscard]] qint64 offsetY() const;

    signals:
        void frameReady(const QString &role, const cv::Mat &frame, qint64 monotonicNs);
        void captureFailed(const QString &role, const QString &message);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        QString role_;
        bool open_{false};
        bool streaming_{false};
        std::atomic_bool framePending_{false};

        bool setEnum(const char *feature, const char *value, QString *error);
        bool setDouble(const char *feature, double value, QString *error);
        bool setInteger(const char *feature, qint64 value, QString *error);
        [[nodiscard]] QString getEnum(const char *feature) const;
        [[nodiscard]] double getDouble(const char *feature) const;
        [[nodiscard]] qint64 getInteger(const char *feature) const;
        static void captureCallback(SapXferCallbackInfo *info);
        void handleFrame(bool trash);
    };
}
