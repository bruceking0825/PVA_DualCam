from PySide6.QtWidgets import (
    QDialog, QFormLayout, QLineEdit, QPushButton, QVBoxLayout
)


class ParamDialog(QDialog):
    def __init__(self, node):
        super().__init__()

        self.node = node
        self.setWindowTitle(node.name)

        self.inputs = {}

        form_layout = QFormLayout()

        params = node.get_params()

        for key, info in params.items():
            line = QLineEdit(str(info["value"]))
            form_layout.addRow(key, line)
            self.inputs[key] = line

        btn_ok = QPushButton("OK")
        btn_ok.clicked.connect(self.apply)

        layout = QVBoxLayout()
        layout.addLayout(form_layout)
        layout.addWidget(btn_ok)

        self.setLayout(layout)

    def apply(self):
        for key, widget in self.inputs.items():
            text = widget.text()

            param_type = self.node.get_params()[key]["type"]

            try:
                value = param_type(text)
                self.node.set_param(key, value)
            except:
                print(f"Invalid value for {key}")

        self.accept()