#include "image_storage.hpp"
#include "overlay_render.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QImage>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
QImage expected(const pva::MeasurementResult &result, const pva::MeasurementConfig &config,
                const pva::MeasurementRois &rois, int camera) {
    auto overlay1 = result.overlay1, overlay2 = result.overlay2;
    pva::appendDisplayRois(config, rois, result.stage, result.preview1.size(), result.preview2.size(), overlay1, overlay2);
    return pva::renderOverlayImage(camera == 1 ? result.preview1 : result.preview2,
                                  camera == 1 ? overlay1 : overlay2).convertToFormat(QImage::Format_RGB32);
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    pva::MeasurementConfig config;
    config.runtime.measurementImageDir = directory.filePath("images");
    config.measurement.autoExposureRoiCamera1 = {4, 5, 16, 20};
    config.measurement.autoExposureRoiCamera2 = {6, 7, 10, 12};
    pva::MeasurementRois rois;
    rois.melt = std::array<double, 6>{50, 35, 20, 16, 150, 0}; // cam2 falls back locally
    rois.diameter = std::array<double, 8>{70, 40, 0, 90, 10, 20, -10, 0};
    rois.dip = std::array<double, 3>{60, 30, 20};
    pva::MeasurementResult result;
    result.preview1 = cv::Mat(80, 100, CV_8U, cv::Scalar(17));
    result.preview2 = cv::Mat(80, 100, CV_8U, cv::Scalar(37));
    result.overlay1.push_back({pva::OverlayType::Line, {{10, 20}, {80, 20}}, {0, 0, 255}, 2});
    result.overlay2.push_back({pva::OverlayType::Cross, {{30, 30}}, {0, 255, 255}, 2});
    pva::ImageStorage storage;
    QStringList errors;
    QObject::connect(&storage, &pva::ImageStorage::failed, &app, [&](const QString &error) { errors.append(error); });
    const auto path1 = QDir(config.runtime.measurementImageDir).filePath("image1.bmp");
    const auto path2 = QDir(config.runtime.measurementImageDir).filePath("image2.bmp");
    for (const auto stage : {pva::MeasurementStage::Melt, pva::MeasurementStage::Dip, pva::MeasurementStage::Neck,
                             pva::MeasurementStage::Crown, pva::MeasurementStage::Body}) {
        result.stage = stage;
        result.valid = stage != pva::MeasurementStage::Body;
        storage.saveMeasurement(result, config, rois);
        storage.flush();
        check(QImage(path1).convertToFormat(QImage::Format_RGB32) == expected(result, config, rois, 1),
              "Camera1 BMP matches complete native-size overlay, including failed result");
        check(QImage(path2).convertToFormat(QImage::Format_RGB32) == expected(result, config, rois, 2),
              "Camera2 BMP matches complete native-size overlay");
    }
    check(errors.isEmpty(), "All five stages save successfully");
    result.stage = pva::MeasurementStage::Melt;
    const auto color1 = expected(result, config, rois, 1).pixelColor(40, 35);
    const auto color2 = expected(result, config, rois, 2).pixelColor(6, 10);
    check(color1.green() > color1.red() && color1.green() > color1.blue(), "PLC exposure ROI exports green");
    check(color2.blue() > color2.red() && color2.blue() > color2.green(), "Local exposure ROI exports blue");
    storage.saveMeasurement(result, config, rois);
    config.measurement.autoExposureRoiCamera1 = {1, 1, 2, 2};
    rois.melt.reset();
    storage.flush();
    check(QImage(path1).pixelColor(40, 35).green() > 100, "Queued export retains original ROI snapshot");
    const auto previous1 = QImage(path1);
    result.preview1.release();
    result.preview2 = cv::Mat(80, 100, CV_8U, cv::Scalar(87));
    storage.saveMeasurement(result, config, rois);
    storage.flush();
    check(QImage(path1) == previous1 && QImage(path2).pixelColor(90, 70).red() == 87,
          "Empty camera1 preserves old BMP while camera2 updates");
    check(errors.size() == 1 && errors.back().contains(path1), "Empty image error includes full path");
    result.preview1 = cv::Mat(80, 100, CV_8U, cv::Scalar(67));
    const QString oldDirectory = config.runtime.measurementImageDir;
    storage.saveMeasurement(result, config, rois);
    config.runtime.measurementImageDir = directory.filePath("new_images");
    storage.saveMeasurement(result, config, rois);
    storage.flush();
    check(QFile::exists(QDir(oldDirectory).filePath("image1.bmp")) &&
          QFile::exists(QDir(config.runtime.measurementImageDir).filePath("image1.bmp")),
          "Directory changes only affect subsequent exports");
    const auto blockedPath = directory.filePath("not_a_directory");
    QFile blocked(blockedPath);
    check(blocked.open(QIODevice::WriteOnly), "Create invalid output directory fixture");
    blocked.close();
    config.runtime.measurementImageDir = blockedPath;
    storage.saveMeasurement(result, config, rois);
    storage.flush();
    check(errors.size() == 3 && errors.back().contains(blockedPath), "Directory creation failures include path");
    config.runtime.measurementImageDir = oldDirectory;
#ifdef Q_OS_WIN
    const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(path1.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "Lock measurement BMP against replacement");
    if (locked != INVALID_HANDLE_VALUE) {
        storage.saveMeasurement(result, config, rois);
        storage.flush();
        check(errors.size() == 4 && errors.back().contains(path1), "Locked output reports atomic replacement failure");
        CloseHandle(locked);
    }
#endif
    const int beforeQueueErrors = errors.size();
    for (int i = 0; i < 17; ++i) storage.saveMeasurement(result, config, rois);
    check(errors.size() == beforeQueueErrors + 1 && errors.back().contains("queue full"),
          "Full queue reports failure without blocking or replacing accepted work");
    storage.flush();
    const auto finalImage = QImage(path1);
    check(finalImage.size() == QSize(100, 80) && finalImage.pixelColor(90, 70).red() == 67,
          "Flush completes accepted exports at original dimensions");
    storage.save(directory.filePath("facettes"), 1, result.preview1);
    storage.flush();
    check(QImage(directory.filePath("facettes/Facette1.bmp")).pixelColor(90, 70).red() == 67,
          "Facette grayscale saving remains unchanged");
    pva::ImageStorage continuous;
    QStringList continuousErrors;
    QObject::connect(&continuous, &pva::ImageStorage::failed, &app,
                     [&](const QString &error) { continuousErrors.append(error); });
    config.runtime.continuousSaveDir = directory.filePath("continuous");
    config.runtime.continuousSaveOverlay = false;
    const QDateTime time(QDate(2026, 10, 12), QTime(15, 30, 45));
    result.preview1 = cv::Mat(40, 100, CV_8U, cv::Scalar(20));
    result.preview2 = cv::Mat(30, 80, CV_8U, cv::Scalar(90));
    const auto pngPath = QDir(config.runtime.continuousSaveDir).filePath("20261012_153045.png");
    continuous.saveContinuous(result, config, rois, time);
    config.runtime.continuousSaveOverlay = true; // queued task must retain false
    continuous.flush();
    const auto rawPng = QImage(pngPath);
    check(rawPng.size() == QSize(100, 70) && rawPng.pixelColor(20, 10) == QColor(20, 20, 20) &&
          rawPng.pixelColor(20, 50) == QColor(90, 90, 90) && rawPng.pixelColor(90, 50) == Qt::black,
          "Continuous PNG keeps camera1 above camera2, pads black and snapshots overlay setting");
    continuous.saveContinuous(result, config, rois, time);
    continuous.flush();
    check(QImage(pngPath) == rawPng && continuousErrors.size() == 1 && continuousErrors.back().contains("already exists"),
          "Existing timestamp PNG is not overwritten");
    continuous.saveContinuous(result, config, rois, time.addSecs(1));
    continuous.flush();
    const auto overlayPng = QImage(QDir(config.runtime.continuousSaveDir).filePath("20261012_153046.png"));
    check(overlayPng.pixelColor(40, 20).red() > overlayPng.pixelColor(40, 20).green(),
          "Overlay enabled includes colored measurement lines");
    result.preview2.release();
    continuous.saveContinuous(result, config, rois, time.addSecs(2));
    continuous.flush();
    check(continuousErrors.size() == 2 && !QFile::exists(QDir(config.runtime.continuousSaveDir).filePath("20261012_153047.png")),
          "Empty camera skips entire combined PNG");
    result.preview2 = cv::Mat(30, 80, CV_8U, cv::Scalar(90));
    config.runtime.continuousSaveDir = blockedPath;
    continuous.saveContinuous(result, config, rois, time.addSecs(3));
    continuous.flush();
    check(continuousErrors.size() == 3 && continuousErrors.back().contains(blockedPath),
          "Continuous directory failure includes full file path");
    config.runtime.continuousSaveDir = directory.filePath("continuous_new");
    const auto queuedDirectory = config.runtime.continuousSaveDir;
    continuous.saveContinuous(result, config, rois, time.addSecs(4));
    config.runtime.continuousSaveDir = directory.filePath("continuous_next");
    continuous.saveContinuous(result, config, rois, time.addSecs(5));
    continuous.flush();
    check(QFile::exists(QDir(queuedDirectory).filePath("20261012_153049.png")) &&
          QFile::exists(QDir(config.runtime.continuousSaveDir).filePath("20261012_153050.png")),
          "Continuous output path is fixed at queue submission");
    const int beforeContinuousQueue = continuousErrors.size();
    for (int i = 0; i < 17; ++i) continuous.saveContinuous(result, config, rois, time.addSecs(10 + i));
    check(continuousErrors.size() == beforeContinuousQueue + 1 && continuousErrors.back().contains("queue full"),
          "Continuous queue is bounded");
    const int beforeBmpErrors = errors.size();
    storage.saveMeasurement(result, config, rois);
    storage.flush();
    check(errors.size() == beforeBmpErrors, "Full continuous queue does not block BMP queue");
    continuous.flush();
    check(QDir(config.runtime.continuousSaveDir).entryList({"*.png"}, QDir::Files).size() == 17,
          "Shutdown flush completes all accepted PNG jobs");
    return failures ? 1 : 0;
}
