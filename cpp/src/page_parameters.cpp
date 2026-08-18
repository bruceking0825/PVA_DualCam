#include "pva/page_parameters.hpp"
#include "pva/app_signals.hpp"
#include "pva/config_manager.hpp"
#include "ui_PageParameters.h"
#include <algorithm>
#include <utility>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QSaveFile>
#include <QScrollArea>
#include <QTabBar>
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

        QFormLayout *formForPage(QWidget *page)
        {
            if (!page)
                return nullptr;
            auto *container = page->findChild<QWidget *>("formContainer");
            return container ? qobject_cast<QFormLayout *>(container->layout()) : nullptr;
        }
    }

    PageParameters::PageParameters(QString path, QWidget *parent)
        : BasePage(parent), ui_(std::make_unique<Ui::PageParameters>()), configPath_(std::move(path))
    {
        initializePage([this] { ui_->setupUi(this); });
    }

    void PageParameters::initializeState() {}

    void PageParameters::setupPageUi()
    {
        ui_->tabWidget->setStyleSheet(
            "QTabWidget::pane { border: none; background-color: transparent; }");
    }

    void PageParameters::bindEvents()
    {
        connect(ui_->btn_load_parm, &QPushButton::clicked, this, &PageParameters::load);
        connect(ui_->btn_cancel_parm, &QPushButton::clicked, this, [this]
                { loadFromDisk(); });
        connect(ui_->btn_save_parm, &QPushButton::clicked, this, &PageParameters::save);
        connect(ui_->btn_add, &QPushButton::clicked, this, &PageParameters::addRow);
        connect(ui_->btn_insert, &QPushButton::clicked, this, &PageParameters::insertRow);
        connect(ui_->btn_delete, &QPushButton::clicked, this, &PageParameters::deleteRow);
        connect(ui_->btn_up, &QPushButton::clicked, this, &PageParameters::moveRowUp);
        connect(ui_->btn_down, &QPushButton::clicked, this, &PageParameters::moveRowDown);
        connect(ui_->tabWidget, &QTabWidget::currentChanged, this, [this](int)
                {
                    selectedGroup_ = ui_->tabWidget->currentIndex() >= 0
                                         ? ui_->tabWidget->tabText(ui_->tabWidget->currentIndex())
                                         : QString{};
                    selectedRowIndex_ = -1;
                    selectRow(-1);
                    // Python Settings.WIDGET_TAB_STYLE：当前参数组使用粉色左边线和蓝灰背景。
                    ui_->tabWidget->tabBar()->setStyleSheet(
                        "QTabBar::tab:selected {"
                        "border-left: 2px solid rgb(255, 121, 198);"
                        "background-color: rgb(86, 99, 136);"
                        "color: rgb(255, 255, 255);"
                        "}");
                });
    }

    void PageParameters::bindSignals()
    {
        connect(&AppSignals::instance(), &AppSignals::appClose, this, &QWidget::close);
    }

    void PageParameters::onReady()
    {
        loadFromDisk();
        emit ConfigManager::instance().batchChanged();
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
        selectedRowIndex_ = -1;
        selectedGroup_.clear();
        groupLayouts_.clear();
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
                groupLayouts_.insert(group, form);
                groupOrder_.append(group);
            }
            else if (form && !line.isEmpty() && !line.startsWith('#') &&
                     !line.startsWith(';') && line.contains('='))
            {
                auto parts = line.split('=');
                QString key = parts.takeFirst().trimmed(), value = displayValue(parts.join('='));
                const QString group = page->property("section").toString();
                formRows_[group].append(createRow(group, key, value));
            }
        }
        for (const QString &group : std::as_const(groupOrder_))
            refreshGroupLayout(group);
        if (ui_->tabWidget->count() > 0)
        {
            selectedGroup_ = ui_->tabWidget->tabText(ui_->tabWidget->currentIndex());
            selectedRowIndex_ = -1;
        }
        return true;
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
        ConfigManager::instance().load(configPath_);
        emit configurationSaved();
        QMessageBox::information(this, "Save", "Settings saved successfully.");
    }

    QFormLayout *PageParameters::currentForm() const
    {
        return groupLayouts_.value(selectedGroup_, formForPage(ui_->tabWidget->currentWidget()));
    }

    QLineEdit *PageParameters::selectedEditor() const
    {
        const auto rows = formRows_.value(selectedGroup_);
        if (selectedRowIndex_ < 0 || selectedRowIndex_ >= rows.size())
            return nullptr;
        return rows.at(selectedRowIndex_).edit;
    }

    QLineEdit *PageParameters::editorAt(QFormLayout *form, int row) const
    {
        auto *item = form && row >= 0 && row < form->rowCount()
                         ? form->itemAt(row, QFormLayout::FieldRole)
                         : nullptr;
        return item ? qobject_cast<QLineEdit *>(item->widget()) : nullptr;
    }

    void PageParameters::registerRow(QLabel *label, QLineEdit *edit)
    {
        label->setBuddy(edit);
        label->installEventFilter(this);
        edit->installEventFilter(this);
    }

    void PageParameters::selectRow(int row)
    {
        auto &rows = formRows_[selectedGroup_];
        selectedRowIndex_ = row >= 0 && row < rows.size() ? row : -1;
        for (int index = 0; index < rows.size(); ++index)
            if (rows[index].label)
                rows[index].label->setStyleSheet(index == selectedRowIndex_
                    ? "border-left: 2px solid rgb(189, 147, 249);"
                      "background-color: rgb(86, 99, 136);"
                      "color: rgb(255, 255, 255);"
                    : QString{});
    }

    int PageParameters::rowIndexFor(const QObject *widget) const
    {
        const auto rows = formRows_.value(selectedGroup_);
        for (int index = 0; index < rows.size(); ++index)
            if (rows[index].label == widget || rows[index].edit == widget)
                return index;
        return -1;
    }

    bool PageParameters::eventFilter(QObject *watched, QEvent *event)
    {
        auto *edit = qobject_cast<QLineEdit *>(watched);
        auto *label = qobject_cast<QLabel *>(watched);
        if (!edit && label)
            edit = qobject_cast<QLineEdit *>(label->buddy());
        if (edit && (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress ||
                     event->type() == QEvent::MouseButtonDblClick))
            selectRow(rowIndexFor(edit));
        if (edit && label && event->type() == QEvent::MouseButtonDblClick)
        {
            bool accepted = false;
            const QString oldKey = edit->property("key").toString();
            const QString newKey = QInputDialog::getText(this, "Parameter", "Key:",
                                                         QLineEdit::Normal, oldKey, &accepted).trimmed();
            auto *form = currentForm();
            bool duplicate = false;
            for (int row = 0; form && row < form->rowCount(); ++row)
                if (auto *other = editorAt(form, row); other && other != edit &&
                                                       other->property("key").toString() == newKey)
                    duplicate = true;
            if (accepted && !newKey.isEmpty() && !duplicate)
            {
                if (selectedRowIndex_ >= 0 && selectedRowIndex_ < formRows_[selectedGroup_].size())
                    formRows_[selectedGroup_][selectedRowIndex_].key = newKey;
                edit->setProperty("key", newKey);
                label->setText(newKey);
            }
            else if (accepted && duplicate)
                QMessageBox::warning(this, "Parameter", "The parameter key already exists.");
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    PageParameters::FormRow PageParameters::createRow(const QString &, const QString &key, const QString &value)
    {
        auto *edit = new QLineEdit(value, this);
        edit->setProperty("key", key);
        auto *label = new QLabel(key, this);
        label->setObjectName(key);
        registerRow(label, edit);
        return FormRow{key, label, edit};
    }

    QString PageParameters::nextKey(const QString &group) const
    {
        for (int index = 1; index < 1000; ++index)
        {
            const QString candidate = QString("new_key_%1").arg(index);
            const auto rows = formRows_.value(group);
            if (std::none_of(rows.cbegin(), rows.cend(), [&candidate](const FormRow &row)
                             { return row.key == candidate; }))
                return candidate;
        }
        return {};
    }

    void PageParameters::refreshGroupLayout(const QString &group)
    {
        auto *form = groupLayouts_.value(group);
        if (!form)
            return;
        while (form->rowCount() > 0)
        {
            auto taken = form->takeRow(0);
            delete taken.labelItem;
            delete taken.fieldItem;
        }
        for (const FormRow &row : std::as_const(formRows_[group]))
            form->addRow(row.label, row.edit);
        if (group == selectedGroup_)
            selectRow(selectedRowIndex_);
    }

    void PageParameters::addRow()
    {
        const QString key = nextKey(selectedGroup_);
        if (key.isEmpty())
            return;
        formRows_[selectedGroup_].append(createRow(selectedGroup_, key, "value"));
        refreshGroupLayout(selectedGroup_);
    }

    void PageParameters::insertRow()
    {
        if (selectedRowIndex_ < 0)
            return;
        const QString key = nextKey(selectedGroup_);
        if (key.isEmpty())
            return;
        formRows_[selectedGroup_].insert(selectedRowIndex_, createRow(selectedGroup_, key, "value"));
        refreshGroupLayout(selectedGroup_);
    }

    void PageParameters::deleteRow()
    {
        auto &rows = formRows_[selectedGroup_];
        if (selectedRowIndex_ < 0 || selectedRowIndex_ >= rows.size())
            return;
        const FormRow removed = rows.takeAt(selectedRowIndex_);
        selectedRowIndex_ = std::min(selectedRowIndex_, static_cast<int>(rows.size()) - 1);
        refreshGroupLayout(selectedGroup_);
        delete removed.label;
        delete removed.edit;
    }

    void PageParameters::moveSelectedRow(int delta)
    {
        auto &rows = formRows_[selectedGroup_];
        const int target = selectedRowIndex_ + delta;
        if (selectedRowIndex_ < 0 || target < 0 || target >= rows.size())
            return;
        rows.swapItemsAt(selectedRowIndex_, target);
        selectedRowIndex_ = target;
        refreshGroupLayout(selectedGroup_);
    }

    void PageParameters::moveRowUp() { moveSelectedRow(-1); }
    void PageParameters::moveRowDown() { moveSelectedRow(1); }
}
