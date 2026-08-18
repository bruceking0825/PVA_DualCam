#include "pva/main_window.hpp"
#include "pva/app_signals.hpp"
#include "pva/config_manager.hpp"
#include "pva/page_camera.hpp"
#include "pva/page_home.hpp"
#include "pva/page_parameters.hpp"
#include "ui_main.h"
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QPushButton>
#include <QSizeGrip>

namespace pva
{
    namespace
    {
        QString findProjectFile(const QString &relativePath)
        {
            for (QDir directory : {QDir::current(), QDir(QCoreApplication::applicationDirPath())})
            {
                for (int level = 0; level < 6; ++level)
                {
                    const QString candidate = directory.absoluteFilePath(relativePath);
                    if (QFileInfo::exists(candidate))
                        return QFileInfo(candidate).absoluteFilePath();
                    if (!directory.cdUp())
                        break;
                }
            }
            return {};
        }
    }

    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>())
    {
        ui_->setupUi(this);
        // 原 Python 版本使用自绘标题栏；透明无边框可消除系统窗口的白色外围框。
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle("Stereo Vision Measurement");
        ui_->titleRightInfo->setText("Stereo Vision Measurement");
        ui_->btn_home->setText("Home");
        ui_->btn_statistics->hide();
        ui_->btn_IO->hide();
        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(17);
        shadow->setOffset(0, 0);
        shadow->setColor(QColor(0, 0, 0, 150));
        ui_->bgApp->setGraphicsEffect(shadow);
        // 与 Python 版本一致，在无边框窗口右下角提供系统窗口缩放手柄。
        auto *sizeGrip = new QSizeGrip(ui_->frame_size_grip);
        sizeGrip->setFixedSize(20, 20);
        sizeGrip->setStyleSheet("margin: 0px; padding: 0px;");
        configPath_ = findProjectFile("src/cnf.ini");
        if (configPath_.isEmpty())
            throw std::runtime_error("Cannot locate src/cnf.ini");
        auto &configManager = ConfigManager::instance();
        configManager.load(configPath_, false);
        home_ = new PageHome(configManager.config(), ui_->stackedWidget);
        camera_ = new PageCamera(configManager.config(), ui_->stackedWidget);
        parameters_ = new PageParameters(configPath_, ui_->stackedWidget);
        ui_->stackedWidget->addWidget(home_);
        ui_->stackedWidget->addWidget(parameters_);
        ui_->stackedWidget->addWidget(camera_);
        ui_->stackedWidget->setCurrentWidget(home_);
        connect(ui_->btn_home, &QPushButton::clicked, this, &MainWindow::switchPage);
        connect(ui_->btn_parameters, &QPushButton::clicked, this, &MainWindow::switchPage);
        connect(ui_->btn_camera, &QPushButton::clicked, this, &MainWindow::switchPage);
        connect(ui_->minimizeAppBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
        connect(ui_->maximizeRestoreAppBtn, &QPushButton::clicked, this, [this]
                { toggleMaximizeRestore(); });
        connect(ui_->closeAppBtn, &QPushButton::clicked, this, &QWidget::close);
        connect(ui_->toggleButton, &QPushButton::clicked, this, &MainWindow::toggleMenu);
        connect(ui_->toggleLeftBox, &QPushButton::clicked, this, &MainWindow::toggleLeftBox);
        connect(ui_->extraCloseColumnBtn, &QPushButton::clicked, this, &MainWindow::toggleLeftBox);
        connect(ui_->settingsTopBtn, &QPushButton::clicked, this, &MainWindow::toggleRightBox);
        loadTheme();
        updateSelectedMenu(ui_->btn_home);
    }
    MainWindow::~MainWindow() = default;
    void MainWindow::toggleMaximizeRestore()
    {
        if (isMaximized())
        {
            showNormal();
            ui_->appMargins->setContentsMargins(10, 10, 10, 10);
            ui_->frame_size_grip->show();
        }
        else
        {
            showMaximized();
            ui_->appMargins->setContentsMargins(0, 0, 0, 0);
            ui_->frame_size_grip->hide();
        }
    }
    void MainWindow::switchPage()
    {
        QObject *s = sender();
        if (s == ui_->btn_home)
            ui_->stackedWidget->setCurrentWidget(home_);
        else if (s == ui_->btn_parameters)
            ui_->stackedWidget->setCurrentWidget(parameters_);
        else if (s == ui_->btn_camera)
            ui_->stackedWidget->setCurrentWidget(camera_);
        updateSelectedMenu(qobject_cast<QWidget *>(s));
    }
    void MainWindow::updateSelectedMenu(QWidget *selected)
    {
        static const QString selectedStyle =
            "border-left: 22px solid qlineargradient(spread:pad, x1:0.034, y1:0, x2:0.216, y2:0, "
            "stop:0.499 rgba(255, 121, 198, 255), stop:0.5 rgba(85, 170, 255, 0));"
            "background-color: rgb(86, 99, 136);";
        for (auto *button : {ui_->btn_home, ui_->btn_parameters, ui_->btn_camera})
        {
            QString style = button->styleSheet();
            style.remove(selectedStyle);
            if (button == selected)
                style += selectedStyle;
            button->setStyleSheet(style);
        }
    }
    void MainWindow::toggleMenu()
    {
        auto *animation = new QPropertyAnimation(ui_->leftMenuBg, "minimumWidth", this);
        animation->setDuration(500);
        animation->setStartValue(ui_->leftMenuBg->width());
        animation->setEndValue(ui_->leftMenuBg->width() <= 60 ? 240 : 60);
        animation->setEasingCurve(QEasingCurve::InOutQuart);
        connect(animation, &QPropertyAnimation::valueChanged, this, [this](const QVariant &value)
                { ui_->leftMenuBg->setMaximumWidth(value.toInt()); });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    void MainWindow::animateSideBoxes(int leftWidth, int rightWidth)
    {
        auto *group = new QParallelAnimationGroup(this);
        for (const auto &[widget, target] : {std::pair<QWidget *, int>{ui_->extraLeftBox, leftWidth},
                                             std::pair<QWidget *, int>{ui_->extraRightBox, rightWidth}})
        {
            auto *animation = new QPropertyAnimation(widget, "minimumWidth", group);
            animation->setDuration(500);
            animation->setStartValue(widget->width());
            animation->setEndValue(target);
            animation->setEasingCurve(QEasingCurve::InOutQuart);
            connect(animation, &QPropertyAnimation::valueChanged, widget, [widget](const QVariant &value)
                    { widget->setMaximumWidth(value.toInt()); });
            group->addAnimation(animation);
        }
        group->start(QAbstractAnimation::DeleteWhenStopped);
    }
    void MainWindow::toggleLeftBox()
    {
        animateSideBoxes(ui_->extraLeftBox->width() == 0 ? 240 : 0, 0);
    }
    void MainWindow::toggleRightBox()
    {
        animateSideBoxes(0, ui_->extraRightBox->width() == 0 ? 240 : 0);
    }
    void MainWindow::loadTheme()
    {
        const QString path = findProjectFile("src/themes/py_dracula_dark.qss");
        if (!path.isEmpty())
        {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                ui_->styleSheet->setStyleSheet(QString::fromUtf8(file.readAll()));
        }
    }
    void MainWindow::closeEvent(QCloseEvent *event)
    {
        if (QMessageBox::question(this, "Confirm Exit", "Close the program?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        emit AppSignals::instance().appClose();
        event->accept();
    }
    void MainWindow::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
            dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        QMainWindow::mousePressEvent(event);
    }
    void MainWindow::mouseMoveEvent(QMouseEvent *event)
    {
        if ((event->buttons() & Qt::LeftButton) && !isMaximized())
        {
            move(event->globalPosition().toPoint() - dragPosition_);
            event->accept();
            return;
        }
        QMainWindow::mouseMoveEvent(event);
    }
    void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton && event->position().y() <= ui_->contentTopBg->height())
        {
            toggleMaximizeRestore();
            event->accept();
            return;
        }
        QMainWindow::mouseDoubleClickEvent(event);
    }
}
