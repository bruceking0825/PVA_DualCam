# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'PageParameters.ui'
##
## Created by: Qt User Interface Compiler version 6.10.2
##
## WARNING! All changes made in this file will be lost when recompiling UI file!
################################################################################

from PySide6.QtCore import (QCoreApplication, QDate, QDateTime, QLocale,
    QMetaObject, QObject, QPoint, QRect,
    QSize, QTime, QUrl, Qt)
from PySide6.QtGui import (QBrush, QColor, QConicalGradient, QCursor,
    QFont, QFontDatabase, QGradient, QIcon,
    QImage, QKeySequence, QLinearGradient, QPainter,
    QPalette, QPixmap, QRadialGradient, QTransform)
from PySide6.QtWidgets import (QApplication, QHBoxLayout, QPushButton, QSizePolicy,
    QSpacerItem, QTabWidget, QVBoxLayout, QWidget)

class Ui_PageParameters(object):
    def setupUi(self, PageParameters):
        if not PageParameters.objectName():
            PageParameters.setObjectName(u"PageParameters")
        PageParameters.resize(663, 530)
        PageParameters.setStyleSheet(u"#PageParameters {	\n"
"	background-color: rgb(40, 44, 52);\n"
"	/*border: 1px solid rgb(44, 49, 58);*/\n"
"}")
        self.verticalLayout = QVBoxLayout(PageParameters)
        self.verticalLayout.setObjectName(u"verticalLayout")
        self.verticalLayout_22 = QVBoxLayout()
        self.verticalLayout_22.setObjectName(u"verticalLayout_22")
        self.horizontalLayout_7 = QHBoxLayout()
        self.horizontalLayout_7.setObjectName(u"horizontalLayout_7")
        self.tabWidget = QTabWidget(PageParameters)
        self.tabWidget.setObjectName(u"tabWidget")
        self.tabWidget.setMaximumSize(QSize(16777215, 16777215))
        self.tabWidget.setStyleSheet(u"")

        self.horizontalLayout_7.addWidget(self.tabWidget)

        self.verticalLayout_21 = QVBoxLayout()
        self.verticalLayout_21.setSpacing(12)
        self.verticalLayout_21.setObjectName(u"verticalLayout_21")
        self.verticalSpacer_3 = QSpacerItem(20, 40, QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)

        self.verticalLayout_21.addItem(self.verticalSpacer_3)

        self.btn_add = QPushButton(PageParameters)
        self.btn_add.setObjectName(u"btn_add")

        self.verticalLayout_21.addWidget(self.btn_add)

        self.btn_insert = QPushButton(PageParameters)
        self.btn_insert.setObjectName(u"btn_insert")

        self.verticalLayout_21.addWidget(self.btn_insert)

        self.btn_up = QPushButton(PageParameters)
        self.btn_up.setObjectName(u"btn_up")

        self.verticalLayout_21.addWidget(self.btn_up)

        self.btn_down = QPushButton(PageParameters)
        self.btn_down.setObjectName(u"btn_down")

        self.verticalLayout_21.addWidget(self.btn_down)

        self.btn_delete = QPushButton(PageParameters)
        self.btn_delete.setObjectName(u"btn_delete")

        self.verticalLayout_21.addWidget(self.btn_delete)

        self.verticalSpacer_2 = QSpacerItem(20, 40, QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)

        self.verticalLayout_21.addItem(self.verticalSpacer_2)


        self.horizontalLayout_7.addLayout(self.verticalLayout_21)


        self.verticalLayout_22.addLayout(self.horizontalLayout_7)

        self.horizontalLayout_10 = QHBoxLayout()
        self.horizontalLayout_10.setObjectName(u"horizontalLayout_10")
        self.horizontalSpacer = QSpacerItem(40, 20, QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Minimum)

        self.horizontalLayout_10.addItem(self.horizontalSpacer)

        self.btn_load_parm = QPushButton(PageParameters)
        self.btn_load_parm.setObjectName(u"btn_load_parm")

        self.horizontalLayout_10.addWidget(self.btn_load_parm)

        self.btn_save_parm = QPushButton(PageParameters)
        self.btn_save_parm.setObjectName(u"btn_save_parm")

        self.horizontalLayout_10.addWidget(self.btn_save_parm)

        self.btn_cancel_parm = QPushButton(PageParameters)
        self.btn_cancel_parm.setObjectName(u"btn_cancel_parm")

        self.horizontalLayout_10.addWidget(self.btn_cancel_parm)


        self.verticalLayout_22.addLayout(self.horizontalLayout_10)


        self.verticalLayout.addLayout(self.verticalLayout_22)


        self.retranslateUi(PageParameters)

        self.tabWidget.setCurrentIndex(-1)


        QMetaObject.connectSlotsByName(PageParameters)
    # setupUi

    def retranslateUi(self, PageParameters):
        PageParameters.setWindowTitle(QCoreApplication.translate("PageParameters", u"Form", None))
        self.btn_add.setText(QCoreApplication.translate("PageParameters", u"Add", None))
        self.btn_insert.setText(QCoreApplication.translate("PageParameters", u"Insert", None))
        self.btn_up.setText(QCoreApplication.translate("PageParameters", u"Up", None))
        self.btn_down.setText(QCoreApplication.translate("PageParameters", u"Down", None))
        self.btn_delete.setText(QCoreApplication.translate("PageParameters", u"Delete", None))
        self.btn_load_parm.setText(QCoreApplication.translate("PageParameters", u"Load", None))
        self.btn_save_parm.setText(QCoreApplication.translate("PageParameters", u"Save", None))
        self.btn_cancel_parm.setText(QCoreApplication.translate("PageParameters", u"Cancel", None))
    # retranslateUi

