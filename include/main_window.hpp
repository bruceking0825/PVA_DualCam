#pragma once
#include <QMainWindow>
#include <QString>
#include <memory>
#include <optional>

class QTimer;
class QSystemTrayIcon;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

namespace pva
{
    class AppServices;
    class PageHome;
    class PageCamera;
    class PageParameters;

    class MainWindow final : public QMainWindow
    {
        Q_OBJECT
    public:
        explicit MainWindow(QWidget *parent = nullptr, bool startServices = true);
        ~MainWindow() override;

    protected:
        bool event(QEvent *event) override;
        void changeEvent(QEvent *event) override;
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
        bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif
#endif
        void closeEvent(QCloseEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void mouseDoubleClickEvent(QMouseEvent *event) override;
    private slots:
        void restoreFromTray();
        void exitApplication();
        void switchPage();
        void toggleMenu();
        void toggleLeftBox();
        void toggleRightBox();
        void showGlobalStatus(const QString &device, const QString &state,
                              const QString &type, const QString &message);

    private:
        struct StatusMessage
        {
            QString text;
            bool alarm{};
        };

        std::unique_ptr<Ui::MainWindow> ui_;
        std::unique_ptr<AppServices> services_;
        PageHome *home_{};
        PageCamera *camera_{};
        PageParameters *parameters_{};
        QTimer *statusHoldTimer_{};
        QSystemTrayIcon *trayIcon_{};
        bool hiddenToTray_{false};
        bool trayWasMaximized_{false};
        bool exitConfirmationOpen_{false};
        bool exitRequested_{false};
        std::optional<StatusMessage> pendingStatus_;
        QString currentStatusText_;
        bool currentStatusAlarm_{};
        QString configPath_;
        QString themePath_;
        QPoint dragPosition_;
        bool titleDragActive_{false};
        void setTitleDragActive(bool active);
        void loadTheme();
        void updateSelectedMenu(QWidget *selected);
        void animateSideBoxes(int leftWidth, int rightWidth);
        void toggleMaximizeRestore();
        void updateWindowState();
        void displayGlobalStatus(const StatusMessage &status);
        void displayPendingStatus();
    };
}
