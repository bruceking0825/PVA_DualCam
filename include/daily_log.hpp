#pragma once

#include <QDateTime>
#include <QString>
#include <deque>

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
        DailyLogResult append(const QDateTime &time, const QString &message, int repetitions = 1);
        DailyLogResult enqueue(const QDateTime &time, const QString &message);
        bool flush(QString *error = nullptr);
        bool pending() const { return !pending_.empty(); }
    private:
        struct Record { QString directory; QDateTime time; QString message; int count; };
        std::deque<Record> pending_;
        QString queuedDirectory_, queuedMessage_;
        QDate queuedDate_;
        int queuedCount_{0};
        QString directory_;
        QDate lastDate_;
        QString lastMessage_;
        int lastCount_{0};
        qint64 lastOffset_{-1};
    };
}
