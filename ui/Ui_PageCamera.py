# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'PageCamera.ui'
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
from PySide6.QtWidgets import (QApplication, QComboBox, QFormLayout, QFrame,
    QGridLayout, QHBoxLayout, QLabel, QLayout,
    QLineEdit, QPushButton, QSizePolicy, QSplitter,
    QVBoxLayout, QWidget)

from custom_graphics_view import CustomGraphicsView
import resources_rc

class Ui_PageCamera(object):
    def setupUi(self, PageCamera):
        if not PageCamera.objectName():
            PageCamera.setObjectName(u"PageCamera")
        PageCamera.resize(868, 655)
        PageCamera.setStyleSheet(u"#PageCamera {	\n"
"	background-color: rgb(40, 44, 52);\n"
"	/*border: 1px solid rgb(44, 49, 58);*/\n"
"}\n"
"/*#graphicsView{\n"
"	border: 2px solid rgb(70, 80, 110);\n"
"}*/\n"
"#frame_2\n"
"{\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"	border-left: 2px solid rgb(70, 80, 110);\n"
"	border-bottom: 0px solid rgb(70, 80, 110);\n"
"	border-right: 0px solid rgb(70, 80, 110);\n"
"}\n"
"#frame_3\n"
"{\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"	border-left: 0px solid rgb(70, 80, 110);\n"
"	border-bottom: 0px solid rgb(70, 80, 110);\n"
"	border-right: 2px solid rgb(70, 80, 110);\n"
"}\n"
"#frame_4\n"
"{\n"
"    border-top: 0px solid rgb(70, 80, 110);\n"
"	border-left: 2px solid rgb(70, 80, 110);\n"
"	border-bottom: 2px solid rgb(70, 80, 110);\n"
"	border-right: 0px solid rgb(70, 80, 110);\n"
"}\n"
"#frame\n"
"{\n"
"    border-top: 0px solid rgb(70, 80, 110);\n"
"	border-left: 0px solid rgb(70, 80, 110);\n"
"	border-bottom: 2px solid rgb(70, 80, 110);\n"
"	border-right: 2px solid rgb(70, 80, 110);\n"
""
                        "}\n"
"#frame_6\n"
"{\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"	border-left: 2px solid rgb(70, 80, 110);\n"
"	border-bottom: 2px solid rgb(70, 80, 110);\n"
"	border-right: 2px solid rgb(70, 80, 110);\n"
"}\n"
"")
        self.horizontalLayout = QHBoxLayout(PageCamera)
        self.horizontalLayout.setSpacing(3)
        self.horizontalLayout.setObjectName(u"horizontalLayout")
        self.splitter_3 = QSplitter(PageCamera)
        self.splitter_3.setObjectName(u"splitter_3")
        self.splitter_3.setOrientation(Qt.Orientation.Vertical)
        self.splitter_3.setHandleWidth(1)
        self.splitter = QSplitter(self.splitter_3)
        self.splitter.setObjectName(u"splitter")
        self.splitter.setOrientation(Qt.Orientation.Horizontal)
        self.splitter.setHandleWidth(1)
        self.frame_2 = QFrame(self.splitter)
        self.frame_2.setObjectName(u"frame_2")
        self.frame_2.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_2.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayout_2 = QGridLayout(self.frame_2)
        self.gridLayout_2.setSpacing(0)
        self.gridLayout_2.setObjectName(u"gridLayout_2")
        self.gridLayout_2.setContentsMargins(0, 0, 0, 0)
        self.btnOrgFullFill = QPushButton(self.frame_2)
        self.btnOrgFullFill.setObjectName(u"btnOrgFullFill")

        self.gridLayout_2.addWidget(self.btnOrgFullFill, 0, 1, 1, 1)

        self.label_12 = QLabel(self.frame_2)
        self.label_12.setObjectName(u"label_12")

        self.gridLayout_2.addWidget(self.label_12, 0, 0, 1, 1)

        self.lblOrgInfo = QLabel(self.frame_2)
        self.lblOrgInfo.setObjectName(u"lblOrgInfo")

        self.gridLayout_2.addWidget(self.lblOrgInfo, 2, 0, 1, 1)

        self.btnOrgOpen = QPushButton(self.frame_2)
        self.btnOrgOpen.setObjectName(u"btnOrgOpen")

        self.gridLayout_2.addWidget(self.btnOrgOpen, 0, 2, 1, 1)

        self.orgGraphicsView = CustomGraphicsView(self.frame_2)
        self.orgGraphicsView.setObjectName(u"orgGraphicsView")

        self.gridLayout_2.addWidget(self.orgGraphicsView, 1, 0, 1, 3)

        self.splitter.addWidget(self.frame_2)
        self.frame_3 = QFrame(self.splitter)
        self.frame_3.setObjectName(u"frame_3")
        self.frame_3.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_3.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayout_3 = QGridLayout(self.frame_3)
        self.gridLayout_3.setSpacing(0)
        self.gridLayout_3.setObjectName(u"gridLayout_3")
        self.gridLayout_3.setContentsMargins(0, 0, 0, 0)
        self.label_13 = QLabel(self.frame_3)
        self.label_13.setObjectName(u"label_13")

        self.gridLayout_3.addWidget(self.label_13, 0, 0, 1, 1)

        self.btnTransformedFullFill = QPushButton(self.frame_3)
        self.btnTransformedFullFill.setObjectName(u"btnTransformedFullFill")

        self.gridLayout_3.addWidget(self.btnTransformedFullFill, 0, 1, 1, 1)

        self.transGraphicsView = CustomGraphicsView(self.frame_3)
        self.transGraphicsView.setObjectName(u"transGraphicsView")

        self.gridLayout_3.addWidget(self.transGraphicsView, 1, 0, 1, 2)

        self.lblTransformedInfo = QLabel(self.frame_3)
        self.lblTransformedInfo.setObjectName(u"lblTransformedInfo")

        self.gridLayout_3.addWidget(self.lblTransformedInfo, 2, 0, 1, 1)

        self.splitter.addWidget(self.frame_3)
        self.splitter_3.addWidget(self.splitter)
        self.splitter_2 = QSplitter(self.splitter_3)
        self.splitter_2.setObjectName(u"splitter_2")
        self.splitter_2.setOrientation(Qt.Orientation.Horizontal)
        self.splitter_2.setHandleWidth(1)
        self.frame_4 = QFrame(self.splitter_2)
        self.frame_4.setObjectName(u"frame_4")
        self.frame_4.setMaximumSize(QSize(120, 16777215))
        self.frame_4.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_4.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayout_4 = QGridLayout(self.frame_4)
        self.gridLayout_4.setSpacing(0)
        self.gridLayout_4.setObjectName(u"gridLayout_4")
        self.gridLayout_4.setContentsMargins(0, 0, 0, 0)
        self.QVBtoolBox = QVBoxLayout()
        self.QVBtoolBox.setObjectName(u"QVBtoolBox")
        self.QVBtoolBox.setContentsMargins(-1, 6, -1, -1)

        self.gridLayout_4.addLayout(self.QVBtoolBox, 1, 1, 1, 1)

        self.label_15 = QLabel(self.frame_4)
        self.label_15.setObjectName(u"label_15")
        self.label_15.setMaximumSize(QSize(16777215, 20))
        font = QFont()
        font.setBold(True)
        self.label_15.setFont(font)
        self.label_15.setStyleSheet(u"background-color: rgb(33, 37, 43);")
        self.label_15.setMargin(3)

        self.gridLayout_4.addWidget(self.label_15, 0, 1, 1, 1)

        self.splitter_2.addWidget(self.frame_4)
        self.frame = QFrame(self.splitter_2)
        self.frame.setObjectName(u"frame")
        self.frame.setStyleSheet(u"")
        self.frame.setFrameShape(QFrame.Shape.NoFrame)
        self.frame.setFrameShadow(QFrame.Shadow.Raised)
        self.frame.setMidLineWidth(0)
        self.gridLayout = QGridLayout(self.frame)
        self.gridLayout.setObjectName(u"gridLayout")
        self.gridLayout.setHorizontalSpacing(0)
        self.gridLayout.setVerticalSpacing(6)
        self.gridLayout.setContentsMargins(0, 0, 0, 0)
        self.QHBView = QHBoxLayout()
        self.QHBView.setSpacing(0)
        self.QHBView.setObjectName(u"QHBView")

        self.gridLayout.addLayout(self.QHBView, 0, 2, 5, 2)

        self.btnSave = QPushButton(self.frame)
        self.btnSave.setObjectName(u"btnSave")
        self.btnSave.setMaximumSize(QSize(50, 16777215))

        self.gridLayout.addWidget(self.btnSave, 3, 4, 1, 1)

        self.btnLoad = QPushButton(self.frame)
        self.btnLoad.setObjectName(u"btnLoad")
        self.btnLoad.setMaximumSize(QSize(50, 16777215))

        self.gridLayout.addWidget(self.btnLoad, 2, 4, 1, 1)

        self.btnRun = QPushButton(self.frame)
        self.btnRun.setObjectName(u"btnRun")
        self.btnRun.setMaximumSize(QSize(50, 16777215))

        self.gridLayout.addWidget(self.btnRun, 0, 4, 1, 1)

        self.btnConfig = QPushButton(self.frame)
        self.btnConfig.setObjectName(u"btnConfig")
        self.btnConfig.setMaximumSize(QSize(50, 16777215))

        self.gridLayout.addWidget(self.btnConfig, 1, 4, 1, 1)

        self.splitter_2.addWidget(self.frame)
        self.splitter_3.addWidget(self.splitter_2)

        self.horizontalLayout.addWidget(self.splitter_3)

        self.frame_6 = QFrame(PageCamera)
        self.frame_6.setObjectName(u"frame_6")
        self.frame_6.setMaximumSize(QSize(200, 16777215))
        self.frame_6.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_6.setFrameShadow(QFrame.Shadow.Raised)
        self.verticalLayout = QVBoxLayout(self.frame_6)
        self.verticalLayout.setObjectName(u"verticalLayout")
        self.verticalLayout.setContentsMargins(0, 0, 0, 0)
        self.frame_5 = QFrame(self.frame_6)
        self.frame_5.setObjectName(u"frame_5")
        self.frame_5.setMinimumSize(QSize(0, 300))
        self.frame_5.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_5.setFrameShadow(QFrame.Shadow.Raised)
        self.verticalLayout_2 = QVBoxLayout(self.frame_5)
        self.verticalLayout_2.setSpacing(0)
        self.verticalLayout_2.setObjectName(u"verticalLayout_2")
        self.verticalLayout_2.setSizeConstraint(QLayout.SizeConstraint.SetNoConstraint)
        self.verticalLayout_2.setContentsMargins(0, 0, 0, 0)
        self.label_16 = QLabel(self.frame_5)
        self.label_16.setObjectName(u"label_16")
        self.label_16.setMaximumSize(QSize(16777215, 20))
        self.label_16.setFont(font)
        self.label_16.setStyleSheet(u"background-color:  rgb(33, 37, 43);\n"
"\n"
"border-top: 0px solid rgb(70, 80, 110);\n"
"border-left: 0px solid rgb(70, 80, 110);\n"
"border-bottom: 0px solid rgb(70, 80, 110);\n"
"border-right: 0px solid rgb(70, 80, 110);\n"
"")
        self.label_16.setMargin(3)

        self.verticalLayout_2.addWidget(self.label_16)

        self.frame_7 = QFrame(self.frame_5)
        self.frame_7.setObjectName(u"frame_7")
        sizePolicy = QSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Preferred)
        sizePolicy.setHorizontalStretch(0)
        sizePolicy.setVerticalStretch(0)
        sizePolicy.setHeightForWidth(self.frame_7.sizePolicy().hasHeightForWidth())
        self.frame_7.setSizePolicy(sizePolicy)
        self.frame_7.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_7.setFrameShadow(QFrame.Shadow.Raised)
        self.horizontalLayout_3 = QHBoxLayout(self.frame_7)
        self.horizontalLayout_3.setSpacing(6)
        self.horizontalLayout_3.setObjectName(u"horizontalLayout_3")
        self.horizontalLayout_3.setSizeConstraint(QLayout.SizeConstraint.SetDefaultConstraint)
        self.horizontalLayout_3.setContentsMargins(9, 9, 9, 9)
        self.lblStatus = QLabel(self.frame_7)
        self.lblStatus.setObjectName(u"lblStatus")
        self.lblStatus.setMaximumSize(QSize(24, 24))
        self.lblStatus.setStyleSheet(u"")
        self.lblStatus.setPixmap(QPixmap(u":/images/images/images/ledHigh.png"))
        self.lblStatus.setScaledContents(True)

        self.horizontalLayout_3.addWidget(self.lblStatus)

        self.combCameraList = QComboBox(self.frame_7)
        self.combCameraList.setObjectName(u"combCameraList")
        self.combCameraList.setMinimumSize(QSize(0, 30))

        self.horizontalLayout_3.addWidget(self.combCameraList)

        self.btnRefresh = QPushButton(self.frame_7)
        self.btnRefresh.setObjectName(u"btnRefresh")
        self.btnRefresh.setMaximumSize(QSize(30, 30))
        icon = QIcon(QIcon.fromTheme(QIcon.ThemeIcon.SystemReboot))
        self.btnRefresh.setIcon(icon)

        self.horizontalLayout_3.addWidget(self.btnRefresh)


        self.verticalLayout_2.addWidget(self.frame_7)

        self.label_17 = QLabel(self.frame_5)
        self.label_17.setObjectName(u"label_17")
        self.label_17.setMaximumSize(QSize(16777215, 20))
        self.label_17.setFont(font)
        self.label_17.setStyleSheet(u"background-color: rgb(33, 37, 43);\n"
"border-top: 2px solid rgb(70, 80, 110);\n"
"border-left: 0px solid rgb(70, 80, 110);\n"
"border-bottom: 0px solid rgb(70, 80, 110);\n"
"border-right: 0px solid rgb(70, 80, 110);\n"
"")
        self.label_17.setMargin(3)

        self.verticalLayout_2.addWidget(self.label_17)

        self.frame_8 = QFrame(self.frame_5)
        self.frame_8.setObjectName(u"frame_8")
        self.frame_8.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_8.setFrameShadow(QFrame.Shadow.Raised)
        self.horizontalLayout_2 = QHBoxLayout(self.frame_8)
        self.horizontalLayout_2.setObjectName(u"horizontalLayout_2")
        self.btnCamON = QPushButton(self.frame_8)
        self.btnCamON.setObjectName(u"btnCamON")
        self.btnCamON.setCheckable(True)
        self.btnCamON.setChecked(False)

        self.horizontalLayout_2.addWidget(self.btnCamON)

        self.btnStartSnap = QPushButton(self.frame_8)
        self.btnStartSnap.setObjectName(u"btnStartSnap")
        self.btnStartSnap.setCheckable(True)
        self.btnStartSnap.setChecked(False)

        self.horizontalLayout_2.addWidget(self.btnStartSnap)


        self.verticalLayout_2.addWidget(self.frame_8)

        self.label_18 = QLabel(self.frame_5)
        self.label_18.setObjectName(u"label_18")
        self.label_18.setMaximumSize(QSize(16777215, 20))
        self.label_18.setFont(font)
        self.label_18.setStyleSheet(u"background-color: rgb(33, 37, 43);\n"
"border-top: 2px solid rgb(70, 80, 110);\n"
"border-left: 0px solid rgb(70, 80, 110);\n"
"border-bottom: 0px solid rgb(70, 80, 110);\n"
"border-right: 0px solid rgb(70, 80, 110);\n"
"")
        self.label_18.setMargin(3)

        self.verticalLayout_2.addWidget(self.label_18)

        self.frame_9 = QFrame(self.frame_5)
        self.frame_9.setObjectName(u"frame_9")
        self.frame_9.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_9.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayout_5 = QGridLayout(self.frame_9)
        self.gridLayout_5.setObjectName(u"gridLayout_5")
        self.gridLayout_5.setVerticalSpacing(12)
        self.label = QLabel(self.frame_9)
        self.label.setObjectName(u"label")

        self.gridLayout_5.addWidget(self.label, 1, 0, 1, 1)

        self.combTrigMode = QComboBox(self.frame_9)
        self.combTrigMode.setObjectName(u"combTrigMode")

        self.gridLayout_5.addWidget(self.combTrigMode, 1, 1, 1, 1)

        self.label_5 = QLabel(self.frame_9)
        self.label_5.setObjectName(u"label_5")

        self.gridLayout_5.addWidget(self.label_5, 2, 0, 1, 1)

        self.combTrigSource = QComboBox(self.frame_9)
        self.combTrigSource.setObjectName(u"combTrigSource")

        self.gridLayout_5.addWidget(self.combTrigSource, 2, 1, 1, 1)

        self.label_6 = QLabel(self.frame_9)
        self.label_6.setObjectName(u"label_6")

        self.gridLayout_5.addWidget(self.label_6, 3, 0, 1, 1)

        self.btnSoftTrigger = QPushButton(self.frame_9)
        self.btnSoftTrigger.setObjectName(u"btnSoftTrigger")

        self.gridLayout_5.addWidget(self.btnSoftTrigger, 3, 1, 1, 1)

        self.label_7 = QLabel(self.frame_9)
        self.label_7.setObjectName(u"label_7")

        self.gridLayout_5.addWidget(self.label_7, 4, 0, 1, 1)

        self.combTrigEdge = QComboBox(self.frame_9)
        self.combTrigEdge.setObjectName(u"combTrigEdge")

        self.gridLayout_5.addWidget(self.combTrigEdge, 4, 1, 1, 1)


        self.verticalLayout_2.addWidget(self.frame_9)

        self.label_19 = QLabel(self.frame_5)
        self.label_19.setObjectName(u"label_19")
        self.label_19.setMaximumSize(QSize(16777215, 20))
        self.label_19.setFont(font)
        self.label_19.setStyleSheet(u"background-color: rgb(33, 37, 43);\n"
"border-top: 2px solid rgb(70, 80, 110);\n"
"border-left: 0px solid rgb(70, 80, 110);\n"
"border-bottom: 0px solid rgb(70, 80, 110);\n"
"border-right: 0px solid rgb(70, 80, 110);")
        self.label_19.setMargin(3)

        self.verticalLayout_2.addWidget(self.label_19)

        self.frame_10 = QFrame(self.frame_5)
        self.frame_10.setObjectName(u"frame_10")
        self.frame_10.setFrameShape(QFrame.Shape.StyledPanel)
        self.frame_10.setFrameShadow(QFrame.Shadow.Raised)
        self.formLayout = QFormLayout(self.frame_10)
        self.formLayout.setObjectName(u"formLayout")
        self.formLayout.setVerticalSpacing(12)
        self.lblExposure = QLabel(self.frame_10)
        self.lblExposure.setObjectName(u"lblExposure")
        self.lblExposure.setWordWrap(True)

        self.formLayout.setWidget(0, QFormLayout.ItemRole.LabelRole, self.lblExposure)

        self.edtExposure = QLineEdit(self.frame_10)
        self.edtExposure.setObjectName(u"edtExposure")

        self.formLayout.setWidget(0, QFormLayout.ItemRole.FieldRole, self.edtExposure)

        self.lblGain = QLabel(self.frame_10)
        self.lblGain.setObjectName(u"lblGain")
        self.lblGain.setWordWrap(True)

        self.formLayout.setWidget(1, QFormLayout.ItemRole.LabelRole, self.lblGain)

        self.edtGain = QLineEdit(self.frame_10)
        self.edtGain.setObjectName(u"edtGain")

        self.formLayout.setWidget(1, QFormLayout.ItemRole.FieldRole, self.edtGain)

        self.lblWidth = QLabel(self.frame_10)
        self.lblWidth.setObjectName(u"lblWidth")
        self.lblWidth.setWordWrap(True)

        self.formLayout.setWidget(2, QFormLayout.ItemRole.LabelRole, self.lblWidth)

        self.edtWidth = QLineEdit(self.frame_10)
        self.edtWidth.setObjectName(u"edtWidth")

        self.formLayout.setWidget(2, QFormLayout.ItemRole.FieldRole, self.edtWidth)

        self.lblHeight = QLabel(self.frame_10)
        self.lblHeight.setObjectName(u"lblHeight")
        self.lblHeight.setWordWrap(True)

        self.formLayout.setWidget(3, QFormLayout.ItemRole.LabelRole, self.lblHeight)

        self.edtHeight = QLineEdit(self.frame_10)
        self.edtHeight.setObjectName(u"edtHeight")

        self.formLayout.setWidget(3, QFormLayout.ItemRole.FieldRole, self.edtHeight)

        self.lblOffsetX = QLabel(self.frame_10)
        self.lblOffsetX.setObjectName(u"lblOffsetX")
        self.lblOffsetX.setWordWrap(True)

        self.formLayout.setWidget(4, QFormLayout.ItemRole.LabelRole, self.lblOffsetX)

        self.edtOffsetX = QLineEdit(self.frame_10)
        self.edtOffsetX.setObjectName(u"edtOffsetX")

        self.formLayout.setWidget(4, QFormLayout.ItemRole.FieldRole, self.edtOffsetX)

        self.lblOffsetY = QLabel(self.frame_10)
        self.lblOffsetY.setObjectName(u"lblOffsetY")
        self.lblOffsetY.setWordWrap(True)

        self.formLayout.setWidget(5, QFormLayout.ItemRole.LabelRole, self.lblOffsetY)

        self.edtOffsetY = QLineEdit(self.frame_10)
        self.edtOffsetY.setObjectName(u"edtOffsetY")

        self.formLayout.setWidget(5, QFormLayout.ItemRole.FieldRole, self.edtOffsetY)


        self.verticalLayout_2.addWidget(self.frame_10)


        self.verticalLayout.addWidget(self.frame_5)


        self.horizontalLayout.addWidget(self.frame_6)


        self.retranslateUi(PageCamera)

        QMetaObject.connectSlotsByName(PageCamera)
    # setupUi

    def retranslateUi(self, PageCamera):
        PageCamera.setWindowTitle(QCoreApplication.translate("PageCamera", u"Form", None))
        self.btnOrgFullFill.setText(QCoreApplication.translate("PageCamera", u"Full Fill", None))
        self.label_12.setText(QCoreApplication.translate("PageCamera", u"Original Image", None))
        self.lblOrgInfo.setText(QCoreApplication.translate("PageCamera", u"TextLabel", None))
        self.btnOrgOpen.setText(QCoreApplication.translate("PageCamera", u"Open", None))
        self.label_13.setText(QCoreApplication.translate("PageCamera", u"Preprocessed", None))
        self.btnTransformedFullFill.setText(QCoreApplication.translate("PageCamera", u"Full Fill", None))
        self.lblTransformedInfo.setText(QCoreApplication.translate("PageCamera", u"TextLabel", None))
        self.label_15.setText(QCoreApplication.translate("PageCamera", u"ToolBox", None))
        self.btnSave.setText(QCoreApplication.translate("PageCamera", u"Save", None))
        self.btnLoad.setText(QCoreApplication.translate("PageCamera", u"Load", None))
        self.btnRun.setText(QCoreApplication.translate("PageCamera", u"Run", None))
        self.btnConfig.setText(QCoreApplication.translate("PageCamera", u"Config", None))
        self.label_16.setText(QCoreApplication.translate("PageCamera", u"Select Camera", None))
        self.lblStatus.setText("")
        self.btnRefresh.setText("")
        self.label_17.setText(QCoreApplication.translate("PageCamera", u"ON/OFF", None))
        self.btnCamON.setText(QCoreApplication.translate("PageCamera", u"Cam ON", None))
        self.btnStartSnap.setText(QCoreApplication.translate("PageCamera", u"Start Snap", None))
        self.label_18.setText(QCoreApplication.translate("PageCamera", u"Trigger", None))
        self.label.setText(QCoreApplication.translate("PageCamera", u"Trigger Mode", None))
        self.label_5.setText(QCoreApplication.translate("PageCamera", u"Trigger Source", None))
        self.label_6.setText(QCoreApplication.translate("PageCamera", u"Soft Trigger", None))
        self.btnSoftTrigger.setText(QCoreApplication.translate("PageCamera", u"Send", None))
        self.label_7.setText(QCoreApplication.translate("PageCamera", u"Trigger Edge", None))
        self.label_19.setText(QCoreApplication.translate("PageCamera", u"Parameters", None))
        self.lblExposure.setText(QCoreApplication.translate("PageCamera", u"Exposure", None))
        self.lblGain.setText(QCoreApplication.translate("PageCamera", u"Gain", None))
        self.lblWidth.setText(QCoreApplication.translate("PageCamera", u"Width", None))
        self.lblHeight.setText(QCoreApplication.translate("PageCamera", u"Height", None))
        self.lblOffsetX.setText(QCoreApplication.translate("PageCamera", u"OffsetX", None))
        self.lblOffsetY.setText(QCoreApplication.translate("PageCamera", u"OffsetY", None))
    # retranslateUi

