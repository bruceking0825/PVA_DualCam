#pragma once
#include <string>

namespace pva {
// 对接 EKZExplorer 的文件读写握手；作用域必须覆盖两张 BMP 的提交。
class EkzImageWriteGuard final {
public:
    explicit EkzImageWriteGuard(const std::wstring &channel = L"SherlockImage");
    ~EkzImageWriteGuard();
    EkzImageWriteGuard(const EkzImageWriteGuard &) = delete;
    EkzImageWriteGuard &operator=(const EkzImageWriteGuard &) = delete;
    bool canWrite() const { return writable_; }
    bool synchronized() const { return acquired_; }
private:
    void close() noexcept;
    void *reader_{};
    void *writer_{};
    bool writable_{true};
    bool acquired_{};
};
}
