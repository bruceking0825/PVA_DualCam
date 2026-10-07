# C++ 运行架构

## 职责与状态归属

| 模块 | 唯一持有的状态 | 执行位置 |
| --- | --- | --- |
| AppServices | 服务实例、线程生命周期与对象连接 | UI 装配层 |
| RuntimeController | 运行状态、模式、阶段、批次/任务身份、有效配置、离线索引、配对与 Facette 请求关联 | 运行控制线程 |
| CameraService / CameraManager | 设备、生产使用权、曝光覆盖、串行触发队列、回调投递批次 | 相机线程；SDK 回调只交接已复制图像 |
| PlcSession | 待回复命令、超时、PLC 参数/ROI 覆盖、协议状态持久化 | 运行控制线程 |
| MeasurementWorker / MeasurementEngine | 单待执行任务、测量历史、配置/ROI 快照、离线解码缓存 | 测量工作线程 |
| ImageStorage | 最多 16 个未完成的 Facette 读写任务 | 单线程后台池 |
| PageHome / PageCamera | 显示快照、缩放/展开状态、最新预览输入 | UI 线程；Camera 预览算法另用单线程池 |

ConfigManager 仍是 INI 基础配置的唯一来源。RuntimeController 保留基础配置副本，并叠加 PlcSession 中的参数生成有效快照；页面不会反向修改有效配置。新增服务状态应只放在一个所有者内，页面的副本只用于显示。

## 流程与身份

离线文件或在线帧对 → MeasurementWorker → MeasurementEngine → RuntimeController → PlcSession / 最新显示邮箱。

每次启动递增 runId；PLC 请求或离线任务有 requestId；配置/覆盖变化递增 configurationVersion。generation 用于撤销阶段、选图、配置及请求变化前的任务。结果同时携带来源、阶段和以上身份。旧任务不仅不能回显/回复新请求，也不能提交其候选测量历史。

在线启动等待相机服务确认后从 Starting 转为 Running。停止经过 Stopping，撤销任务并停止服务后进入 Stopped；相机失败进入 Faulted。相机生命周期通知带运行批次，关闭设备时使已经排队的旧 SDK 回调失效。只有 RuntimeController 改变运行状态。

## 队列与线程约束

- 测量线程保留一个运行任务和一个待执行任务。普通离线预览允许合并；PLC 同时只有一个待回复请求，第二个请求明确返回 busy。
- CameraService 同时只触发一组双相机测量或一张 Cam2 Facette，等待完成再启动下一个；等待队列上限 16，超时关闭采集链路，避免迟到帧归属错误。
- ImageStorage 最多接收 16 个任务，满载明确报警；已接收任务不被替换，退出时完成写盘。QSaveFile 保留原子替换行为。
- SDK 缓冲区在驱动出口复制一次；后续 Mat 按只读、引用计数方式交接。不要在页面或消费者中原地修改共享图像。
- 运行结果先交给 PLC，再供页面通过线程安全单槽邮箱读取；UI 不接收无限累积的图像事件。图像刷新 67 ms，诊断刷新至少 200 ms，隐藏页不绘图。
- Camera 预览池同时只有一个任务和一个最新待处理输入；打开图片、图像管线在池中运行。
- 磁盘状态写入复用 StateStore、CameraStateStore、PlcRuntimeStore、DailyLog；全部位于后台服务或测量线程。PLC 参数持久化成功后才确认，保留原协议语义。
- 应用退出先停止运行任务，再关闭相机，最后退出并等待服务线程。不要在 UI 回调中直接调用 SDK 或等待测量。

## 配置与兼容性

PLC 阈值覆盖高于 INI 阈值，基础配置热更新不会清除 PLC 覆盖。在线启动先恢复相机记忆曝光，再重放已持久化的 PLC 曝光；收到 PLC 曝光后，本轮该相机的自动曝光不再覆盖它。自动曝光仍仅用于现有 Idle/Neck 阶段。

算法使用 MeasurementRois 和 MeasurementData，不依赖 PLC 文件格式或数字数组下标。measurement_payload.cpp 唯一负责结果字段排列，SherlockProtocol 负责倍率与帧编码。内部检测的搜索坐标转换保持原实现，所有对外几何仍为原始相机图像坐标。

Facette 沿用“立即协议确认、失败在本机报警”的既有约定，不新增第二次 PLC 完成报文。停止时尚未采到的 Facette 请求明确记录取消；已进入存储的图片完成写盘。

## 新功能应放在哪里

- 新检测阶段/数值：measurement_engine 与 algorithms，并增加测量回归。
- 新 PLC 命令/映射：PlcSession、measurement_payload、SherlockProtocol，不写进页面或算法。
- 新相机能力/采集顺序：CameraService；底层 SDK 接口放 DalsaCamera。
- 新任务调度、超时或运行模式：RuntimeController，并优先添加无界面测试。
- 新图像来源：离线来源/工作线程或相机服务，最终走同一测量提交链路。
- 新文件格式/存储：现有 Store 或 ImageStorage，避免在绘图回调里写盘。
- 静态布局：Designer .ui；页面代码仅绑定控件和显示。

AppSignals 仅保留全局状态提示与退出广播。Home 和 Camera 之间不再通过全局信号互相调用采集逻辑；Camera 页未创建时生产服务仍可独立运行。
