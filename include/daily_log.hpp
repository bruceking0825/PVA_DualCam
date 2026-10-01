#pragma once

#include <QDateTime>
#include <QString>

namespace pva
{
    struct DailyLogResult
    {
        QString line;
        bool merged{false};
        bool written{false};
        QString error;
    };

    class DailyLog
    {
    public:
        void setDirectory(QString directory);
        DailyLogResult append(const QDateTime &time, const QString &message);
    private:
        QString directory_;
        QDate lastDate_;
        QString lastMessage_;
        int lastCount_{0};
        qint64 lastOffset_{-1};
    };
}
