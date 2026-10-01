#include "daily_log.hpp"
#include <QDir>
#include <QFile>
#include <utility>

namespace pva
{
    void DailyLog::setDirectory(QString directory)
    {
        if (directory_ == directory)
            return;
        directory_ = std::move(directory);
        lastDate_ = {};
        lastMessage_.clear();
        lastCount_ = 0;
        lastOffset_ = -1;
    }

    DailyLogResult DailyLog::append(const QDateTime &time, const QString &message)
    {
        DailyLogResult result;
        result.merged = lastDate_ == time.date() && lastMessage_ == message;
        const int count = result.merged ? lastCount_ + 1 : 1;
        result.line = time.toString("HH:mm:ss ") + message +
                      (count > 1 ? QString(" x %1").arg(count) : QString());
        if (!QDir().mkpath(directory_))
        {
            result.error = "Cannot create log directory: " + directory_;
            return result;
        }
        QFile file(QDir(directory_).filePath(time.date().toString("yyyy-MM-dd") + ".log"));
        if (!file.open(QIODevice::ReadWrite))
        {
            result.error = file.errorString();
            return result;
        }
        const qint64 offset = result.merged && lastOffset_ >= 0 &&
                                      lastOffset_ <= file.size()
                                  ? lastOffset_ : file.size();
        const QByteArray bytes = result.line.toUtf8() + '\n';
        if (!file.resize(offset) || !file.seek(offset) ||
            file.write(bytes) != bytes.size() || !file.flush())
        {
            result.error = file.errorString();
            return result;
        }
        result.written = true;
        lastDate_ = time.date();
        lastMessage_ = message;
        lastCount_ = count;
        lastOffset_ = offset;
        return result;
    }
}
