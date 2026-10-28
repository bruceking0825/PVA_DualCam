# EKZExplorer 图像刷新兼容修复

## 原因与证据

EKZExplorer 4.1.32 的 TSherlockImage 通过独立线程读取 BMP，除了文件路径，还使用两个 Windows 命名事件：

- SherlockImageReader_Lock_Name：手动复位事件，读图完成/允许写入。
- SherlockImageWriter_Lock_Name：自动复位事件，写图完成/允许读取。

Explorer 0x008734DC 将通道名与 Reader_Lock_Name、Writer_Lock_Name 拼接；0x00875F94 使用 SherlockImage 通道；0x00873804 的读线程等待 Writer 事件，重置 Reader，加载图像后设置 Reader。
原服务器 0x00628C30 建立相同事件；0x00628EA0 的写线程等待 Reader，重置 Writer，保存图像后设置 Writer。只覆盖 image1.bmp/image2.bmp，没有通知 Writer 事件，会进入超时/恢复路径，表现为较长时间才更新；原服务器启动后提供了缺失的事件通知。

之前静态报告中的 PostMessageW、19 字节共享状态属于另外的控制/状态通道。本次修复针对图像读写事件，不宣称完整替代所有服务器生命周期和状态接口。

## 实现

新增 EkzImageWriteGuard，在 ImageStorage 的测量 BMP 写入作用域内使用：

1. 每批图片重新 OpenEvent，兼容 Explorer 先启动、晚启动、退出和重启。
2. Explorer 不存在时按原方式输出 BMP，不创建事件或启动外部程序。
3. 等待 Reader 最多 50 ms；成功后重置 Writer。
4. image1.bmp、image2.bmp 仍使用现有 QSaveFile 提交和失败重试。
5. 成对写入作用域结束时 SetEvent(Writer)，异常时也释放握手。
6. Reader 忙碌时跳过本批覆盖，并按原写线程超时分支唤醒读线程；后续周期继续处理最新结果。

没有修改测量算法、PLC TCP 协议、相机触发或现场 EKZExplorer 配置。此握手不等价于两份文件组成一个原子事务，单张提交失败时保留原有错误/重试策略。图像不是文件监视器触发，也不需要每个周期发送相机通知消息 0xBD0。

## 验证

- MSVC 14.29 / Qt 5.15.19 / OpenCV 4.14，x86 Release 主程序编译通过。
- image_storage_tests 通过：保留已有图片输出、覆盖、文件锁和异常测试。
- 新增真实 Win32 命名事件测试，使用随机独立通道，不连接现场 Explorer：无读者、晚启动、写入中事件保持复位、完成后触发、异常释放、两张图片可解码、读者忙时不覆盖、读完恢复、退出后继续写图。
- 本机缺少对应 x86 Sapera 运行库，未启动主程序或相机。
- 未运行真实 EKZExplorer 的端到端验收，测试通过不代表已现场确认。

## 使用与现场验收

1. 使用本次编译的完整部署目录；已有匹配 DLL 的部署可只替换 SherlockSrvr.exe。保留实际部署的 cnf.ini。
2. 退出原版 SherlockSrvr.exe，避免两个写者竞争同名文件与事件。
3. 确认 EKZExplorer.ini 的 Camera_1 Use_Mapfile=0，Filename1/2 指向 R:\TEMP\image1.bmp 和 image2.bmp，并与 PVA 的 measurementImageDir 对应。本次没有自动改任何配置。
4. 让 EKZExplorer 与 PVA 在同一 Windows 用户会话及相同权限级别运行。
5. 启动 PVA 并进入正常周期，确认 Explorer 图像随 BMP 更新，不需要启动原服务器。
6. 分别验证 Explorer 先启动、后启动和重启。原界面读图线程有自身节流，不能保证每个采集帧都显示。

此修复只解决 BMP 图像刷新；Explorer 的服务器就绪指示、自动重启和完整消息协议仍是独立兼容项。
