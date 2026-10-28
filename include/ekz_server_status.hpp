#pragma once
#include <QString>
#include <memory>
#include <string>

namespace pva {
// EKZExplorer 4.1.32 的服务器状态通道，和 BMP 刷新事件相互独立。
// 创建、更新、销毁均由应用装配线程负责；固定 32 位句柄布局也适用于 x64 编译。
class EkzServerStatus final {
public:
    explicit EkzServerStatus(std::wstring mappingName = L"EKZExplorer");
    ~EkzServerStatus();
    EkzServerStatus(const EkzServerStatus &) = delete;
    EkzServerStatus &operator=(const EkzServerStatus &) = delete;
    bool start(QString *error = nullptr);
    void setReady(bool ready);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
