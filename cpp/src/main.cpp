#include "pva/main_window.hpp"
#include "pva/models.hpp"
#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("PVA DualCam C++");
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    qRegisterMetaType<cv::Mat>("cv::Mat");
    application.setWindowIcon(QIcon(":/images/images/images/zhonghuan.png"));
    pva::MainWindow window;
    window.show();
    return application.exec();
}
