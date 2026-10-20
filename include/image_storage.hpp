#pragma once
#include <QObject>
#include <QDateTime>
#include <QThreadPool>
#include <QMutex>
#include <optional>
#include <opencv2/core.hpp>
#include "config.hpp"
#include "measurement_geometry.hpp"

namespace pva {
// Facette/归档保留已接收请求；测量覆盖图独立串行写入，只保留最新待保存结果。
class ImageStorage final : public QObject {
    Q_OBJECT
public:
    explicit ImageStorage(QObject *parent = nullptr);
    ~ImageStorage() override;
    void flush();
    void load(const QString &directory, int index);
    void save(const QString &directory, int index, const cv::Mat &image);
    void saveMeasurement(const MeasurementResult &result, const MeasurementConfig &config,
                         const MeasurementRois &rois);
    void saveContinuous(const MeasurementResult &result, const MeasurementConfig &config,
                        const MeasurementRois &rois, const QDateTime &time);
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
    struct MeasurementSave {
        MeasurementResult result;
        MeasurementConfig config;
        MeasurementRois rois;
    };
    void writeMeasurements(MeasurementSave task);
    QThreadPool measurementPool_;
    QMutex measurementMutex_;
    bool measurementRunning_{};
    std::optional<MeasurementSave> latestMeasurement_;
};
}
