#pragma once
#include <QWidget>
#include <memory>

class QFormLayout;
class QLineEdit;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class PageParameters;
}
QT_END_NAMESPACE

namespace pva
{
    class PageParameters final : public QWidget
    {
        Q_OBJECT
    public:
        explicit PageParameters(QString configPath, QWidget *parent = nullptr);
        ~PageParameters() override;
    signals:
        void configurationSaved();
    private slots:
        void load();
        void save();
        void addRow();
        void insertRow();
        void deleteRow();
        void moveRowUp();
        void moveRowDown();

    private:
        std::unique_ptr<Ui::PageParameters> ui_;
        QString configPath_;
        void clearTabs();
        QFormLayout *currentForm() const;
        QLineEdit *selectedEditor() const;
        void createRow(int row = -1);
        void moveSelectedRow(int delta);
    };
}
