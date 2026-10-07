#pragma once
#include "camera_manager.hpp"
#include <QObject>
#include <opencv2/core.hpp>

Q_DECLARE_METATYPE(cv::Mat)

namespace pva
{
    class AppSignals final : public QObject
    {
        Q_OBJECT
    public:
        static AppSignals &instance();
    signals:
        void appClose();
        void status(const QString &device, const QString &state, const QString &type, const QString &message);

    };
}
