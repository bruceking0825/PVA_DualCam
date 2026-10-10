#include "page_parameters.hpp"
#include "app_signals.hpp"
#include "config_manager.hpp"
#include "ui_PageParameters.h"
#include <utility>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollArea>
#include <QTextStream>
#include <QVBoxLayout>

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

    PageParameters::PageParameters(QString path, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageParameters>()), configPath_(std::move(path))
    {
        initializePage([this]
                       { ui_->setupUi(this); });
    }

    void PageParameters::initializeState() {}

    void PageParameters::setupPageUi() {}

    void PageParameters::bindEvents()
    {
        connect(ui_->btn_load_parm, &QPushButton::clicked, this, &PageParameters::load);
        connect(ui_->btn_cancel_parm, &QPushButton::clicked, this, [this]
                {
                    if (loadFromDisk())
                        ConfigManager::instance().load(configPath_); });
        connect(ui_->btn_save_parm, &QPushButton::clicked, this, &PageParameters::save);
    }

    void PageParameters::bindSignals()
    {
        connect(&AppSignals::instance(), &AppSignals::appClose, this, &QWidget::close);
    }

    void PageParameters::onReady()
    {
        loadFromDisk();
        emit ConfigManager::instance().batchChanged();
    }
    PageParameters::~PageParameters() = default;
    void PageParameters::clearTabs()
    {
        formRows_.clear();
        groupOrder_.clear();
        while (ui_->tabWidget->count())
            delete ui_->tabWidget->widget(0);
    }
    void PageParameters::load()
    {
        if (loadFromDisk())
        {
            ConfigManager::instance().load(configPath_);
            QMessageBox::information(this, "Load", "Settings loaded successfully.");
        }
    }

    bool PageParameters::loadFromDisk()
    {
        QFile file(configPath_);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QMessageBox::warning(this, "Load", file.errorString());
            return false;
        }
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

                // Keep the same hierarchy as the Python page.  In particular,
                // the scroll area's frame must not become an extra box below
                // the tabs.
                auto *outerLayout = new QVBoxLayout(page);
                auto *scroll = new QScrollArea;
                scroll->setWidgetResizable(true);
                scroll->setStyleSheet("QScrollArea { border: none; }");

                auto *content = new QWidget;
                content->setObjectName("formContainer");
                content->setStyleSheet(
                    "QWidget#formContainer {"
                    "border: 2px solid rgb(70, 80, 110);"
                    "background: transparent;"
                    "padding: 0px;"
                    "}");
                form = new QFormLayout(content);
                scroll->setWidget(content);
                outerLayout->addWidget(scroll);
                const QString group = page->property("section").toString();
                ui_->tabWidget->addTab(page, group);
                groupOrder_.append(group);
            }
            else if (form && !line.isEmpty() && !line.startsWith('#') &&
                     !line.startsWith(';') && line.contains('='))
            {
                auto parts = line.split('=');
                QString key = parts.takeFirst().trimmed(), value = displayValue(parts.join('='));
                const QString group = page->property("section").toString();
                const auto row = createRow(group, key, value);
                form->addRow(row.label, row.edit);
                formRows_[group].append(row);
            }
        }
        return true;
    }
    void PageParameters::save()
    {
        for (const QString &group : std::as_const(groupOrder_))
        {
            for (const FormRow &row : formRows_.value(group))
            {
                if (!row.edit)
                    continue;
                QString error;
                if (!ConfigManager::instance().setEntry(group, row.key, row.edit->text(), &error))
                {
                    QMessageBox::warning(this, "Parameter", error);
                    row.edit->setFocus();
                    return;
                }
            }
        }

        QSaveFile file(configPath_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QMessageBox::warning(this, "Save", file.errorString());
            return;
        }
        QTextStream out(&file);
        for (const QString &group : std::as_const(groupOrder_))
        {
            out << '[' << group << "]\n";
            for (const FormRow &row : formRows_.value(group))
                if (row.edit)
                    out << row.key << " = " << storedValue(row.edit->text()) << "\n";
            out << "\n";
        }
        if (!file.commit())
        {
            QMessageBox::warning(this, "Save", file.errorString());
            return;
        }
        emit configurationSaved();
        QMessageBox::information(this, "Save", "Settings saved successfully.");
    }

    PageParameters::FormRow PageParameters::createRow(const QString &group, const QString &key, const QString &value)
    {
        auto *edit = new QLineEdit(value, this);
        edit->setProperty("key", key);
        connect(edit, &QLineEdit::returnPressed, this, [this, group, key, edit]
                {
                    QString error;
                    if (!ConfigManager::instance().setEntry(group, key, edit->text(), &error))
                        QMessageBox::warning(this, "Parameter", error); });
        auto *label = new QLabel(key, this);
        label->setObjectName(key);
        label->setBuddy(edit);
        return FormRow{key, label, edit};
    }


}
