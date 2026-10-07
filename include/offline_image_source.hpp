#pragma once
#include <QString>
#include <QDateTime>
#include <opencv2/core.hpp>
#include <utility>
namespace pva {
// 工作线程内使用；最多缓存当前一张合成图，兼顾 x86 内存限制。
class OfflineImageSource {
public:
    std::pair<cv::Mat, cv::Mat> load(const QString &path);
private:
    QString path_;
    qint64 size_{-1};
    QDateTime modified_;
    cv::Mat composite_;
};
}
