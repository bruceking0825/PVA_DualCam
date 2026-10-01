#include "page_home.hpp"
#include "sherlock_protocol.hpp"
#include "custom_graphics_view.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMetaObject>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    bool hasPicture(CustomGraphicsView *view)
    {
        if (!view)
            return false;
        for (auto *item : view->scene()->items())
            if (qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
                return true;
        return false;
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary test directory");
    if (!temporary.isValid())
        return 1;

    const QString inputDirectory = temporary.filePath("input");
    const QString outputDirectory = temporary.filePath("facettes");
    QDir().mkpath(inputDirectory);
    cv::Mat pair(8, 8, CV_8UC1, cv::Scalar(10));
    pair.rowRange(4, 8).setTo(40);
    const QString pairPath = QDir(inputDirectory).filePath("pair.bmp");
    check(cv::imwrite(QFile::encodeName(pairPath).constData(), pair),
          "Write simulated stereo image");

    auto config = pva::MeasurementConfig::loadIni(QString::fromLocal8Bit(argv[1]));
    config.runtime.offlineImageDir = inputDirectory;
    config.runtime.facetteImageDir = outputDirectory;
    config.runtime.stateFile = temporary.filePath("measurement_state.json");
    pva::PageHome page(config);

    auto *firstView = page.findChild<CustomGraphicsView *>("facetView1");
    auto *secondView = page.findChild<CustomGraphicsView *>("facetView2");
    check(firstView && secondView && !hasPicture(firstView) && !hasPicture(secondView),
          "Facette views begin with filename placeholders");

    pva::SherlockCommand command{"pic_fac1", {}, "pic_fac1"};
    check(QMetaObject::invokeMethod(&page, "onSherlockCommand", Qt::DirectConnection,
                                    Q_ARG(pva::SherlockCommand, command)),
          "Offline Facette command reaches page");
    const QString savedPath = QDir(outputDirectory).filePath("Facette1.bmp");
    cv::Mat saved = cv::imread(QFile::encodeName(savedPath).constData(), cv::IMREAD_GRAYSCALE);
    check(!saved.empty() && saved.rows == 4 && saved.cols == 8 &&
              saved.at<uchar>(0, 0) == 40 && hasPicture(firstView) && !hasPicture(secondView),
          "Offline Facette uses Camera 2 half and updates only its view");

    pair.rowRange(4, 8).setTo(70);
    cv::imwrite(QFile::encodeName(pairPath).constData(), pair);
    check(QMetaObject::invokeMethod(&page, "onSherlockCommand", Qt::DirectConnection,
                                    Q_ARG(pva::SherlockCommand, command)),
          "Repeat Facette command reaches page");
    saved = cv::imread(QFile::encodeName(savedPath).constData(), cv::IMREAD_GRAYSCALE);
    check(!saved.empty() && saved.at<uchar>(0, 0) == 70,
          "Repeated Facette command atomically overwrites BMP");

    config.runtime.facetteImageDir = temporary.filePath("other-facettes");
    page.reloadConfig(config);
    check(!hasPicture(firstView), "Changing Facette directory restores filename placeholder");
    return failures == 0 ? 0 : 1;
}
