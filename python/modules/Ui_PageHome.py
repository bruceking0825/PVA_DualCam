# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'PageHome.ui'
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
from PySide6.QtWidgets import (QApplication, QFormLayout, QFrame, QGridLayout,
    QHBoxLayout, QHeaderView, QLabel, QPlainTextEdit,
    QPushButton, QSizePolicy, QSpacerItem, QSplitter,
    QTabWidget, QTreeWidget, QTreeWidgetItem, QVBoxLayout,
    QWidget)

from widgets import CustomGraphicsView
import resources_rc

class Ui_PageHome(object):
    def setupUi(self, PageHome):
        if not PageHome.objectName():
            PageHome.setObjectName(u"PageHome")
        PageHome.resize(1100, 655)
        PageHome.setStyleSheet(u"#PageHome {\n"
"    background-color: rgb(40, 44, 52);\n"
"}\n"
"#frameCam1 {\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"    border-left: 2px solid rgb(70, 80, 110);\n"
"    border-bottom: 2px solid rgb(70, 80, 110);\n"
"    border-right: 0px solid rgb(70, 80, 110);\n"
"}\n"
"#frameCam2 {\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"    border-left: 0px solid rgb(70, 80, 110);\n"
"    border-bottom: 2px solid rgb(70, 80, 110);\n"
"    border-right: 0px solid rgb(70, 80, 110);\n"
"}\n"
"#rightPanel {\n"
"    border-top: 2px solid rgb(70, 80, 110);\n"
"    border-left: 0px solid rgb(70, 80, 110);\n"
"    border-bottom: 2px solid rgb(70, 80, 110);\n"
"    border-right: 2px solid rgb(70, 80, 110);\n"
"    background: transparent;\n"
"}\n"
"#rightPanel QLabel {\n"
"    border: 0px;\n"
"}")
        self.horizontalLayout = QHBoxLayout(PageHome)
        self.horizontalLayout.setSpacing(8)
        self.horizontalLayout.setObjectName(u"horizontalLayout")
        self.horizontalLayout.setContentsMargins(10, 10, 10, 10)
        self.mainSplitter = QSplitter(PageHome)
        self.mainSplitter.setObjectName(u"mainSplitter")
        self.mainSplitter.setOrientation(Qt.Orientation.Horizontal)
        self.mainSplitter.setHandleWidth(1)
        self.viewSplitter = QSplitter(self.mainSplitter)
        self.viewSplitter.setObjectName(u"viewSplitter")
        self.viewSplitter.setOrientation(Qt.Orientation.Horizontal)
        self.viewSplitter.setHandleWidth(1)
        self.frameCam1 = QFrame(self.viewSplitter)
        self.frameCam1.setObjectName(u"frameCam1")
        self.frameCam1.setFrameShape(QFrame.Shape.StyledPanel)
        self.frameCam1.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayoutCam1 = QGridLayout(self.frameCam1)
        self.gridLayoutCam1.setSpacing(4)
        self.gridLayoutCam1.setObjectName(u"gridLayoutCam1")
        self.gridLayoutCam1.setContentsMargins(4, 4, 4, 4)
        self.cam1TitleLayout = QHBoxLayout()
        self.cam1TitleLayout.setSpacing(4)
        self.cam1TitleLayout.setObjectName(u"cam1TitleLayout")
        self.lblCamera1Status = QLabel(self.frameCam1)
        self.lblCamera1Status.setObjectName(u"lblCamera1Status")
        self.lblCamera1Status.setMaximumSize(QSize(24, 24))
        self.lblCamera1Status.setPixmap(QPixmap(u":/images/images/images/ledHigh.png"))
        self.lblCamera1Status.setScaledContents(True)

        self.cam1TitleLayout.addWidget(self.lblCamera1Status)

        self.labelCam1 = QLabel(self.frameCam1)
        self.labelCam1.setObjectName(u"labelCam1")
        self.labelCam1.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.cam1TitleLayout.addWidget(self.labelCam1)


        self.gridLayoutCam1.addLayout(self.cam1TitleLayout, 0, 0, 1, 1)

        self.cam1GraphicsView = CustomGraphicsView(self.frameCam1)
        self.cam1GraphicsView.setObjectName(u"cam1GraphicsView")

        self.gridLayoutCam1.addWidget(self.cam1GraphicsView, 1, 0, 1, 1)

        self.lblCam1Info = QLabel(self.frameCam1)
        self.lblCam1Info.setObjectName(u"lblCam1Info")

        self.gridLayoutCam1.addWidget(self.lblCam1Info, 2, 0, 1, 1)

        self.viewSplitter.addWidget(self.frameCam1)
        self.frameCam2 = QFrame(self.viewSplitter)
        self.frameCam2.setObjectName(u"frameCam2")
        self.frameCam2.setFrameShape(QFrame.Shape.StyledPanel)
        self.frameCam2.setFrameShadow(QFrame.Shadow.Raised)
        self.gridLayoutCam2 = QGridLayout(self.frameCam2)
        self.gridLayoutCam2.setSpacing(4)
        self.gridLayoutCam2.setObjectName(u"gridLayoutCam2")
        self.gridLayoutCam2.setContentsMargins(4, 4, 4, 4)
        self.cam2TitleLayout = QHBoxLayout()
        self.cam2TitleLayout.setSpacing(4)
        self.cam2TitleLayout.setObjectName(u"cam2TitleLayout")
        self.lblCamera2Status = QLabel(self.frameCam2)
        self.lblCamera2Status.setObjectName(u"lblCamera2Status")
        self.lblCamera2Status.setMaximumSize(QSize(24, 24))
        self.lblCamera2Status.setPixmap(QPixmap(u":/images/images/images/ledHigh.png"))
        self.lblCamera2Status.setScaledContents(True)

        self.cam2TitleLayout.addWidget(self.lblCamera2Status)

        self.labelCam2 = QLabel(self.frameCam2)
        self.labelCam2.setObjectName(u"labelCam2")
        self.labelCam2.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.cam2TitleLayout.addWidget(self.labelCam2)


        self.gridLayoutCam2.addLayout(self.cam2TitleLayout, 0, 0, 1, 1)

        self.cam2GraphicsView = CustomGraphicsView(self.frameCam2)
        self.cam2GraphicsView.setObjectName(u"cam2GraphicsView")

        self.gridLayoutCam2.addWidget(self.cam2GraphicsView, 1, 0, 1, 1)

        self.lblCam2Info = QLabel(self.frameCam2)
        self.lblCam2Info.setObjectName(u"lblCam2Info")

        self.gridLayoutCam2.addWidget(self.lblCam2Info, 2, 0, 1, 1)

        self.viewSplitter.addWidget(self.frameCam2)
        self.mainSplitter.addWidget(self.viewSplitter)
        self.rightPanel = QFrame(self.mainSplitter)
        self.rightPanel.setObjectName(u"rightPanel")
        self.rightPanel.setMinimumSize(QSize(300, 0))
        self.rightPanel.setFrameShape(QFrame.Shape.StyledPanel)
        self.rightPanel.setFrameShadow(QFrame.Shadow.Raised)
        self.rightLayout = QVBoxLayout(self.rightPanel)
        self.rightLayout.setSpacing(8)
        self.rightLayout.setObjectName(u"rightLayout")
        self.rightLayout.setContentsMargins(10, 10, 10, 10)
        self.runtimeButtonLayout = QHBoxLayout()
        self.runtimeButtonLayout.setSpacing(18)
        self.runtimeButtonLayout.setObjectName(u"runtimeButtonLayout")
        self.runtimeButtonGroupLayout = QVBoxLayout()
        self.runtimeButtonGroupLayout.setSpacing(6)
        self.runtimeButtonGroupLayout.setObjectName(u"runtimeButtonGroupLayout")
        self.btnOnline = QPushButton(self.rightPanel)
        self.btnOnline.setObjectName(u"btnOnline")
        self.btnOnline.setCheckable(True)
        self.btnOnline.setChecked(False)

        self.runtimeButtonGroupLayout.addWidget(self.btnOnline)

        self.btnStart = QPushButton(self.rightPanel)
        self.btnStart.setObjectName(u"btnStart")

        self.runtimeButtonGroupLayout.addWidget(self.btnStart)


        self.runtimeButtonLayout.addLayout(self.runtimeButtonGroupLayout)

        self.connectionStatusLayout = QVBoxLayout()
        self.connectionStatusLayout.setSpacing(4)
        self.connectionStatusLayout.setObjectName(u"connectionStatusLayout")
        self.plcStatusLayout = QHBoxLayout()
        self.plcStatusLayout.setSpacing(4)
        self.plcStatusLayout.setObjectName(u"plcStatusLayout")
        self.lblPlcStatus = QLabel(self.rightPanel)
        self.lblPlcStatus.setObjectName(u"lblPlcStatus")
        self.lblPlcStatus.setMaximumSize(QSize(24, 24))
        self.lblPlcStatus.setPixmap(QPixmap(u":/images/images/images/ledHigh.png"))
        self.lblPlcStatus.setScaledContents(True)

        self.plcStatusLayout.addWidget(self.lblPlcStatus)

        self.labelPlcStatus = QLabel(self.rightPanel)
        self.labelPlcStatus.setObjectName(u"labelPlcStatus")

        self.plcStatusLayout.addWidget(self.labelPlcStatus)


        self.connectionStatusLayout.addLayout(self.plcStatusLayout)


        self.runtimeButtonLayout.addLayout(self.connectionStatusLayout)


        self.rightLayout.addLayout(self.runtimeButtonLayout)

        self.labelOfflineMode = QLabel(self.rightPanel)
        self.labelOfflineMode.setObjectName(u"labelOfflineMode")

        self.rightLayout.addWidget(self.labelOfflineMode)

        self.stageModeLayout = QHBoxLayout()
        self.stageModeLayout.setSpacing(4)
        self.stageModeLayout.setObjectName(u"stageModeLayout")
        self.btnStageIdle = QPushButton(self.rightPanel)
        self.btnStageIdle.setObjectName(u"btnStageIdle")
        self.btnStageIdle.setCheckable(True)

        self.stageModeLayout.addWidget(self.btnStageIdle)

        self.btnStageNeck = QPushButton(self.rightPanel)
        self.btnStageNeck.setObjectName(u"btnStageNeck")
        self.btnStageNeck.setCheckable(True)

        self.stageModeLayout.addWidget(self.btnStageNeck)

        self.btnStageCrown = QPushButton(self.rightPanel)
        self.btnStageCrown.setObjectName(u"btnStageCrown")
        self.btnStageCrown.setCheckable(True)

        self.stageModeLayout.addWidget(self.btnStageCrown)

        self.btnStageBody = QPushButton(self.rightPanel)
        self.btnStageBody.setObjectName(u"btnStageBody")
        self.btnStageBody.setCheckable(True)

        self.stageModeLayout.addWidget(self.btnStageBody)

        self.btnStageEndcone = QPushButton(self.rightPanel)
        self.btnStageEndcone.setObjectName(u"btnStageEndcone")
        self.btnStageEndcone.setCheckable(True)

        self.stageModeLayout.addWidget(self.btnStageEndcone)


        self.rightLayout.addLayout(self.stageModeLayout)

        self.labelOfflineImages = QLabel(self.rightPanel)
        self.labelOfflineImages.setObjectName(u"labelOfflineImages")

        self.rightLayout.addWidget(self.labelOfflineImages)

        self.offlineFileLayout = QHBoxLayout()
        self.offlineFileLayout.setSpacing(6)
        self.offlineFileLayout.setObjectName(u"offlineFileLayout")
        self.btnFirstImage = QPushButton(self.rightPanel)
        self.btnFirstImage.setObjectName(u"btnFirstImage")
        icon = QIcon()
        icon.addFile(u":/icons/images/icons/cil-media-skip-backward.png", QSize(), QIcon.Mode.Normal, QIcon.State.Off)
        self.btnFirstImage.setIcon(icon)

        self.offlineFileLayout.addWidget(self.btnFirstImage)

        self.btnPreviousImage = QPushButton(self.rightPanel)
        self.btnPreviousImage.setObjectName(u"btnPreviousImage")
        icon1 = QIcon()
        icon1.addFile(u":/icons/images/icons/cil-chevron-left.png", QSize(), QIcon.Mode.Normal, QIcon.State.Off)
        self.btnPreviousImage.setIcon(icon1)

        self.offlineFileLayout.addWidget(self.btnPreviousImage)

        self.btnNextImage = QPushButton(self.rightPanel)
        self.btnNextImage.setObjectName(u"btnNextImage")
        icon2 = QIcon()
        icon2.addFile(u":/icons/images/icons/cil-chevron-right.png", QSize(), QIcon.Mode.Normal, QIcon.State.Off)
        self.btnNextImage.setIcon(icon2)

        self.offlineFileLayout.addWidget(self.btnNextImage)

        self.btnLastImage = QPushButton(self.rightPanel)
        self.btnLastImage.setObjectName(u"btnLastImage")
        icon3 = QIcon()
        icon3.addFile(u":/icons/images/icons/cil-media-skip-forward.png", QSize(), QIcon.Mode.Normal, QIcon.State.Off)
        self.btnLastImage.setIcon(icon3)

        self.offlineFileLayout.addWidget(self.btnLastImage)

        self.lblImageIndex = QLabel(self.rightPanel)
        self.lblImageIndex.setObjectName(u"lblImageIndex")
        self.lblImageIndex.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.offlineFileLayout.addWidget(self.lblImageIndex)


        self.rightLayout.addLayout(self.offlineFileLayout)

        self.lblOfflineImage = QLabel(self.rightPanel)
        self.lblOfflineImage.setObjectName(u"lblOfflineImage")

        self.rightLayout.addWidget(self.lblOfflineImage)

        self.detailsTabs = QTabWidget(self.rightPanel)
        self.detailsTabs.setObjectName(u"detailsTabs")
        self.tabResults = QWidget()
        self.tabResults.setObjectName(u"tabResults")
        self.resultsLayout = QVBoxLayout(self.tabResults)
        self.resultsLayout.setSpacing(8)
        self.resultsLayout.setObjectName(u"resultsLayout")
        self.resultsLayout.setContentsMargins(8, 8, 8, 8)
        self.labelMeasurements = QLabel(self.tabResults)
        self.labelMeasurements.setObjectName(u"labelMeasurements")

        self.resultsLayout.addWidget(self.labelMeasurements)

        self.valuesForm = QFormLayout()
        self.valuesForm.setObjectName(u"valuesForm")
        self.valuesForm.setHorizontalSpacing(12)
        self.valuesForm.setVerticalSpacing(8)
        self.labelDiameter = QLabel(self.tabResults)
        self.labelDiameter.setObjectName(u"labelDiameter")

        self.valuesForm.setWidget(1, QFormLayout.ItemRole.LabelRole, self.labelDiameter)

        self.lblDiameter = QLabel(self.tabResults)
        self.lblDiameter.setObjectName(u"lblDiameter")

        self.valuesForm.setWidget(1, QFormLayout.ItemRole.FieldRole, self.lblDiameter)


        self.resultsLayout.addLayout(self.valuesForm)

        self.lblCycle = QLabel(self.tabResults)
        self.lblCycle.setObjectName(u"lblCycle")

        self.resultsLayout.addWidget(self.lblCycle)

        self.lblFrameDelta = QLabel(self.tabResults)
        self.lblFrameDelta.setObjectName(u"lblFrameDelta")

        self.resultsLayout.addWidget(self.lblFrameDelta)

        self.lblLastFrame = QLabel(self.tabResults)
        self.lblLastFrame.setObjectName(u"lblLastFrame")

        self.resultsLayout.addWidget(self.lblLastFrame)

        self.resultsSpacer = QSpacerItem(20, 40, QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)

        self.resultsLayout.addItem(self.resultsSpacer)

        self.detailsTabs.addTab(self.tabResults, "")
        self.tabProcess = QWidget()
        self.tabProcess.setObjectName(u"tabProcess")
        self.processLayout = QVBoxLayout(self.tabProcess)
        self.processLayout.setObjectName(u"processLayout")
        self.processLayout.setContentsMargins(4, 4, 4, 4)
        self.treeProcess = QTreeWidget(self.tabProcess)
        self.treeProcess.setObjectName(u"treeProcess")
        self.treeProcess.setAlternatingRowColors(False)
        self.treeProcess.setIndentation(0)
        self.treeProcess.setRootIsDecorated(False)
        self.treeProcess.setUniformRowHeights(True)

        self.processLayout.addWidget(self.treeProcess)

        self.detailsTabs.addTab(self.tabProcess, "")
        self.tabLog = QWidget()
        self.tabLog.setObjectName(u"tabLog")
        self.logLayout = QVBoxLayout(self.tabLog)
        self.logLayout.setObjectName(u"logLayout")
        self.logLayout.setContentsMargins(4, 4, 4, 4)
        self.txtLog = QPlainTextEdit(self.tabLog)
        self.txtLog.setObjectName(u"txtLog")
        self.txtLog.setReadOnly(True)

        self.logLayout.addWidget(self.txtLog)

        self.detailsTabs.addTab(self.tabLog, "")

        self.rightLayout.addWidget(self.detailsTabs)

        self.lblStatus = QLabel(self.rightPanel)
        self.lblStatus.setObjectName(u"lblStatus")
        self.lblStatus.setMinimumSize(QSize(0, 28))
        self.lblStatus.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lblStatus.setWordWrap(True)

        self.rightLayout.addWidget(self.lblStatus)

        self.mainSplitter.addWidget(self.rightPanel)

        self.horizontalLayout.addWidget(self.mainSplitter)


        self.retranslateUi(PageHome)

        self.detailsTabs.setCurrentIndex(0)


        QMetaObject.connectSlotsByName(PageHome)
    # setupUi

    def retranslateUi(self, PageHome):
        PageHome.setWindowTitle(QCoreApplication.translate("PageHome", u"Stereo Measurement", None))
        self.lblCamera1Status.setText("")
        self.labelCam1.setText(QCoreApplication.translate("PageHome", u"Camera 1", None))
        self.lblCam1Info.setText(QCoreApplication.translate("PageHome", u"Pos (0,0) | G 0 | Light --", None))
        self.lblCamera2Status.setText("")
        self.labelCam2.setText(QCoreApplication.translate("PageHome", u"Camera 2", None))
        self.lblCam2Info.setText(QCoreApplication.translate("PageHome", u"Pos (0,0) | G 0 | Light --", None))
        self.btnOnline.setText(QCoreApplication.translate("PageHome", u"Offline", None))
        self.btnStart.setText(QCoreApplication.translate("PageHome", u"Run", None))
        self.lblPlcStatus.setText("")
        self.labelPlcStatus.setText(QCoreApplication.translate("PageHome", u"PLC", None))
        self.labelOfflineMode.setText(QCoreApplication.translate("PageHome", u"Mode", None))
        self.btnStageIdle.setText(QCoreApplication.translate("PageHome", u"Idle", None))
        self.btnStageNeck.setText(QCoreApplication.translate("PageHome", u"Neck", None))
        self.btnStageCrown.setText(QCoreApplication.translate("PageHome", u"Crown", None))
        self.btnStageBody.setText(QCoreApplication.translate("PageHome", u"Body", None))
        self.btnStageEndcone.setText(QCoreApplication.translate("PageHome", u"Endcone", None))
        self.labelOfflineImages.setText(QCoreApplication.translate("PageHome", u"Image Sequence", None))
#if QT_CONFIG(tooltip)
        self.btnFirstImage.setToolTip(QCoreApplication.translate("PageHome", u"First image", None))
#endif // QT_CONFIG(tooltip)
        self.btnFirstImage.setText("")
#if QT_CONFIG(tooltip)
        self.btnPreviousImage.setToolTip(QCoreApplication.translate("PageHome", u"Previous image", None))
#endif // QT_CONFIG(tooltip)
        self.btnPreviousImage.setText("")
#if QT_CONFIG(tooltip)
        self.btnNextImage.setToolTip(QCoreApplication.translate("PageHome", u"Next image", None))
#endif // QT_CONFIG(tooltip)
        self.btnNextImage.setText("")
#if QT_CONFIG(tooltip)
        self.btnLastImage.setToolTip(QCoreApplication.translate("PageHome", u"Last image", None))
#endif // QT_CONFIG(tooltip)
        self.btnLastImage.setText("")
        self.lblImageIndex.setText(QCoreApplication.translate("PageHome", u"0 / 0", None))
#if QT_CONFIG(tooltip)
        self.lblOfflineImage.setToolTip(QCoreApplication.translate("PageHome", u"Current offline image", None))
#endif // QT_CONFIG(tooltip)
        self.lblOfflineImage.setText(QCoreApplication.translate("PageHome", u"Configured image", None))
        self.labelMeasurements.setText(QCoreApplication.translate("PageHome", u"Measurements", None))
        self.labelDiameter.setText(QCoreApplication.translate("PageHome", u"Diameter (mm)", None))
        self.lblDiameter.setText(QCoreApplication.translate("PageHome", u"--", None))
        self.lblCycle.setText(QCoreApplication.translate("PageHome", u"Cycle: --", None))
        self.lblFrameDelta.setText(QCoreApplication.translate("PageHome", u"Frame delta: --", None))
        self.lblLastFrame.setText(QCoreApplication.translate("PageHome", u"Last frame: --", None))
        self.detailsTabs.setTabText(self.detailsTabs.indexOf(self.tabResults), QCoreApplication.translate("PageHome", u"Results", None))
        ___qtreewidgetitem = self.treeProcess.headerItem()
        ___qtreewidgetitem.setText(2, QCoreApplication.translate("PageHome", u"Unit", None));
        ___qtreewidgetitem.setText(1, QCoreApplication.translate("PageHome", u"Value", None));
        ___qtreewidgetitem.setText(0, QCoreApplication.translate("PageHome", u"Name", None));
        self.detailsTabs.setTabText(self.detailsTabs.indexOf(self.tabProcess), QCoreApplication.translate("PageHome", u"Process", None))
        self.detailsTabs.setTabText(self.detailsTabs.indexOf(self.tabLog), QCoreApplication.translate("PageHome", u"Log", None))
        self.lblStatus.setText(QCoreApplication.translate("PageHome", u"Ready", None))
    # retranslateUi

