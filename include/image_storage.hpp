#pragma once
#include <QObject>
#include <QThreadPool>
#include <opencv2/core.hpp>

namespace pva {
// 有界、串行图像存储。已接收的保存任务在退出时完成，不替换旧请求。
class ImageStorage final : public QObject {
    Q_OBJECT
public:
    explicit ImageStorage(QObject *parent = nullptr);
    ~ImageStorage() override;
    void flush();
    void load(const QString &directory, int index);
    void save(const QString &directory, int index, const cv::Mat &image);
    void captureOffline(const QString &directory, int index, const QString &sourcePath);
signals:
    void ready(const QString &directory, int index, const cv::Mat &image);
    void saved(const QString &path);
    void failed(const QString &message);
private:
    enum class Operation { Load, Save, Offline };
    void submit(Operation operation, QString directory, int index, cv::Mat image = {}, QString source = {});
    QThreadPool pool_;
    int pending_{};
};
}
