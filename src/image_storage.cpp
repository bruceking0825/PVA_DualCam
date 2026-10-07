#include "image_storage.hpp"
#include <QDir>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QRunnable>
#include <QSaveFile>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace pva {
namespace {
cv::Mat readImage(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.readAll();
    if (bytes.isEmpty()) return {};
    return cv::imdecode(cv::Mat(1, bytes.size(), CV_8U, const_cast<char *>(bytes.constData())), cv::IMREAD_UNCHANGED);
}
cv::Mat writeImage(const QString &directory, const QString &path, const cv::Mat &image)
{
    if (image.empty()) throw std::runtime_error("Camera 2 image is empty");
    cv::Mat gray;
    if (image.channels() == 1) gray = image;
    else if (image.channels() == 3 || image.channels() == 4)
        cv::cvtColor(image, gray, image.channels() == 3 ? cv::COLOR_BGR2GRAY : cv::COLOR_BGRA2GRAY);
    else throw std::runtime_error("Unsupported Camera 2 image format");
    if (gray.depth() != CV_8U) {
        cv::Mat converted;
        gray.convertTo(converted, CV_8U, gray.depth() == CV_16U ? 1.0 / 256.0 : 1.0);
        gray = converted;
    }
    std::vector<uchar> encoded;
    if (!cv::imencode(".bmp", gray, encoded) || encoded.empty()) throw std::runtime_error("BMP encoding failed");
    if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create Facette directory");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char *>(encoded.data()),
        qint64(encoded.size())) != qint64(encoded.size()) || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
    return gray;
}
}
ImageStorage::ImageStorage(QObject *parent) : QObject(parent) { pool_.setMaxThreadCount(1); }
ImageStorage::~ImageStorage() { pool_.waitForDone(); }
void ImageStorage::flush() { pool_.waitForDone(); QCoreApplication::sendPostedEvents(this, QEvent::MetaCall); }
void ImageStorage::load(const QString &directory, int index) { submit(Operation::Load, directory, index); }
void ImageStorage::save(const QString &directory, int index, const cv::Mat &image) { submit(Operation::Save, directory, index, image); }
void ImageStorage::captureOffline(const QString &directory, int index, const QString &sourcePath) {
    submit(Operation::Offline, directory, index, {}, sourcePath);
}
void ImageStorage::submit(Operation operation, QString directory, int index, cv::Mat image, QString source)
{
    if (pending_ >= 16) { emit failed(QString("Facette%1 storage rejected: queue full").arg(index)); return; }
    ++pending_;
    pool_.start(QRunnable::create([this, operation, directory, index, image, source] {
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
        }, Qt::QueuedConnection);
    }));
}
}
