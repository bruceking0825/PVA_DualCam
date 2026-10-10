#include "page_parameters.hpp"
#include "config_manager.hpp"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <iostream>

namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void dismissMessage() {
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
    });
}
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const QString path = temp.filePath("cnf.ini");
    const auto writeConfig = [&](int percent) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(QString("[Neck]\nneck_gradient_threshold_percent_cam1 = [%1]\n"
            "neck_gradient_threshold_percent_cam2 = [40]\n\n[Body]\nbody_edge_min_coverage_ratio = [0.8]\n")
            .arg(percent).toUtf8()) > 0;
    };
    check(writeConfig(50), "Write temporary parameter fixture");
    auto &config = pva::ConfigManager::instance();
    config.load(path);
    pva::PageParameters page(path);
    page.show();
    auto *tabs = page.findChild<QTabWidget *>("tabWidget");
    check(tabs && tabs->count() == 2 && tabs->tabText(0) == "Neck" && tabs->tabText(1) == "Body",
        "Parameter groups retain file order");
    for (const char *name : {"btn_add", "btn_insert", "btn_up", "btn_down", "btn_delete"})
        check(page.findChild<QPushButton *>(name) == nullptr, "Row-management buttons are removed");
    const auto firstEditor = [&]() -> QLineEdit * {
        for (auto *edit : page.findChildren<QLineEdit *>())
            if (edit->property("key").toString() == "neck_gradient_threshold_percent_cam1") return edit;
        return nullptr;
    };
    auto *edit = firstEditor();
    auto *label = page.findChild<QLabel *>("neck_gradient_threshold_percent_cam1");
    check(edit && label && edit->text() == "50", "Parameter values load without brackets");
    if (!edit || !label || !tabs) return 1;
    auto *container = tabs->widget(0)->findChild<QWidget *>("formContainer");
    auto *form = container ? qobject_cast<QFormLayout *>(container->layout()) : nullptr;
    check(form && form->rowCount() == 2 &&
        form->itemAt(0, QFormLayout::FieldRole)->widget() == edit &&
        qobject_cast<QLabel *>(form->itemAt(1, QFormLayout::LabelRole)->widget())->text() ==
            "neck_gradient_threshold_percent_cam2", "Parameter rows retain file order");
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(2, 2),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(label, &doubleClick);
    check(label->text() == "neck_gradient_threshold_percent_cam1" &&
        page.findChild<QInputDialog *>() == nullptr, "Double-click cannot rename a parameter");
    edit->setText("30");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(edit, &enter);
    check(config.config().neck.gradientThresholdPercentCamera1 == 30, "Enter updates runtime value");
    int saves{};
    QObject::connect(&page, &pva::PageParameters::configurationSaved, &app, [&] { ++saves; });
    dismissMessage();
    check(QMetaObject::invokeMethod(&page, "save", Qt::DirectConnection), "Save remains callable");
    QFile saved(path);
    check(saved.open(QIODevice::ReadOnly), "Saved file opens");
    const auto content = saved.readAll(); saved.close();
    check(saves == 1 && content.contains("neck_gradient_threshold_percent_cam1 = [30]") &&
        content.indexOf("percent_cam1") < content.indexOf("percent_cam2") &&
        content.indexOf("[Neck]") < content.indexOf("[Body]"), "Save preserves keys, brackets and order");
    edit->setText("60");
    QApplication::sendEvent(edit, &enter);
    auto *cancel = page.findChild<QPushButton *>("btn_cancel_parm");
    check(cancel != nullptr, "Cancel remains present");
    if (cancel) cancel->click();
    check(firstEditor() && firstEditor()->text() == "30" &&
        config.config().neck.gradientThresholdPercentCamera1 == 30, "Cancel restores disk value and runtime");
    check(writeConfig(72), "Change temporary disk configuration");
    dismissMessage();
    check(QMetaObject::invokeMethod(&page, "load", Qt::DirectConnection), "Load remains callable");
    check(firstEditor() && firstEditor()->text() == "72" &&
        config.config().neck.gradientThresholdPercentCamera1 == 72, "Load refreshes form and runtime");
    return failures ? 1 : 0;
}
