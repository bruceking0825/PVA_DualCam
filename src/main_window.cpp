#include "main_window.hpp"
#include "app_signals.hpp"
#include "config_manager.hpp"
#include "page_camera.hpp"
#include "app_services.hpp"
#include "page_home.hpp"
#include "page_parameters.hpp"
#include "ui_main.h"
#include <QApplication>
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
#include <QTimer>
#include <QEvent>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pva
{
    namespace
    {
        constexpr int NormalStatusHoldMs = 2000;
        constexpr int AlarmStatusHoldMs = 5000;

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

    MainWindow::MainWindow(QWidget *parent, bool startServices) : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>())
    {
        ui_->setupUi(this);
        // 原 Python 版本使用自绘标题栏；透明无边框可消除系统窗口的白色外围框。
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowSystemMenuHint |
                       Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle("Stereo Vision Measurement");
        ui_->titleRightInfo->setText("Stereo Vision Measurement");
        ui_->btn_home->setText("Home");
        ui_->btn_statistics->hide();
        ui_->btn_IO->hide();
        statusHoldTimer_ = new QTimer(this);
        statusHoldTimer_->setSingleShot(true);
        connect(statusHoldTimer_, &QTimer::timeout, this, &MainWindow::displayPendingStatus);
        auto &appSignals = AppSignals::instance();
        connect(&appSignals, &AppSignals::status, this, &MainWindow::showGlobalStatus);
        showGlobalStatus("System", "OK", "info", "Ready");
        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(17);
        shadow->setOffset(0, 0);
        shadow->setColor(QColor(0, 0, 0, 150));
        ui_->bgApp->setGraphicsEffect(shadow);
        // 与 Python 版本一致，在无边框窗口右下角提供系统窗口缩放手柄。
        auto *sizeGrip = new QSizeGrip(ui_->frame_size_grip);
        sizeGrip->setFixedSize(20, 20);
        sizeGrip->setStyleSheet("margin: 0px; padding: 0px;");
        configPath_ = findProjectFile("cnf.ini");
        if (configPath_.isEmpty())
            throw std::runtime_error("Cannot locate cnf.ini");
        auto &configManager = ConfigManager::instance();
        configManager.load(configPath_, false);
        services_ = std::make_unique<AppServices>(configManager.config());
        home_ = new PageHome(configManager.config(), services_->runtime(), ui_->stackedWidget);
        camera_ = new PageCamera(services_->cameras(), ui_->stackedWidget);
        connect(&services_->cameras(), &CameraService::cameraExposureChanged, home_, &PageHome::onCameraExposure);
        connect(&services_->cameras(), &CameraService::stateChanged, home_, &PageHome::onCameraState);
        if (startServices) services_->start();
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
        updateWindowState();
        trayIcon_ = new QSystemTrayIcon(QIcon(":/app/zh1.ico"), this);
        trayIcon_->setObjectName("systemTrayIcon");
        trayIcon_->setToolTip("Stereo Vision Measurement");
        auto *trayMenu = new QMenu(this);
        auto *showAction = trayMenu->addAction("Show", this, &MainWindow::restoreFromTray);
        showAction->setObjectName("trayShowAction");
        trayMenu->addSeparator();
        auto *exitAction = trayMenu->addAction("Exit", this, &MainWindow::exitApplication);
        exitAction->setObjectName("trayExitAction");
        trayIcon_->setContextMenu(trayMenu);
        connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) restoreFromTray();
        });
        if (QSystemTrayIcon::isSystemTrayAvailable()) trayIcon_->show();
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            trayIcon_->hide();
            services_->stop();
        });
    }
    MainWindow::~MainWindow()
    {
        if (trayIcon_) trayIcon_->hide();
    }
    void MainWindow::setTitleDragActive(bool active)
    {
        if (titleDragActive_ == active) return;
        titleDragActive_ = active;
        if (home_) home_->setDisplayPaused(active);
    }
    bool MainWindow::event(QEvent *event)
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate ||
            event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)
            setTitleDragActive(false);
        return QMainWindow::event(event);
    }
    void MainWindow::toggleMaximizeRestore()
    {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    }
    void MainWindow::changeEvent(QEvent *event)
    {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) updateWindowState();
    }
#ifdef Q_OS_WIN
    bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                                 qintptr *result)
#else
                                 long *result)
#endif
    {
        const auto *native = static_cast<MSG *>(message);
        if (native->message == WM_ENTERSIZEMOVE) setTitleDragActive(true);
        else if (native->message == WM_EXITSIZEMOVE || native->message == WM_CANCELMODE ||
                 native->message == WM_CAPTURECHANGED)
            setTitleDragActive(false);
        if (native->message == WM_SYSCOMMAND) {
            switch (native->wParam & 0xfff0) {
            case SC_MINIMIZE:
                showMinimized();
                break;
            case SC_MAXIMIZE:
                showMaximized();
                break;
            case SC_RESTORE:
                if (isMinimized()) {
                    setWindowState(windowState() & ~Qt::WindowMinimized);
                    show();
                } else showNormal();
                break;
            default:
                return QMainWindow::nativeEvent(eventType, message, result);
            }
            *result = 0;
            return true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
#endif
    void MainWindow::updateWindowState()
    {
        const bool maximized = windowState().testFlag(Qt::WindowMaximized);
        ui_->maximizeRestoreAppBtn->setIcon(QIcon(maximized ? ":/icons/images/icons/icon_restore.png"
                                                         : ":/icons/images/icons/icon_maximize.png"));
        ui_->maximizeRestoreAppBtn->setToolTip(maximized ? "Restore" : "Maximize");
        const int margin = maximized ? 0 : 10;
        ui_->appMargins->setContentsMargins(margin, margin, margin, margin);
        ui_->frame_size_grip->setVisible(!maximized);
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
    void MainWindow::showGlobalStatus(const QString &device, const QString &state,
                                      const QString &type, const QString &message)
    {
        const bool alarm = state.compare("NG", Qt::CaseInsensitive) == 0 ||
                           type.compare("error", Qt::CaseInsensitive) == 0;
        const QString source = device.trimmed();
        const QString text = source.isEmpty() ? message : QString("%1: %2").arg(source, message);
        const StatusMessage incoming{text, alarm};

        if (text == currentStatusText_ && alarm == currentStatusAlarm_)
        {
            // 持续出现的同一报警从最后一次发生起继续保留 5 秒，期间不被正常帧覆盖。
            if (alarm)
            {
                if (pendingStatus_ && !pendingStatus_->alarm)
                    pendingStatus_.reset();
                statusHoldTimer_->start(AlarmStatusHoldMs);
            }
            return;
        }

        if (statusHoldTimer_->isActive())
        {
            // 报警可以立即打断普通信息；已经显示的报警则必须先让用户看清。
            if (alarm && !currentStatusAlarm_)
            {
                pendingStatus_.reset();
                displayGlobalStatus(incoming);
                return;
            }
            // 已排队的报警不能被后续普通运行信息覆盖。
            if (!alarm && pendingStatus_ && pendingStatus_->alarm)
                return;
            pendingStatus_ = incoming;
            return;
        }

        displayGlobalStatus(incoming);
    }

    void MainWindow::displayGlobalStatus(const StatusMessage &status)
    {
        currentStatusText_ = status.text;
        currentStatusAlarm_ = status.alarm;
        ui_->lblStatus->setText(status.text);
        ui_->lblStatus->setToolTip(status.text);
        ui_->lblStatus->setStyleSheet(
            QString("background-color: %1; color: white; border-radius: 3px; "
                    "padding: 1px 8px;")
                .arg(status.alarm ? "rgb(190, 55, 55)" : "rgb(42, 150, 75)"));
        statusHoldTimer_->start(status.alarm ? AlarmStatusHoldMs : NormalStatusHoldMs);
    }

    void MainWindow::displayPendingStatus()
    {
        if (!pendingStatus_)
            return;
        const StatusMessage status = std::move(*pendingStatus_);
        pendingStatus_.reset();
        displayGlobalStatus(status);
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
        const QString path = findProjectFile("themes/py_dracula_dark.qss");
        if (!path.isEmpty())
        {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                const QString styleSheet = QString::fromUtf8(file.readAll());
                // main.ui contains a design-time copy of the theme. Clear it so
                // one application-wide style sheet controls every widget,
                // including message boxes and file dialogs.
                ui_->styleSheet->setStyleSheet({});
                qApp->setStyleSheet(styleSheet);
            }
        }
    }
    void MainWindow::closeEvent(QCloseEvent *event)
    {
        if (exitRequested_) {
            event->accept();
            return;
        }
        event->ignore();
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            trayWasMaximized_ = windowState().testFlag(Qt::WindowMaximized);
            hiddenToTray_ = true;
            trayIcon_->show();
            hide();
        } else {
            hiddenToTray_ = false;
            showMinimized();
            showGlobalStatus("System", "INFO", "info", "System tray unavailable; minimized to taskbar");
        }
    }
    void MainWindow::restoreFromTray()
    {
        if (exitRequested_) return;
        if (!isVisible() || isMinimized()) {
            const bool maximized = hiddenToTray_ ? trayWasMaximized_ : windowState().testFlag(Qt::WindowMaximized);
            hiddenToTray_ = false;
            if (maximized) showMaximized();
            else showNormal();
        }
        raise();
        activateWindow();
    }
    void MainWindow::exitApplication()
    {
        if (exitRequested_ || exitConfirmationOpen_) return;
        exitConfirmationOpen_ = true;
        const auto answer = QMessageBox::question(this, "Confirm Exit", "Close the program?",
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        exitConfirmationOpen_ = false;
        if (answer != QMessageBox::Yes) return;
        exitRequested_ = true;
        emit AppSignals::instance().appClose();
        services_->stop();
        trayIcon_->hide();
        hide();
        qApp->quit();
    }
    void MainWindow::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton && !isMaximized() &&
            ui_->contentTopBg->rect().contains(ui_->contentTopBg->mapFrom(this, event->pos())))
            setTitleDragActive(true);
        if (event->button() == Qt::LeftButton)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
            dragPosition_ = event->globalPos() - frameGeometry().topLeft();
#endif
        QMainWindow::mousePressEvent(event);
    }
    void MainWindow::mouseMoveEvent(QMouseEvent *event)
    {
        if ((event->buttons() & Qt::LeftButton) && !isMaximized())
        {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            move(event->globalPosition().toPoint() - dragPosition_);
#else
            move(event->globalPos() - dragPosition_);
#endif
            event->accept();
            return;
        }
        QMainWindow::mouseMoveEvent(event);
    }
    void MainWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton) setTitleDragActive(false);
        QMainWindow::mouseReleaseEvent(event);
    }
    void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (event->button() == Qt::LeftButton && event->position().y() <= ui_->contentTopBg->height())
#else
        if (event->button() == Qt::LeftButton && event->pos().y() <= ui_->contentTopBg->height())
#endif
        {
            toggleMaximizeRestore();
            event->accept();
            return;
        }
        QMainWindow::mouseDoubleClickEvent(event);
    }
}
