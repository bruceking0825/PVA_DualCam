# PVA DualCam C++

Qt 5/6 + OpenCV 4 的双相机测量程序。C++ 工程根目录就是本目录，`cpp/` 仅存放辅助 Python 脚本。

静态界面由 `ui/main.ui`、`ui/PageHome.ui`、`ui/PageCamera.ui` 和 `ui/PageParameters.ui` 定义。Home 的 Cam1/Cam2 保持上下布局；离线拼接图上半部分为 Cam1，下半部分为 Cam2，读取时不旋转。

## 构建与运行

在 MSVC 开发者终端中，从工程根目录执行（依赖路径按本机修改）：

```powershell
cmake -S . -B out/build/tasks-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=D:/Qt/6.9.3/msvc2022_64 -DOpenCV_DIR=D:/APP/opencv4.14.0/opencv/build/x64/vc16/lib
cmake --build out/build/tasks-debug
ctest --test-dir out/build/tasks-debug --output-on-failure --timeout 30
```

启动 `out/build/tasks-debug/SherlockSrvr.exe`，并将 Qt 和 OpenCV 的 `bin` 目录加入 PATH。配置文件为根目录的 `cnf.ini`，其中相对路径以配置目录为基准。

VS Code 打开工程根目录后，可运行 `CMake: Build Debug` 或 `CMake: Build Release` 任务；F5 使用 `.vscode/launch.json`。不要求使用 CMakeUserPresets。MSVC 启动器统一依赖输出编码，避免中文系统上的 Ninja 漏编译头文件变更。

Qt、OpenCV、编译器和 Sapera 必须采用一致的 x86/x64 架构。在线相机还需要对应架构的 Sapera 头文件、导入库和运行时 DLL；不满足时构建会明确提示禁用相机支持，但仍可运行离线功能。

## 运行结构

- `AppServices` 创建并注入运行控制器、相机服务，负责线程和退出顺序。
- `RuntimeController` 拥有唯一运行状态、阶段、批次与任务分发，不依赖 QWidget。
- `CameraService` 管理设备、曝光、手动/生产使用权以及 Cam2 测量和 Facette 触发顺序。
- `MeasurementWorker` 在后台解码离线图像并执行测量，复用当前文件缓存。
- `PlcSession` 管理参数覆盖、请求超时及结果回复；算法通过具名字段返回结果。
- `ImageStorage` 串行保存 Facette 图片；测量、相机、PLC 状态仍复用现有原子文件存储格式。
- 页面只发送操作、接收快照和绘图。Home 图像最多约 15 FPS，诊断最多 5 FPS。

详细职责、线程边界、兼容约束与后续扩展入口见 [架构说明](docs/cpp_architecture.md)。

查找功能时，从页面的 `bindEvents` 找到按钮处理函数，再在 `bindSignals` 查看具名业务连接。架构说明列出 Run、切图、PLC 测量和相机调试的具体函数路径。相机服务直接持有设备；PLC 曝光直接连接相机服务。

## 协议与功能边界

程序作为 Sherlock 兼容 TCP 服务端：5000 接收命令，5001 返回结果。保留既有帧编码、字段顺序、倍率、错误格式和 90 字节报文兼容处理。

支持直径、Melt/Dip、采集开关、版本/刷新率、拟合模式、阈值/曝光/ROI 参数及四张 Facette 图片命令。历史兼容参数只保存、不参与当前算法的行为保持不变。Melt 计数字段仍保留原实现的零值；本次重构不补做旧 Sherlock 中未实现的检测。

Neck 直径由 Cam1 决定；Cam2 提供参考结果。保留 Crown/Body、Endcone 的现有检测与状态依赖，不改变阈值标准和原图坐标约定。

## 验证

`runtime_tests` 不创建 QWidget，覆盖离线测量、模拟在线配对、过期结果、参数覆盖、PLC 超时、Facette 交错、存储队列及停止恢复。`service_lifecycle_tests` 验证无页面的应用装配和退出。其余测试覆盖算法、协议和 Facette 显示。

`page_command_tests` 通过真实按钮验证页面到后台线程的连接；`connection_health_tests` 验证相机回调批次隔离和连接状态。当前 Debug/Release 均通过 11 项测试，环境与未验证项见 [验证记录](docs/cpp_refactor_validation.md)。

Release 连续运行测试（依赖 DLL 同样需要在 PATH 中）：

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
./out/build/tasks-release/runtime_ui_benchmark.exe 600
```

测试仅使用临时合成图像和状态文件，报告 10 ms 界面定时器的延迟 P95、结果数及 Windows 工作集。它不能替代真实设备、真实图像和人工拖动/缩放/切页验收。验收记录见 [重构验证记录](docs/cpp_refactor_validation.md)。

## 32 位 Release 打包

先在 x86 MSVC 开发者终端构建最新代码：

~~~powershell
cmake --build build-win32-release --target pva_dualcam_cpp
~~~

双击根目录 package_win32.cmd，或执行：

~~~powershell
.\package_win32.cmd -NoPause
# 只检查输入，不复制文件
.\package_win32.cmd -ValidateOnly -NoPause
# 指定其他已编译的 32 位 Release 目录
.\package_win32.cmd -BuildDirectory E:\path\to\release -NoPause
~~~

输出为 dist/PVA_DualCam_x86_时间戳_随机后缀/，包含 EXE、Qt 插件、OpenCV DLL、配置和主题。脚本读取 CMake 缓存中的依赖路径，并检查 EXE 和 DLL 均为 x86；不会重新编译，不复制现有测量状态和生产图片。包内已包含 x86 Visual C++ 运行库，并提供 start.cmd 固定启动目录；Windows 10/11 无需另装 VC 运行库。在线相机需安装对应架构的 Sapera 运行库和采集卡驱动。部署前检查 cnf.ini 中的路径和相机配置。


### 一键编译并打包

双击根目录 build_package_win32.cmd，自动初始化 x86 MSVC 14.29、配置 Release、编译最新主程序，然后调用打包脚本。任一步失败即停止；成功时显示 dist 下的新部署目录。脚本中的 Qt/OpenCV/Sapera 路径与本机 Win32 构建配置一致。

~~~powershell
.\build_package_win32.cmd -NoPause
~~~

生产主机复制整个输出目录，运行 start.cmd。仍需已安装匹配的 Sapera 运行库和采集卡驱动，并检查 cnf.ini 路径。
