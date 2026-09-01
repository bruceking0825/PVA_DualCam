#pragma once
#include "base_page.hpp"
#include <QHash>
#include <QStringList>
#include <QVector>
#include <memory>

class QEvent;
class QFormLayout;
class QLabel;
class QLineEdit;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class PageParameters;
}
QT_END_NAMESPACE

namespace pva
{
    class PageParameters final : public BasePage
    {
        Q_OBJECT
    public:
        explicit PageParameters(QString configPath, QWidget *parent = nullptr);
        ~PageParameters() override;
    protected:
        bool eventFilter(QObject *watched, QEvent *event) override;
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
        void initializeState() override;
        void setupPageUi() override;
        void bindEvents() override;
        void bindSignals() override;
        void onReady() override;
        struct FormRow
        {
            QString key;
            QLabel *label = nullptr;
            QLineEdit *edit = nullptr;
        };

        std::unique_ptr<Ui::PageParameters> ui_;
        QString configPath_;
        QHash<QString, QFormLayout *> groupLayouts_;
        QHash<QString, QVector<FormRow>> formRows_;
        QStringList groupOrder_;
        QString selectedGroup_;
        int selectedRowIndex_ = -1;
        void clearTabs();
        bool loadFromDisk();
        QFormLayout *currentForm() const;
        QLineEdit *selectedEditor() const;
        void registerRow(QLabel *label, QLineEdit *edit);
        void selectRow(int row);
        int rowIndexFor(const QObject *widget) const;
        void refreshGroupLayout(const QString &group);
        QString nextKey(const QString &group) const;
        QLineEdit *editorAt(QFormLayout *form, int row) const;
        FormRow createRow(const QString &group, const QString &key, const QString &value);
        void moveSelectedRow(int delta);
    };
}
