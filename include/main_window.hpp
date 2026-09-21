#pragma once
#include <QMainWindow>
#include <QString>
#include <memory>
#include <optional>

class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

namespace pva
{
    class PageHome;
    class PageCamera;
    class PageParameters;

    class MainWindow final : public QMainWindow
    {
        Q_OBJECT
    public:
        explicit MainWindow(QWidget *parent = nullptr);
        ~MainWindow() override;

    protected:
        void closeEvent(QCloseEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseDoubleClickEvent(QMouseEvent *event) override;
    private slots:
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
        PageHome *home_{};
        PageCamera *camera_{};
        PageParameters *parameters_{};
        QTimer *statusHoldTimer_{};
        std::optional<StatusMessage> pendingStatus_;
        QString currentStatusText_;
        bool currentStatusAlarm_{};
        QString configPath_;
        QString themePath_;
        QPoint dragPosition_;
        void loadTheme();
        void updateSelectedMenu(QWidget *selected);
        void animateSideBoxes(int leftWidth, int rightWidth);
        void toggleMaximizeRestore();
        void displayGlobalStatus(const StatusMessage &status);
        void displayPendingStatus();
    };
}
