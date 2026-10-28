# EKZExplorer 右上角服务器状态兼容

本次在原有图片刷新事件兼容之上，新增服务器运行/就绪状态发布。

## 协议

命名文件映射：EKZExplorer，长度 19 字节。固定字段如下（十六进制偏移）：

|偏移|长度|字段|本次行为|
|---|---|---|---|
|00|4|Explorer HWND|保留|
|04|4|Sherlock HWND|发布一个本进程隐藏消息窗口的 32 位句柄|
|08|4|两个相机事件值|已有映射保留；新映射使用原默认 100、101|
|0C|4|模式|已有映射保留；新映射默认 1|
|10|1|ExplorerRunning|保留|
|11|1|SherlockRunning|启动 1，正常退出 0|
|12|1|SherlockReady|初始化中 0，两个服务完成初始化后 1，退出前 0|

ExplorerRunning 是 Explorer 自己维护的状态。服务器发布时不会把共享区整体清零，以免破坏对端窗口句柄、相机选择或模式。

EKZExplorer 的界面判断：

- Running=0：Camera not running。
- Running=1、Ready=0：Camera is starting。
- Running=1、Ready=1：隐藏这两个提示。

状态分支静态地址为 Explorer 0x008D7B12；读取 Ready、Running 的方法分别为 0x00874458、0x0087447C。原服务器在初始化尾部 0x0062AFB6 将 Ready 设置为 1。

## 生命周期

AppServices::start 在启动服务线程前注册状态；运行服务和相机服务完成 initialize 后分别向装配线程确认，两个确认都到达才发布 Ready。Ready 表示服务器组件初始化完成，和原版一样，不等同于正在测量、物理相机已采集或者 PLC 已建立连接。程序处于离线、待机时也可以 Ready。

AppServices::stop 先清 Ready，停止并等待线程退出后清 Running 和服务器窗口句柄。迟到的初始化回调不会把已退出的服务重新标记为 Ready。托盘隐藏不是退出，状态保持。

若发现已有有效的原服务器窗口/运行标志，或另一替代程序持有通道，则拒绝抢占并通过现有状态栏显示错误，保留当前服务器状态。无效旧窗口对应的残留标志可以在下次启动时恢复。

独立隐藏窗口只提供有效的协议句柄；本次不实现相机选择、远程退出或自动重启消息的完整业务处理。异常强制终止不能运行清理逻辑，Explorer 持有的标志可能暂时残留，下一次正常启动/退出会恢复；本次没有增加独立看门狗。

## 验证和使用

- x86 Release 主程序编译。
- ekz_server_status_tests 使用随机映射名和独立子进程，检查启动/就绪/停止、对端字段保留、并发占用拒绝、重复操作、析构清理和异常终止后恢复，不使用现场 EKZExplorer 映射。
- image_storage_tests 回归原有 BMP 输出及事件握手。
- 本机缺少对应 x86 Sapera 运行库，未启动真实相机或真实 EKZExplorer 联调。

现场部署：退出原版 SherlockSrvr，退出旧 PVA，使用新 EXE 替换现有部署目录中的程序，保留原配置及 DLL。PVA 和 EKZExplorer 应在同一 Windows 会话与权限级别运行。预期启动期间显示 starting，初始化后提示消失，正常退出后显示 not running。原图片目录和 Use_Mapfile 配置保持前次修复要求。
