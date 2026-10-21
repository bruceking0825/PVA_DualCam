#include "image_storage.hpp"
#include "overlay_render.hpp"
#include <QDir>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QRunnable>
#include <QSaveFile>
#include <QPainter>
#include <QLockFile>
#include <QMutexLocker>
#include <QThread>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <algorithm>

namespace pva
{
    namespace
    {
        cv::Mat readImage(const QString &path)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return {};
            const auto bytes = file.readAll();
            if (bytes.isEmpty())
                return {};
            return cv::imdecode(cv::Mat(1, bytes.size(), CV_8U, const_cast<char *>(bytes.constData())), cv::IMREAD_UNCHANGED);
        }
        cv::Mat writeImage(const QString &directory, const QString &path, const cv::Mat &image)
        {
            if (image.empty())
                throw std::runtime_error("Camera 2 image is empty");
            cv::Mat gray;
            if (image.channels() == 1)
                gray = image;
            else if (image.channels() == 3 || image.channels() == 4)
                cv::cvtColor(image, gray, image.channels() == 3 ? cv::COLOR_BGR2GRAY : cv::COLOR_BGRA2GRAY);
            else
                throw std::runtime_error("Unsupported Camera 2 image format");
            if (gray.depth() != CV_8U)
            {
                cv::Mat converted;
                gray.convertTo(converted, CV_8U, gray.depth() == CV_16U ? 1.0 / 256.0 : 1.0);
                gray = converted;
            }
            std::vector<uchar> encoded;
            if (!cv::imencode(".bmp", gray, encoded) || encoded.empty())
                throw std::runtime_error("BMP encoding failed");
            if (!QDir().mkpath(directory))
                throw std::runtime_error("Cannot create Facette directory");
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char *>(encoded.data()), qint64(encoded.size())) != qint64(encoded.size()) || !file.commit())
                throw std::runtime_error(file.errorString().toStdString());
            return gray;
        }

        bool saveMeasurementImage(const QString &path, const QImage &image)
        {
            constexpr int attempts = 20;
            constexpr unsigned long retryDelayMs = 50;
            for (int attempt = 0; attempt < attempts; ++attempt)
            {
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly))
                    throw std::runtime_error(file.errorString().toStdString());
                if (!image.save(&file, "BMP"))
                    throw std::runtime_error("BMP encoding/write failed");
                if (file.commit())
                    return true;

                // Windows 读取方未开放删除共享时，原子替换会暂时失败；短时重试后静默保留旧图。
                if (file.error() != QFileDevice::RenameError)
                    throw std::runtime_error(file.errorString().toStdString());
                if (attempt + 1 < attempts)
                    QThread::msleep(retryDelayMs);
            }
            return false;
        }
    }
    ImageStorage::ImageStorage(QObject *parent) : QObject(parent)
    {
        pool_.setMaxThreadCount(1);
        measurementPool_.setMaxThreadCount(1);
    }
    ImageStorage::~ImageStorage()
    {
        measurementPool_.waitForDone();
        pool_.waitForDone();
    }
    void ImageStorage::flush()
    {
        // 测量线程自行接续最新任务，收尾不依赖所属线程处理完成通知。
        measurementPool_.waitForDone();
        pool_.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
    void ImageStorage::load(const QString &directory, int index) { submit(Operation::Load, directory, index); }
    void ImageStorage::save(const QString &directory, int index, const cv::Mat &image) { submit(Operation::Save, directory, index, image); }
    void ImageStorage::saveMeasurement(const MeasurementResult &result, const MeasurementConfig &config,
                                       const MeasurementRois &rois)
    {
        auto snapshot = result;
        if (!snapshot.preview1.empty() && !snapshot.preview1.u)
            snapshot.preview1 = snapshot.preview1.clone();
        if (!snapshot.preview2.empty() && !snapshot.preview2.u)
            snapshot.preview2 = snapshot.preview2.clone();
        MeasurementSave task{std::move(snapshot), config, rois};
        QMutexLocker lock(&measurementMutex_);
        if (measurementRunning_) {
            // 同名 BMP 只需要最新结果；覆盖等待项，不挤占 Facette 的队列。
            latestMeasurement_ = std::move(task);
            return;
        }
        measurementRunning_ = true;
        measurementPool_.start(QRunnable::create([this, task = std::move(task)]() mutable {
            writeMeasurements(std::move(task));
        }));
    }
    void ImageStorage::writeMeasurements(MeasurementSave task)
    {
        for (;;) {
            const auto &snapshot = task.result;
            const auto &directory = task.config.runtime.measurementImageDir;
            QStringList errors;
            try {
                auto overlay1 = snapshot.overlay1, overlay2 = snapshot.overlay2;
                appendDisplayRois(task.config, task.rois, snapshot.stage,
                                 snapshot.preview1.size(), snapshot.preview2.size(), overlay1, overlay2);
                for (int camera = 1; camera <= 2; ++camera) {
                    const auto path = QDir(directory).filePath(QString("image%1.bmp").arg(camera));
                    try {
                        const auto image = renderOverlayImage(camera == 1 ? snapshot.preview1 : snapshot.preview2,
                            camera == 1 ? overlay1 : overlay2, &snapshot.regionStatistics[camera - 1]);
                        if (image.isNull()) throw std::runtime_error("Measurement image is empty");
                        if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create measurement image directory");
                        saveMeasurementImage(path, image);
                    } catch (const std::exception &exception) {
                        errors.append(path + ": " + QString::fromUtf8(exception.what()));
                    }
                }
            } catch (const std::exception &exception) {
                errors.append(directory + ": " + QString::fromUtf8(exception.what()));
            }
            if (!errors.isEmpty()) {
                QMetaObject::invokeMethod(this, [this, errors] {
                    for (const auto &error : errors) emit failed(error);
                }, Qt::QueuedConnection);
            }
            QMutexLocker lock(&measurementMutex_);
            if (!latestMeasurement_) {
                measurementRunning_ = false;
                return;
            }
            task = std::move(*latestMeasurement_);
            latestMeasurement_.reset();
        }
    }
    void ImageStorage::saveContinuous(const MeasurementResult &result, const MeasurementConfig &config,
                                      const MeasurementRois &rois, const QDateTime &time)
    {
        const QString directory = config.runtime.continuousSaveDir;
        const QString path = QDir(directory).filePath(time.toString("yyyyMMdd_HHmmss") + ".png");
        if (pending_ >= 16)
        {
            emit failed(path + ": continuous storage rejected: queue full");
            return;
        }
        ++pending_;
        auto snapshot = result;
        if (!snapshot.preview1.empty() && !snapshot.preview1.u)
            snapshot.preview1 = snapshot.preview1.clone();
        if (!snapshot.preview2.empty() && !snapshot.preview2.u)
            snapshot.preview2 = snapshot.preview2.clone();
        pool_.start(QRunnable::create([this, snapshot, config, rois, directory, path]
                                      {
        QString error;
        try {
            if (snapshot.preview1.empty() || snapshot.preview2.empty())
                throw std::runtime_error("Continuous image skipped: one or both camera images are empty");
            if (QFile::exists(path)) throw std::runtime_error("Continuous image skipped: file already exists");
            std::vector<OverlayElement> overlay1, overlay2;
            if (config.runtime.continuousSaveOverlay) {
                overlay1 = snapshot.overlay1; overlay2 = snapshot.overlay2;
                appendDisplayRois(config, rois, snapshot.stage, snapshot.preview1.size(), snapshot.preview2.size(), overlay1, overlay2);
            }
            const auto first = renderOverlayImage(snapshot.preview1, overlay1,
                config.runtime.continuousSaveOverlay ? &snapshot.regionStatistics[0] : nullptr);
            const auto second = renderOverlayImage(snapshot.preview2, overlay2,
                config.runtime.continuousSaveOverlay ? &snapshot.regionStatistics[1] : nullptr);
            QImage combined(std::max(first.width(), second.width()), first.height() + second.height(), QImage::Format_RGB888);
            if (combined.isNull()) throw std::runtime_error("Cannot allocate continuous image");
            combined.fill(Qt::black);
            QPainter painter(&combined);
            painter.drawImage(0, 0, first);
            painter.drawImage(0, first.height(), second);
            painter.end();
            if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create continuous image directory");
            QLockFile lock(path + ".lock");
            if (!lock.tryLock(0)) throw std::runtime_error("Continuous image skipped: file is being saved");
            if (QFile::exists(path)) throw std::runtime_error("Continuous image skipped: file already exists");
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
            if (!combined.save(&file, "PNG")) throw std::runtime_error("PNG encoding/write failed");
            if (QFile::exists(path)) throw std::runtime_error("Continuous image skipped: file already exists");
            if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
        } catch (const std::exception &exception) { error = path + ": " + QString::fromUtf8(exception.what()); }
        QMetaObject::invokeMethod(this, [this, error] {
            --pending_;
            if (!error.isEmpty()) emit failed(error);
        }, Qt::QueuedConnection); }));
    }
    void ImageStorage::captureOffline(const QString &directory, int index, const QString &sourcePath)
    {
        submit(Operation::Offline, directory, index, {}, sourcePath);
    }
    void ImageStorage::submit(Operation operation, QString directory, int index, cv::Mat image, QString source)
    {
        if (pending_ >= 16)
        {
            emit failed(QString("Facette%1 storage rejected: queue full").arg(index));
            return;
        }
        ++pending_;
        pool_.start(QRunnable::create([this, operation, directory, index, image, source]
                                      {
        cv::Mat result = image;
        QString error;
        const auto path = QDir(directory).filePath(QString("Facette%1.bmp").arg(index));
        try {
            if (operation == Operation::Load) result = readImage(path);
            else {
                if (operation == Operation::Offline) {
                    const auto composite = readImage(source);
                    if (composite.empty() || composite.rows < 2 || composite.rows % 2)
                        throw std::runtime_error("Invalid offline stereo image");
                    result = composite.rowRange(composite.rows / 2, composite.rows);
                }
                result = writeImage(directory, path, result);
            }
        } catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
        QMetaObject::invokeMethod(this, [this, operation, directory, index, path, result, error] {
            --pending_;
            if (!error.isEmpty()) { emit failed(path + ": " + error); return; }
            emit ready(directory, index, result);
            if (operation != Operation::Load) emit saved(path);
        }, Qt::QueuedConnection); }));
    }
}
