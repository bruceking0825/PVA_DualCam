#pragma once

#include <QObject>
#include <QStringList>
#include <opencv2/core.hpp>
#include <map>
#include <memory>

namespace pva
{
    class DalsaCamera;

    class CameraManager final : public QObject
    {
        Q_OBJECT
    public:
        explicit CameraManager(QObject *parent = nullptr);
        ~CameraManager() override;

        bool initialize(QString *error = nullptr);
        void reset(const QStringList &userIds);
        DalsaCamera *getByUserId(const QString &userId) const;
        QList<DalsaCamera *> getAll() const;
        qsizetype size() const { return cameras_.size(); }
        void closeAll();

    signals:
        void frameReady(const QString &userId, const cv::Mat &frame, qint64 timestampNs);
        void captureFailed(const QString &userId, const QString &message);

    private:
        std::map<QString, std::unique_ptr<DalsaCamera>> cameras_;
        bool initialized_ = false;
    };
}
