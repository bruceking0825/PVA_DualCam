# C++ 代码阅读与运行结构

以 2026-10-11 调用链扁平化后的实现为准。先从页面按钮的处理函数阅读，再通过 bindSignals 中的具名连接找到业务入口；不需要搜索字符串槽名。

## 四条常用调用路径

箭头标注“排队”的地方会换线程；普通箭头表示同线程调用。

| 操作 | 阅读顺序 |
| --- | --- |
| 离线 Run / Stop | PageHome::toggleRuntime → startRequested / stopRequested → **排队到运行线程** → RuntimeController::startRuntime / stopRuntime |
| 下一张 | PageHome::nextImage → imageStepRequested → **排队到运行线程** → RuntimeController::stepImage → setImageIndex → submitOfflineFrame → MeasurementWorker::submitOffline |
| PLC 测量 | SherlockTcpServer 解码 → PlcSession::commandReceived → RuntimeController::onSherlockCommand → triggerOnlineCapture → **排队到相机线程** → CameraService::triggerOnlineCameras → pumpCapture → DalsaCamera |
| 相机调试曝光 | PageCamera::applyExposure 读取输入框 → exposureRequested → **排队到相机线程** → CameraService::applyExposure → DalsaCamera::setExposure |

测量任务在 MeasurementWorker::run 中执行 MeasurementEngine；结果排队回到 RuntimeController::acceptResult，先调用 PlcSession::complete 回复 PLC，再放入页面读取的显示邮箱。

SDK 回调由 CameraService::resetDevices 建立的连接直接排队到 CameraService::onFrame；不再经过中间相机管理类。关闭时递增回调批次，丢弃关闭前已排队的帧和错误。

PLC 的 `exptme1/2` 只保留 Sherlock 7 协议校验与应答，不再进入相机控制。在线曝光由 CameraService 根据阶段管理：Dip/Neck 自动曝光，进入 Melt 时恢复 cnf 初始曝光与增益。

## 到哪里修改功能

| 文件 / 对象 | 放置内容 |
| --- | --- |
| src/page_home.cpp、src/page_camera.cpp | 按钮、输入值、提示和绘图；bindEvents 绑定控件，bindSignals 绑定业务对象 |
| src/runtime_controller.cpp | 创建与配置、启停与阶段、离线切图、在线回调、PLC 请求、结果与日志；函数按这些分组排列 |
| src/camera_service.cpp | 设备容器、SDK 初始化/关闭、手动与生产使用权、曝光、触发队列及帧归属；直接持有 DalsaCamera |
| src/dalsa_camera.cpp | Sapera SDK 调用与缓冲交接 |
| src/plc_session.cpp | 网络会话、参数/ROI、待回复测量、超时、PLC 状态保存；measurement_payload.cpp 和 sherlock_protocol.cpp 负责结果字段与编码 |
| src/measurement_worker.cpp、src/measurement_engine.cpp、src/algorithms/ | 后台测量任务、测量历史和具体检测算法 |
| src/image_storage.cpp | Facette 后台读写；其它状态继续使用现有 Store |
| src/app_services.cpp | 创建服务线程、连接服务和配置通知、统一退出；不处理测量请求 |

相机角色定义在 models.hpp。RuntimeController::plcSession() 供装配和信号订阅使用；UI 不通过它直接修改 PLC 状态。

新增功能优先放入上述现有对象。只有出现独立生命周期和实质状态时才考虑新增类，不为转发请求增加包装层。静态布局仍由 Designer .ui 定义。

## 状态与线程边界

- RuntimeController 在运行线程独占运行状态、模式、阶段、选图索引、帧配对和任务身份。页面快照只用于显示。
- CameraService 在相机线程独占设备和采集队列。生产期间拒绝冲突的手动操作；相机页面是否存在不影响生产采集。
- PlcSession 与 RuntimeController 同线程；一般业务直接调用。它独占一个待回复测量请求及超时，重复请求明确返回 busy。
- MeasurementWorker 执行后台解码和计算，只保留一个当前任务和一个最新待执行任务。generation、批次、请求及配置版本继续隔离过期任务与测量历史。
- ImageStorage 的测量覆盖图（image1.bmp/image2.bmp）使用独立单线程池：一份正在保存，一份最新等待结果；新结果替换尚未开始的旧结果，不逐帧归档。等待结果携带自己的图像、参数、ROI 和输出目录快照，退出时完成最后一份保存。
- Facette 保持独立于测量覆盖图的有界串行队列，已接收任务不替换，满载明确报错。连续 PNG 归档仍使用控制器中的独立 ImageStorage 实例，保留原有容量和错误处理。
- SDK 缓冲出口保留必要复制，之后图像按只读方式传递。跨线程显示邮箱由互斥锁保护。
- Home 图像刷新约 15 次/秒，诊断最多 5 次/秒；Camera 预览最多 10 次/秒，预览处理在后台池运行。隐藏页面停止绘制。
- AppServices 退出时先停止运行，再关闭相机，最后退出并等待线程。等待测量收尾在运行线程进行。

## 当前行为约定

- 离线图像上半张为 Cam1、下半张为 Cam2，不旋转；对外几何为原图坐标。此次不改测量算法、ROI 或梯度百分比判定。
- 保留当前 acq_on_ / acq_off 的相机确认、取消、5 秒超时、关闭失败重试及 PLC 测试模式逻辑。
- ConfigManager 是 INI 基础来源；运行控制器生成有效配置。当前 PlcSession 中 dia_thr / diathr2 对算法梯度阈值的覆盖是禁用的，继续使用手动配置。
- PLC 曝光优先于自动曝光；启动时保留记忆曝光及 PLC 曝光重放。自动曝光仍只用于当前实现中的 Melt / Dip / Neck 阶段。
- Facette 保留立即协议确认、后续失败在本机报警；Cam2 Facette 与双相机测量在相机队列中串行执行。
- 测试通过业务对象提交 PLC 命令和配置更新；页面不再提供测试专用的业务转发入口。
