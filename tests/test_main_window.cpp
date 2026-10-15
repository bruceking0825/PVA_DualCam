#include "main_window.hpp"
#include "page_home.hpp"
#include "app_signals.hpp"
#include <QSystemTrayIcon>
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>
#include <functional>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 2000) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return ready();
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    // No camera enumeration, PLC listeners, state writes or visible test window.
    pva::MainWindow window(nullptr, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    auto *maximize = window.findChild<QPushButton *>("maximizeRestoreAppBtn");
    auto *minimize = window.findChild<QPushButton *>("minimizeAppBtn");
    auto *margins = window.findChild<QLayout *>("appMargins");
    auto *grip = window.findChild<QWidget *>("frame_size_grip");
    check(maximize && minimize && margins && grip, "Title controls exist");
    if (!maximize || !minimize || !margins || !grip) return 1;
    const auto controlsMatch = [&](bool maximized) {
        const auto expected = QIcon(maximized ? ":/icons/images/icons/icon_restore.png"
                                             : ":/icons/images/icons/icon_maximize.png").pixmap(20, 20).toImage();
        return maximize->icon().pixmap(20, 20).toImage() == expected &&
               maximize->toolTip() == (maximized ? "Restore" : "Maximize") &&
               margins->contentsMargins() == (maximized ? QMargins() : QMargins(10, 10, 10, 10)) &&
               grip->isHidden() == maximized;
    };
    check(window.windowFlags().testFlag(Qt::FramelessWindowHint) &&
          window.windowFlags().testFlag(Qt::WindowSystemMenuHint) &&
          window.windowFlags().testFlag(Qt::WindowMinimizeButtonHint) &&
          window.windowFlags().testFlag(Qt::WindowMaximizeButtonHint),
          "Frameless window retains native system menu and minimize/maximize capabilities");
    check(controlsMatch(false), "Initial icon, tooltip, margins and grip match normal state");
    auto *home = window.findChild<pva::PageHome *>();
    auto *title = window.findChild<QWidget *>("contentTopBg");
    check(home && title, "Title drag has a Home display target");
    if (home && title) {
        const QPoint titlePoint = title->mapTo(&window, QPoint(100, 10));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(titlePoint), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &press);
        check(home->isDisplayPaused(), "Title press pauses Home display");
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(titlePoint), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &release);
        check(!home->isDisplayPaused(), "Title release resumes Home display");
        QApplication::sendEvent(&window, &press);
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivate);
        check(!home->isDisplayPaused(), "Lost window activation does not leave Home display paused");
    }
    maximize->click();
    check(until([&] { return window.isMaximized() && controlsMatch(true); }),
          "Maximize button switches to restore icon");
    maximize->click();
    check(until([&] { return !window.isMaximized() && controlsMatch(false); }),
          "Restore button switches back to maximize icon");
    window.showMaximized();
    check(until([&] { return controlsMatch(true); }), "External maximize synchronizes title controls");
    minimize->click();
    check(until([&] { return window.isMinimized(); }), "Minimize button works from maximized state");
    window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
    check(until([&] { return !window.isMinimized() && window.isMaximized() && controlsMatch(true); }),
          "Restoring a minimized maximized window keeps the restore icon");
    window.showNormal();
    check(until([&] { return controlsMatch(false); }), "External restore synchronizes title controls");
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(100, 10), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &doubleClick);
    check(until([&] { return window.isMaximized() && controlsMatch(true); }),
          "Title double-click synchronizes the restore icon");
    window.showNormal();
#ifdef Q_OS_WIN
    if (QApplication::platformName() == "windows") {
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
        check(home && home->isDisplayPaused(), "Native move loop pauses Home display");
        SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
        check(home && !home->isDisplayPaused(), "Native move loop exit resumes Home display");
        check((style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ==
              (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), "Windows native styles enable taskbar system commands");
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        check(until([&] { return window.isMinimized(); }), "Native taskbar minimize command reaches Qt state");
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        check(until([&] { return !window.isMinimized() && controlsMatch(false); }),
              "Native taskbar restore command restores the window and icon");
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        check(until([&] { return window.isMaximized() && controlsMatch(true); }),
              "Native maximize command updates the restore icon");
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        check(until([&] { return window.isMinimized(); }), "Native minimize also works from maximized state");
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        check(until([&] { return !window.isMinimized() && window.isMaximized() && controlsMatch(true); }),
              "Native restore preserves the previously maximized state");
    }
#endif
    window.showNormal();
    auto *tray = window.findChild<QSystemTrayIcon *>("systemTrayIcon");
    auto *showAction = window.findChild<QAction *>("trayShowAction");
    auto *exitAction = window.findChild<QAction *>("trayExitAction");
    auto *closeButton = window.findChild<QPushButton *>("closeAppBtn");
    check(tray && showAction && exitAction && closeButton, "Tray and its Show/Exit actions exist");
    if (!tray || !showAction || !exitAction || !closeButton) return 1;
    check(!tray->icon().isNull() && tray->toolTip() == "Stereo Vision Measurement" &&
          showAction->text() == "Show" && exitAction->text() == "Exit",
          "Tray reuses application artwork and English menu labels");
    int closeSignals = 0;
    QObject::connect(&pva::AppSignals::instance(), &pva::AppSignals::appClose, &window, [&] { ++closeSignals; });
    bool unexpectedConfirmation = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = window.findChild<QMessageBox *>()) {
            unexpectedConfirmation = true;
            dialog->button(QMessageBox::No)->click();
        }
    });
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    const auto normalGeometry = window.geometry();
    closeButton->click();
    QCoreApplication::processEvents();
    check(!unexpectedConfirmation && closeSignals == 0, "Close button neither confirms nor stops services");
    check(trayAvailable ? !window.isVisible() && tray->isVisible() : window.isMinimized(),
          "Close hides to tray or minimizes safely when the tray is unavailable");
    window.close();
    check(window.findChildren<QSystemTrayIcon *>().size() == 1 && closeSignals == 0,
          "Repeated close does not create duplicate icons or request shutdown");
    showAction->trigger();
    check(until([&] { return window.isVisible() && !window.isMinimized() && !window.isMaximized(); }),
          "Tray Show restores the normal window");
    check(window.geometry() == normalGeometry, "Tray restore preserves normal window geometry");
    window.showMaximized();
    window.close();
    tray->activated(QSystemTrayIcon::Trigger);
    check(until([&] { return !window.isMinimized() && window.isMaximized() && controlsMatch(true); }),
          "Single-click tray activation preserves maximized state");
    window.close();
    tray->activated(QSystemTrayIcon::DoubleClick);
    check(until([&] { return window.isVisible() && !window.isMinimized() && window.isMaximized(); }),
          "Double-click tray activation restores the window");
    showAction->trigger();
    check(window.isMaximized(), "Show on an already visible window does not change maximized state");
    window.showNormal();
    minimize->click();
    check(window.isMinimized() && (!trayAvailable || window.isVisible()),
          "Minimize button retains ordinary taskbar minimize behavior");
    showAction->trigger();
#ifdef Q_OS_WIN
    if (QApplication::platformName() == "windows") {
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        check(until([&] { return closeSignals == 0 && (trayAvailable ? !window.isVisible() : window.isMinimized()); }),
              "System close, including Alt+F4, uses the tray close path");
        showAction->trigger();
    }
#endif
    bool cancelDialogSeen = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = window.findChild<QMessageBox *>()) {
            cancelDialogSeen = dialog->windowTitle() == "Confirm Exit";
            dialog->button(QMessageBox::No)->click();
        }
    });
    exitAction->trigger();
    check(cancelDialogSeen && closeSignals == 0 && window.isVisible(), "Cancelling tray Exit leaves application running");
    bool confirmDialogSeen = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = window.findChild<QMessageBox *>()) {
            confirmDialogSeen = true;
            dialog->button(QMessageBox::Yes)->click();
        }
    });
    exitAction->trigger();
    check(confirmDialogSeen && closeSignals == 1 && !tray->isVisible() && !window.isVisible(),
          "Confirmed Exit requests shutdown once and removes tray and window");
    exitAction->trigger();
    check(closeSignals == 1, "Repeated Exit does not prompt or stop services twice");
    return failures ? 1 : 0;
}
