#include "main_window.hpp"
#include "models.hpp"
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

namespace
{
    QPalette createApplicationPalette()
    {
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#282c34"));
        palette.setColor(QPalette::WindowText, QColor("#dddddd"));
        palette.setColor(QPalette::Base, QColor("#21252b"));
        palette.setColor(QPalette::AlternateBase, QColor("#282c34"));
        palette.setColor(QPalette::ToolTipBase, QColor("#21252b"));
        palette.setColor(QPalette::ToolTipText, QColor("#ffffff"));
        palette.setColor(QPalette::Text, QColor("#dddddd"));
        palette.setColor(QPalette::Button, QColor("#2c313a"));
        palette.setColor(QPalette::ButtonText, QColor("#dddddd"));
        palette.setColor(QPalette::BrightText, QColor("#ffffff"));
        palette.setColor(QPalette::Light, QColor("#46506e"));
        palette.setColor(QPalette::Midlight, QColor("#343b4f"));
        palette.setColor(QPalette::Mid, QColor("#282e3c"));
        palette.setColor(QPalette::Dark, QColor("#21252b"));
        palette.setColor(QPalette::Shadow, QColor("#181a1f"));
        palette.setColor(QPalette::Highlight, QColor("#bd93f9"));
        palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        palette.setColor(QPalette::Link, QColor("#61afef"));
        palette.setColor(QPalette::LinkVisited, QColor("#c678dd"));
        palette.setColor(QPalette::PlaceholderText, QColor("#718096"));

        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#687083"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#687083"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#687083"));
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#46506e"));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#a0a6b3"));
        return palette;
    }
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    // Qt 5 defaults to WindowsVista while Qt 6 defaults to ModernWindows.
    // Fusion plus an explicit palette gives both builds the same drawing base.
    application.setStyle(QStyleFactory::create("Fusion"));
    application.setPalette(createApplicationPalette());
    QApplication::setApplicationName("PVA DualCam C++");
    qRegisterMetaType<pva::MeasurementResult>("pva::MeasurementResult");
    qRegisterMetaType<cv::Mat>("cv::Mat");
    application.setWindowIcon(QIcon(":/images/images/images/zhonghuan.png"));
    pva::MainWindow window;
    window.show();
    return application.exec();
}
