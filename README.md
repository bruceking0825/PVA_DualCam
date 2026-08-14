# Stereo Camera Calibration

本项目基于 `E:\Bruce\Sorter` 的 PySide6 / PyDracula 框架改造，用于双目相机棋盘格标定。

## 当前流程

1. 相机 UserID 分别设置为 `1` 和 `2`。
2. 主界面首页为 `Calibration`。
3. 页面左侧是两个 `CustomGraphicsView`，上方显示相机 1，下方显示相机 2。
4. 页面右侧是图片加载、相机操作、标定参数和日志信息。
5. 离线测试时点击 `Load Combined Image` 或 `Load Next`，从 `imgs` 目录加载一张左右拼接图。
6. 合成图会按列方向从中间一分为二，左半图给相机 1，右半图给相机 2。
7. 在线采集时点击 `Open 1 && 2` 打开两台相机，点击 `Capture` 同时软触发两台相机。
8. 点击 `Detect && Add Step` 检测当前两张图中的棋盘格角点，并按当前高度写入采样数据。
9. 标定板每次上升 `10 mm`，默认采集 `5` 次，对应 `z = 0, 10, 20, 30, 40 mm`。
10. 点击 `Calibrate` 进行双目标定并生成带字段说明的 `ccal001.json`。

## 配置

参数集中在 `src/cnf.ini`：

- `image_dir`: 合成图目录，默认 `E:\Bruce\Calibration\imgs`
- `output_json`: 标定输出文件
- `camera_config`: 相机配置名
- `board_rows`: 棋盘格角点行数
- `board_cols`: 棋盘格角点列数
- `square_size_mm`: 棋盘格格距
- `z_step_mm`: 标定板每次上升高度
- `capture_count`: 采集次数
- `camera1_user_id`: 相机 1 UserID
- `camera2_user_id`: 相机 2 UserID

## 主要文件

- `src/main.py`: 程序入口。
- `src/cnf.ini`: 标定参数配置。
- `src/modules/page_calibration.py`: 双相机图片加载、采集、角点检测、采样和标定页面。
- `src/modules/camera_device.py`: 相机角色定义，包含 `CAM1 = "1"` 和 `CAM2 = "2"`。
- `src/modules/ui_functions.py`: 主窗口，默认进入标定页面。
- `src/calibration_core/`: MATLAB 标定核心的 Python 移植。
- `matlab_version/Calibration/`: 原 MATLAB 标定程序。

## 运行

```powershell
cd E:\Bruce\Calibration\src
python main.py
```

如果使用 Sorter 原环境，可按实际机器环境切换到对应 conda 环境后再运行。
