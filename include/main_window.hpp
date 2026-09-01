#pragma once
#include <QMainWindow>
#include <memory>

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

    private:
        std::unique_ptr<Ui::MainWindow> ui_;
        PageHome *home_{};
        PageCamera *camera_{};
        PageParameters *parameters_{};
        QString configPath_;
        QString themePath_;
        QPoint dragPosition_;
        void loadTheme();
        void updateSelectedMenu(QWidget *selected);
        void animateSideBoxes(int leftWidth, int rightWidth);
        void toggleMaximizeRestore();
    };
}
