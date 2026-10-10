#pragma once
#include "base_page.hpp"
#include <QHash>
#include <QStringList>
#include <QVector>
#include <memory>

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
    signals:
        void configurationSaved();
    private slots:
        void load();
        void save();

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
        QHash<QString, QVector<FormRow>> formRows_;
        QStringList groupOrder_;
        void clearTabs();
        bool loadFromDisk();
        FormRow createRow(const QString &group, const QString &key, const QString &value);
    };
}
