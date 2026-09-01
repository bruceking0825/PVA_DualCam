import os
from PySide6.QtWidgets import (QMainWindow, QHeaderView, QPushButton, QGraphicsDropShadowEffect, 
                               QMessageBox, QSizeGrip)
from PySide6.QtCore import Qt, QPropertyAnimation, QEasingCurve, QParallelAnimationGroup, QEvent, QTimer, QThread
from PySide6.QtGui import QIcon, QColor, QPixmap, QMouseEvent
from modules import Settings, Ui_MainWindow
from modules.page_camera import PageCamera
from modules.page_home import PageHome
from modules.page_parameters import PageParameters
from .app_config import config_manager, default_config_path
from .signals import signals

import atexit
import threading

# 全局变量
GLOBAL_STATE = False
GLOBAL_TITLE_BAR = True

os.environ["QT_FONT_DPI"] = "96"  # 高 DPI 修复

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        # 初始化 UI
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        config_manager.load(default_config_path(), emit_changes=False)
        self.pageHome = PageHome(parent=self.ui.stackedWidget)
        self.pageCamera = PageCamera(parent=self.ui.stackedWidget)
        self.pageParameters = PageParameters(parent=self.ui.stackedWidget)
        #self.ui = self.ui  # 全局 widgets 引用

        # 设置窗口标题和描述
        self.setWindowTitle("Stereo Vision Measurement")
        self.ui.titleRightInfo.setText("Stereo Vision Measurement")
        self.ui.btn_home.setText("Home")
        self.ui.btn_statistics.hide()
        self.ui.btn_IO.hide()


        # 设置自定义标题栏
        self.setup_custom_title_bar()


        # 设置阴影效果
        self.shadow = QGraphicsDropShadowEffect(self)
        self.shadow.setBlurRadius(17)
        self.shadow.setXOffset(0)
        self.shadow.setYOffset(0)
        self.shadow.setColor(QColor(0, 0, 0, 150))
        self.ui.bgApp.setGraphicsEffect(self.shadow)

        # 设置大小调整手柄
        self.sizegrip = QSizeGrip(self.ui.frame_size_grip)
        self.sizegrip.setStyleSheet("width: 20px; height: 20px; margin 0px; padding: 0px;")

        # 绑定信号
        self.bind_signals()

        # 绑定事件
        self.bind_events()

        # 应用主题
        self.apply_theme("themes/py_dracula_dark.qss")
        self.sorter_server = None


        # 设置默认页面
        self.ui.btn_home.setStyleSheet(self.select_menu(self.ui.btn_home.styleSheet()))
        self.ui.stackedWidget.addWidget(self.pageHome)
        self.ui.stackedWidget.addWidget(self.pageParameters)
        self.ui.stackedWidget.addWidget(self.pageCamera)
        self.ui.stackedWidget.setCurrentWidget(self.pageHome)

        # 动画效果
        self.leftMenuAnimation = QPropertyAnimation(self.ui.leftMenuBg, b"minimumWidth")
        self.leftBoxAnimation = QPropertyAnimation(self.ui.extraLeftBox, b"minimumWidth")
        self.rightBoxAnimation = QPropertyAnimation(self.ui.extraRightBox, b"minimumWidth")
        self.groupAnimation = QParallelAnimationGroup()
        
        def check_threads():
            print("程序即将退出，存活线程：")
            for t in threading.enumerate():
                print(f"{t.name} - Alive: {t.is_alive()}, Daemon: {t.daemon}")

        atexit.register(check_threads)

        # 显示窗口
        self.show()

    def closeEvent(self, event):
        reply = QMessageBox.question(
            self,
            "Confirm Exit",
            "Close the program?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            event.ignore()
            return

        print("MainWindow closing")
        
        if getattr(self, "sorter_server", None):
            self.sorter_server.stop()
        signals.app_close.emit()
        event.accept()

    # UI 初始化相关方法
    def setup_custom_title_bar(self):
        if Settings.ENABLE_CUSTOM_TITLE_BAR:
            self.setWindowFlags(Qt.WindowType.FramelessWindowHint)
            self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
            self.ui.titleRightInfo.mouseDoubleClickEvent = self.double_click_maximize_restore
            self.ui.titleRightInfo.mouseMoveEvent = self.move_window
        else:
            self.ui.appMargins.setContentsMargins(0, 0, 0, 0)
            self.ui.minimizeAppBtn.hide()
            self.ui.maximizeRestoreAppBtn.hide()
            self.ui.closeAppBtn.hide()
            self.ui.frame_size_grip.hide()

    def bind_signals(self):
        pass

    def bind_events(self):
        # 菜单切换
        self.ui.toggleButton.clicked.connect(lambda: self.toggle_menu(True))
        self.ui.toggleLeftBox.clicked.connect(lambda: self.toggle_left_box(True))
        self.ui.extraCloseColumnBtn.clicked.connect(lambda: self.toggle_left_box(True))
        self.ui.settingsTopBtn.clicked.connect(lambda: self.toggle_right_box(True))

        # 页面切换和按钮点击
        self.ui.btn_home.clicked.connect(self.button_click)
        self.ui.btn_parameters.clicked.connect(self.button_click)
        self.ui.btn_camera.clicked.connect(self.button_click)


        # 窗口控制按钮
        self.ui.minimizeAppBtn.clicked.connect(self.showMinimized)
        self.ui.maximizeRestoreAppBtn.clicked.connect(self.maximize_restore)
        self.ui.closeAppBtn.clicked.connect(self.close)


    def apply_theme(self, theme_file):
        with open(theme_file, 'r') as f:
            self.ui.styleSheet.setStyleSheet(f.read())

    # 页面和按钮事件处理方法
    def button_click(self):
        btn:QPushButton = self.sender()
        btn_name = btn.objectName()

        if btn_name == "btn_home":
            self.ui.stackedWidget.setCurrentWidget(self.pageHome)

        elif btn_name == "btn_parameters":
            self.ui.stackedWidget.setCurrentWidget(self.pageParameters)

        elif btn_name == "btn_camera":
            self.ui.stackedWidget.setCurrentWidget(self.pageCamera)

        self.reset_style(btn_name)
        btn.setStyleSheet(self.select_menu(btn.styleSheet()))


    def mousePressEvent(self, event:QMouseEvent):
        self.dragPos = event.globalPos()

    def move_window(self, event:QMouseEvent):
        if self.return_status():
            self.maximize_restore()
        if event.buttons() == Qt.MouseButton.LeftButton:
            self.move(self.pos() + event.globalPos() - self.dragPos)
            self.dragPos = event.globalPos()
            event.accept()

    def double_click_maximize_restore(self, event:QMouseEvent):
        if event.type() == QEvent.MouseButtonDblClick:
            QTimer.singleShot(250, self.maximize_restore)

    # UI 操作和动画方法
    def maximize_restore(self):
        global GLOBAL_STATE
        if not GLOBAL_STATE:
            # 保存最大化前的位置和大小
            self.normal_geometry = self.geometry()

            self.showMaximized()
            GLOBAL_STATE = True
            self.ui.appMargins.setContentsMargins(0, 0, 0, 0)
            self.ui.maximizeRestoreAppBtn.setToolTip("Restore")
            self.ui.maximizeRestoreAppBtn.setIcon(QIcon(u":/icons/images/icons/icon_restore.png"))
            self.ui.frame_size_grip.hide()
        else:
            GLOBAL_STATE = False

            self.showNormal()

            # 恢复为最大化前的大小和位置
            if self.normal_geometry:
                self.setGeometry(self.normal_geometry)

            self.ui.appMargins.setContentsMargins(10, 10, 10, 10)
            self.ui.maximizeRestoreAppBtn.setToolTip("Maximize")
            self.ui.maximizeRestoreAppBtn.setIcon(QIcon(u":/icons/images/icons/icon_maximize.png"))
            self.ui.frame_size_grip.show()


    def return_status(self):
        return GLOBAL_STATE

    def toggle_menu(self, enable):
        if enable:
            width = self.ui.leftMenuBg.width()
            max_extend = Settings.MENU_WIDTH
            standard = 60
            width_extended = max_extend if width == standard else standard

            
            self.leftMenuAnimation.setDuration(Settings.TIME_ANIMATION)
            self.leftMenuAnimation.setStartValue(width)
            self.leftMenuAnimation.setEndValue(width_extended)
            self.leftMenuAnimation.setEasingCurve(QEasingCurve.InOutQuart)
            self.leftMenuAnimation.start()

    def toggle_left_box(self, enable):
        if enable:
            width = self.ui.extraLeftBox.width()
            width_right_box = self.ui.extraRightBox.width()
            max_extend = Settings.LEFT_BOX_WIDTH
            color = Settings.BTN_LEFT_BOX_COLOR
            standard = 0

            style = self.ui.toggleLeftBox.styleSheet()
            if width == 0:
                width_extended = max_extend
                self.ui.toggleLeftBox.setStyleSheet(style + color)
                if width_right_box != 0:
                    right_style = self.ui.settingsTopBtn.styleSheet()
                    self.ui.settingsTopBtn.setStyleSheet(right_style.replace(Settings.BTN_RIGHT_BOX_COLOR, ''))
            else:
                width_extended = standard
                self.ui.toggleLeftBox.setStyleSheet(style.replace(color, ''))

            self.start_box_animation(width, width_right_box, "left")

    def toggle_right_box(self, enable):
        if enable:
            width = self.ui.extraRightBox.width()
            width_left_box = self.ui.extraLeftBox.width()
            max_extend = Settings.RIGHT_BOX_WIDTH
            color = Settings.BTN_RIGHT_BOX_COLOR
            standard = 0

            style = self.ui.settingsTopBtn.styleSheet()
            if width == 0:
                width_extended = max_extend
                self.ui.settingsTopBtn.setStyleSheet(style + color)
                if width_left_box != 0:
                    left_style = self.ui.toggleLeftBox.styleSheet()
                    self.ui.toggleLeftBox.setStyleSheet(left_style.replace(Settings.BTN_LEFT_BOX_COLOR, ''))
            else:
                width_extended = standard
                self.ui.settingsTopBtn.setStyleSheet(style.replace(color, ''))

            self.start_box_animation(width_left_box, width, "right")

    def start_box_animation(self, left_box_width, right_box_width, direction):
        right_width = 240 if right_box_width == 0 and direction == "right" else 0
        left_width = 240 if left_box_width == 0 and direction == "left" else 0

        
        self.leftBoxAnimation.setDuration(Settings.TIME_ANIMATION)
        self.leftBoxAnimation.setStartValue(left_box_width)
        self.leftBoxAnimation.setEndValue(left_width)
        self.leftBoxAnimation.setEasingCurve(QEasingCurve.InOutQuart)

        
        self.rightBoxAnimation.setDuration(Settings.TIME_ANIMATION)
        self.rightBoxAnimation.setStartValue(right_box_width)
        self.rightBoxAnimation.setEndValue(right_width)
        self.rightBoxAnimation.setEasingCurve(QEasingCurve.InOutQuart)

        
        self.groupAnimation.addAnimation(self.leftBoxAnimation)
        self.groupAnimation.addAnimation(self.rightBoxAnimation)
        self.groupAnimation.start()

    def select_menu(self, style):
        return style + Settings.MENU_SELECTED_STYLESHEET

    def deselect_menu(self, style):
        return style.replace(Settings.MENU_SELECTED_STYLESHEET, "")

    def reset_style(self, current_widget):
        for w in self.ui.topMenu.findChildren(QPushButton):
            if w.objectName() != current_widget:
                w.setStyleSheet(self.deselect_menu(w.styleSheet()))


if __name__ == "__main__":
    from PySide6.QtWidgets import QApplication
    import sys

    app = QApplication(sys.argv)
    window = MainWindow()
    sys.exit(app.exec())
