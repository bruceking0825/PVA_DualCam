from PySide6.QtWidgets import (QWidget, QVBoxLayout, QScrollArea, QFormLayout, 
                              QPushButton, QLineEdit, QMessageBox, QInputDialog, 
                              QTabWidget, QApplication, QLabel)
from PySide6.QtCore import QFile, QTextStream, QObject, Signal, QIODevice
import sys
from .Ui_PageParameters import Ui_PageParameters  # 假设通过 pyside6-uic 生成的 UI 文件
from modules import Settings
from .utils import PressableLabel, IniEntry, FormRow
from .signals import signals
from .app_config import config_manager
from .base_page import BasePage

class PageParameters(BasePage):
    ui: Ui_PageParameters
    UI_CLASS = Ui_PageParameters

    def _init_state(self) -> None:
        self.filename = str(config_manager.path)
        self.groupLayouts = {}
        self.formRows = {}
        self.groupOrder = []
        self.orderedEntries = []
        self.selectedGroup = ""
        self.oldSelectedGroup = ""
        self.selectedRowIndex = -1

    def _setup_ui(self) -> None:
        pass

    def _bind_signals(self) -> None:
        signals.app_close.connect(self.on_close)
                
    def _bind_events(self) -> None:
        self.ui.btn_load_parm.clicked.connect(self.reload_settings)
        self.ui.btn_save_parm.clicked.connect(self.save_settings)
        self.ui.tabWidget.currentChanged.connect(lambda index: self.on_tab_changed(index))
        self.ui.btn_add.clicked.connect(lambda: self.add_key_value_row())
        self.ui.btn_insert.clicked.connect(lambda: self.insert_key_value_row())
        self.ui.btn_delete.clicked.connect(lambda: self.delete_key_value_row())
        self.ui.btn_up.clicked.connect(lambda: self.move_key_value_row(-1))
        self.ui.btn_down.clicked.connect(lambda: self.move_key_value_row(1))

    def _on_ready(self) -> None:
        self.load_settings()
        config_manager.batch_changed.emit()

    def reload_settings(self) -> None:
        try:
            config_manager.load(self.filename, emit_changes=True)
        except (OSError, TypeError, ValueError) as exc:
            QMessageBox.warning(self, "错误", f"无法加载配置文件: {exc}")
            return
        self.load_settings()

    def load_settings(self):
        self.parse_init_file(self.filename)
        self.ui.tabWidget.clear()
        self.groupLayouts.clear()
        self.formRows.clear()
        self.groupOrder.clear()

        for entry in self.orderedEntries:
            if entry.group not in self.groupLayouts:
                page = QWidget()
                outerLayout = QVBoxLayout(page)
                scroll = QScrollArea()
                scroll.setWidgetResizable(True)
                scroll.setStyleSheet("QScrollArea { border: none; }")
                content = QWidget()
                content.setObjectName("formContainer")  # 设置对象名称，方便样式表选择
                form = QFormLayout(content)
                # 设置样式表
                content.setStyleSheet(Settings.PARAMTER_FORM_STYLE)
                scroll.setWidget(content)
                outerLayout.addWidget(scroll)
                self.ui.tabWidget.addTab(page, entry.group)
                self.ui.tabWidget.currentChanged.connect(lambda index, g=entry.group: 
                    setattr(self, "selectedGroup", self.ui.tabWidget.tabText(index)))
                self.groupLayouts[entry.group] = form
                self.groupOrder.append(entry.group)

            # label = PressableLabel(entry.key)
            # label.setObjectName(entry.key)
            # edit = QLineEdit(entry.value)
            # self.bind_label_events(label)
            # self.bind_edit_events(edit, entry)
            self.formRows.setdefault(entry.group, []).append(self.createFormRow(entry.group, entry.key, entry.value))
            self.refresh_group_layout(entry.group)

    def on_tab_changed(self, index):
        self.oldSelectedGroup = self.selectedGroup
        self.selectedGroup = self.ui.tabWidget.tabText(index)
        self.selectedRowIndex = -1
        self.clear_old_group_style()
        for i in range(self.ui.tabWidget.count()):
            self.ui.tabWidget.tabBar().setStyleSheet("")
        self.ui.tabWidget.tabBar().setStyleSheet(Settings.WIDGET_TAB_STYLE)  # 示例样式

    def bind_label_events(self, label:PressableLabel):
        label.clicked.connect(lambda: self._onLabelClicked(label))
        label.doubleClicked.connect(lambda: self._on_label_double_clicked(label))

    def bind_edit_events(self, edit:QLineEdit, entry:IniEntry):
        edit.returnPressed.connect(lambda: self._apply_entry(IniEntry(entry.group, entry.key, edit.text())))

    def _apply_entry(self, entry: IniEntry) -> bool:
        try:
            return config_manager.set_entry(entry)
        except (TypeError, ValueError) as exc:
            QMessageBox.warning(self, "参数错误", f"{entry.group}.{entry.key}: {exc}")
            return False

    def _onLabelClicked(self, label):
        self.selectedRowIndex = self.find_row_index(self.selectedGroup, label.objectName())
        self.hightlight_selected_row()

    def _on_label_double_clicked(self, label):
        self.selectedRowIndex = self.find_row_index(self.selectedGroup, label.objectName())
        self.hightlight_selected_row()
        newText, ok = QInputDialog.getText(self, "修改标签", "<span style='color: green; font-weight: bold;'>请输入新的标签名:</span>", QLineEdit.Normal, label.text())
        layout = self.groupLayouts.get(self.selectedGroup)
        if not layout:
            return
        # 遍历布局中的子项检查是否已存在同名标签
        has_duplicate = False
        for i in range(layout.rowCount()):
            label_item = layout.itemAt(i, QFormLayout.LabelRole)
            if label_item and label_item.widget():
                if label_item.widget().objectName() == newText and label_item.widget() != label:
                    has_duplicate = True
                    break
        if has_duplicate:
            QMessageBox.warning(self, "错误", "<span style='color: green; font-weight: bold;'>标签名已存在，请输入一个唯一的标签名。</span>")
            return
        if ok and newText:
            label.setText(newText)
            label.setObjectName(newText)
            self.formRows[self.selectedGroup][self.selectedRowIndex].key = newText
            self.formRows[self.selectedGroup][self.selectedRowIndex].label = label

    def createFormRow(self, group, key, value):
        form = self.groupLayouts.get(group)
        if not form:
            return FormRow()
        rowWidget = QWidget()
        rowLayout = QFormLayout(rowWidget)
        label = PressableLabel(key, self)
        label.setObjectName(key)
        edit = QLineEdit(value, self)
        rowLayout.addRow(label, edit)
        self.bind_label_events(label)
        entry = IniEntry(group, key, value)
        self.bind_edit_events(edit, entry)
        return FormRow(rowWidget, label, edit, key)

    def parse_init_file(self, filepath):
        self.orderedEntries.clear()
        file = QFile(filepath)
        if not file.open(QIODevice.ReadOnly | QIODevice.Text):
            QMessageBox.warning(self, "错误", "<span style='color: green; font-weight: bold;'>无法打开 ini 文件</span>")
            return
        in_stream = QTextStream(file)
        currentGroup = ""

        while not in_stream.atEnd():
            line = in_stream.readLine()  # Returns a QString
            line = line.strip()  # Convert to str and remove whitespace
            if line.startswith(";") or not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                currentGroup = line[1:-1].strip()  # Extract group name and strip
            else:
                eqIndex = line.find('=')  # Use find instead of indexOf
                if eqIndex != -1:
                    key = line[:eqIndex].strip()
                    value = line[eqIndex + 1:].strip()
                    value = value.replace("[", "").replace("]", "")  # Remove brackets
                    self.orderedEntries.append(IniEntry(currentGroup, key, value))
        file.close()

    def update_params(self):
        # 收集所有 QLineEdit 的当前值并更新 ComparisonWorker
        for group in self.groupOrder:
            if group not in self.formRows:
                continue
            for row in self.formRows[group]:
                value = row.edit.text()
                entry = IniEntry(group, row.key, value)
                self._apply_entry(entry)
        config_manager.batch_changed.emit()

    def save_settings(self):
        self.update_params()
        file = QFile(self.filename)
        if not file.open(QIODevice.WriteOnly | QIODevice.Text):
            QMessageBox.warning(self, "错误", "<span style='color: green; font-weight: bold;'>无法写入 ini 文件</span>")
            return
        out_stream = QTextStream(file)
        lastGroup = ""
        for group in self.groupOrder:
            if group not in self.formRows:
                continue
            if group != lastGroup:
                if lastGroup:
                    out_stream << "\n"
                out_stream << "[" << group << "]\n"
                lastGroup = group
            for row in self.formRows[group]:
                value = row.edit.text()
                out_stream << row.key << " = [" << value << "]\n"
        file.close()
        QMessageBox.information(self, "成功", "<span style='color: green; font-weight: bold;'>设置已保存</span>")

    def find_row_index(self, group, key):
        if group not in self.formRows:
            return -1
        rows = self.formRows[group]
        for i, row in enumerate(rows):
            if row.key == key:
                return i
        return -1

    def hightlight_selected_row(self):
        if self.selectedGroup not in self.formRows:
            return
        self.selectedRowIndex = min(self.selectedRowIndex, len(self.formRows[self.selectedGroup]) - 1) if self.selectedRowIndex >= 0 else -1
        if self.selectedRowIndex < 0:
            return
        self.clear_old_group_style()
        form = self.groupLayouts.get(self.selectedGroup)
        if not form:
            return
        labelItem = form.itemAt(self.selectedRowIndex, QFormLayout.LabelRole)
        if labelItem:
            labelWidget = labelItem.widget()
            if labelWidget:
                labelWidget.setStyleSheet(Settings.LABEL_SELECTED_STYLE)  # 示例高亮样式

    def clear_old_group_style(self):
        form = self.groupLayouts.get(self.selectedGroup)
        if not form:
            return
        for i in range(form.rowCount()):
            labelItem = form.itemAt(i, QFormLayout.LabelRole)
            if labelItem:
                labelWidget = labelItem.widget()
                if labelWidget:
                    labelWidget.setStyleSheet("")

    def refresh_group_layout(self, group):
        form = self.groupLayouts.get(group)
        if not form:
            return
        for row in self.formRows.get(group, []):
            if row.label:
                row.label.setParent(None)
            if row.edit:
                row.edit.setParent(None)
        for i in range(form.rowCount() - 1, -1, -1):
            form.removeRow(i)
        for row in self.formRows.get(group, []):
            form.addRow(row.label, row.edit)
        for row in self.formRows.get(group, []):
            if row.label:
                row.label.setParent(form.parentWidget())
            if row.edit:
                row.edit.setParent(form.parentWidget())
        self.selectedRowIndex = min(self.selectedRowIndex, form.rowCount() - 1)
        if 0 <= self.selectedRowIndex < form.rowCount():
            self.hightlight_selected_row()
        else:
            self.selectedRowIndex = -1

    def add_key_value_row(self):
        for i in range(1, 1000):  # 防止无限循环
            newKey = f"new_key_{i}"
            if not any(row.key == newKey for row in self.formRows.get(self.selectedGroup, [])):
                row = self.createFormRow(self.selectedGroup, newKey, "value")
                self.formRows.setdefault(self.selectedGroup, []).append(row)
                self.refresh_group_layout(self.selectedGroup)
                break

    def insert_key_value_row(self):
        if self.selectedRowIndex < 0:
            return
        for i in range(1, 1000):
            newKey = f"new_key_{i}"
            if not any(row.key == newKey for row in self.formRows.get(self.selectedGroup, [])):
                row = self.createFormRow(self.selectedGroup, newKey, "value")
                if self.selectedRowIndex >= len(self.formRows[self.selectedGroup]):
                    self.formRows[self.selectedGroup].append(row)
                else:
                    self.formRows[self.selectedGroup].insert(self.selectedRowIndex, row)
                self.refresh_group_layout(self.selectedGroup)
                break

    def delete_key_value_row(self):
        if self.selectedRowIndex < 0:
            return
        self.selectedRowIndex = min(self.selectedRowIndex, len(self.formRows.get(self.selectedGroup, [])) - 1)
        if self.selectedGroup in self.formRows and 0 <= self.selectedRowIndex < len(self.formRows[self.selectedGroup]):
            del self.formRows[self.selectedGroup][self.selectedRowIndex]
        self.refresh_group_layout(self.selectedGroup)

    def move_key_value_row(self, direction):
        if self.selectedRowIndex < 0 or self.selectedRowIndex >= len(self.formRows.get(self.selectedGroup, [])):
            return
        if (direction == -1 and self.selectedRowIndex == 0) or (direction == 1 and self.selectedRowIndex == len(self.formRows[self.selectedGroup]) - 1):
            return
        targetIndex = self.selectedRowIndex + direction
        if self.selectedGroup in self.formRows and 0 <= targetIndex < len(self.formRows[self.selectedGroup]):
            self.formRows[self.selectedGroup][self.selectedRowIndex], self.formRows[self.selectedGroup][targetIndex] = (
                self.formRows[self.selectedGroup][targetIndex], self.formRows[self.selectedGroup][self.selectedRowIndex]
            )
            self.selectedRowIndex = targetIndex
        self.refresh_group_layout(self.selectedGroup)

    def on_close(self):
        self.close()
        print("Page parameters closed")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = PageParameters()
    window.show()
    sys.exit(app.exec()) 
