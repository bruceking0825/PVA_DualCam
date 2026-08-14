#include "pva/page_parameters.hpp"
#include "ui_PageParameters.h"
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QSaveFile>
#include <QTabBar>
#include <QTextStream>

namespace pva
{
    namespace
    {
        QString displayValue(QString value)
        {
            value = value.trimmed();
            if (value.startsWith('[') && value.endsWith(']'))
                value = value.mid(1, value.size() - 2).trimmed();
            return value;
        }

        QString storedValue(QString value)
        {
            // 参数页只编辑纯值；写盘时恢复项目约定的 [value] 格式。
            return '[' + displayValue(std::move(value)) + ']';
        }
    }

    PageParameters::PageParameters(QString path, QWidget *parent) : QWidget(parent), ui_(std::make_unique<Ui::PageParameters>()), configPath_(std::move(path))
    {
        ui_->setupUi(this);
        connect(ui_->btn_load_parm, &QPushButton::clicked, this, &PageParameters::load);
        connect(ui_->btn_cancel_parm, &QPushButton::clicked, this, &PageParameters::load);
        connect(ui_->btn_save_parm, &QPushButton::clicked, this, &PageParameters::save);
        connect(ui_->btn_add, &QPushButton::clicked, this, &PageParameters::addRow);
        connect(ui_->btn_insert, &QPushButton::clicked, this, &PageParameters::insertRow);
        connect(ui_->btn_delete, &QPushButton::clicked, this, &PageParameters::deleteRow);
        connect(ui_->btn_up, &QPushButton::clicked, this, &PageParameters::moveRowUp);
        connect(ui_->btn_down, &QPushButton::clicked, this, &PageParameters::moveRowDown);
        connect(ui_->tabWidget, &QTabWidget::currentChanged, this, [this](int)
                {
                    // Python Settings.WIDGET_TAB_STYLE：当前参数组使用粉色左边线和蓝灰背景。
                    ui_->tabWidget->tabBar()->setStyleSheet(
                        "QTabBar::tab:selected {"
                        "border-left: 2px solid rgb(255, 121, 198);"
                        "background-color: rgb(86, 99, 136);"
                        "color: rgb(255, 255, 255);"
                        "}");
                });
        load();
        ui_->tabWidget->tabBar()->setStyleSheet(
            "QTabBar::tab:selected {"
            "border-left: 2px solid rgb(255, 121, 198);"
            "background-color: rgb(86, 99, 136);"
            "color: rgb(255, 255, 255);"
            "}");
    }
    PageParameters::~PageParameters() = default;
    void PageParameters::clearTabs()
    {
        while (ui_->tabWidget->count())
            delete ui_->tabWidget->widget(0);
    }
    void PageParameters::load()
    {
        QFile file(configPath_);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        clearTabs();
        QWidget *page = nullptr;
        QFormLayout *form = nullptr;
        QTextStream in(&file);
        while (!in.atEnd())
        {
            QString line = in.readLine().trimmed();
            if (line.startsWith('[') && line.endsWith(']'))
            {
                page = new QWidget;
                page->setProperty("section", line.mid(1, line.size() - 2));
                form = new QFormLayout(page);
                ui_->tabWidget->addTab(page, page->property("section").toString());
            }
            else if (form && !line.isEmpty() && !line.startsWith('#') && line.contains('='))
            {
                auto parts = line.split('=');
                QString key = parts.takeFirst().trimmed(), value = displayValue(parts.join('='));
                auto *edit = new QLineEdit(value);
                edit->setProperty("key", key);
                form->addRow(new QLabel(key), edit);
            }
        }
    }
    void PageParameters::save()
    {
        QSaveFile file(configPath_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QMessageBox::warning(this, "Save", file.errorString());
            return;
        }
        QTextStream out(&file);
        for (int i = 0; i < ui_->tabWidget->count(); ++i)
        {
            auto *page = ui_->tabWidget->widget(i);
            out << '[' << page->property("section").toString() << "]\n";
            auto *form = qobject_cast<QFormLayout *>(page->layout());
            for (int row = 0; form && row < form->rowCount(); ++row)
            {
                auto *field = form->itemAt(row, QFormLayout::FieldRole);
                auto *edit = field ? qobject_cast<QLineEdit *>(field->widget()) : nullptr;
                if (edit)
                    out << edit->property("key").toString() << " = " << storedValue(edit->text()) << "\n";
            }
            out << "\n";
        }
        if (file.commit())
            emit configurationSaved();
    }

    QFormLayout *PageParameters::currentForm() const
    {
        auto *page = ui_->tabWidget->currentWidget();
        return page ? qobject_cast<QFormLayout *>(page->layout()) : nullptr;
    }

    QLineEdit *PageParameters::selectedEditor() const
    {
        auto *edit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        return edit && currentForm() && edit->parentWidget() == ui_->tabWidget->currentWidget() ? edit : nullptr;
    }

    void PageParameters::createRow(int row)
    {
        auto *form = currentForm();
        if (!form) return;
        bool accepted = false;
        const QString key = QInputDialog::getText(this, "Parameter", "Key:", QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted || key.isEmpty()) return;
        auto *edit = new QLineEdit;
        edit->setProperty("key", key);
        auto *label = new QLabel(key);
        if (row < 0 || row >= form->rowCount()) form->addRow(label, edit);
        else form->insertRow(row, label, edit);
        edit->setFocus();
    }

    void PageParameters::addRow() { createRow(); }
    void PageParameters::insertRow()
    {
        auto *form = currentForm();
        auto *edit = selectedEditor();
        int row = -1;
        QFormLayout::ItemRole role{};
        if (form && edit) form->getWidgetPosition(edit, &row, &role);
        createRow(row);
    }

    void PageParameters::deleteRow()
    {
        auto *form = currentForm();
        auto *edit = selectedEditor();
        if (!form || !edit) return;
        int row = -1; QFormLayout::ItemRole role{};
        form->getWidgetPosition(edit, &row, &role);
        if (row >= 0)
        {
            auto taken = form->takeRow(row);
            delete taken.labelItem->widget();
            delete taken.fieldItem->widget();
            delete taken.labelItem;
            delete taken.fieldItem;
        }
    }

    void PageParameters::moveSelectedRow(int delta)
    {
        auto *form = currentForm();
        auto *edit = selectedEditor();
        if (!form || !edit) return;
        int row = -1; QFormLayout::ItemRole role{};
        form->getWidgetPosition(edit, &row, &role);
        const int target = row + delta;
        if (row < 0 || target < 0 || target >= form->rowCount()) return;
        auto taken = form->takeRow(row);
        auto *label = taken.labelItem->widget();
        auto *field = taken.fieldItem->widget();
        delete taken.labelItem;
        delete taken.fieldItem;
        form->insertRow(target, label, field);
        edit->setFocus();
    }

    void PageParameters::moveRowUp() { moveSelectedRow(-1); }
    void PageParameters::moveRowDown() { moveSelectedRow(1); }
}
