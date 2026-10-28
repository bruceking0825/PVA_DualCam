#include "ekz_server_status.hpp"
#include <cstdint>
#include <cstring>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pva {
struct EkzServerStatus::Impl {
    explicit Impl(std::wstring name) : mappingName(std::move(name)) {}
    std::wstring mappingName;
#ifdef _WIN32
    HANDLE mapping{}, ownerMutex{};
    bool ownsMutex{};
    HWND window{};
    std::uint8_t *bytes{};
    static constexpr unsigned Size = 19, ServerWindow = 4, Running = 17, Ready = 18;
    std::uint32_t windowId() const {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window));
    }
    std::uint32_t publishedWindow() const {
        std::uint32_t result{};
        if (bytes) std::memcpy(&result, bytes + ServerWindow, sizeof(result));
        return result;
    }
    void flag(unsigned offset, bool value) {
        reinterpret_cast<volatile std::uint8_t *>(bytes)[offset] = value ? 1 : 0;
    }
#endif
};

EkzServerStatus::EkzServerStatus(std::wstring name) : impl_(std::make_unique<Impl>(std::move(name))) {}
EkzServerStatus::~EkzServerStatus() { stop(); }

bool EkzServerStatus::start(QString *error)
{
    if (error) error->clear();
#ifdef _WIN32
    auto &s = *impl_;
    if (s.window) return true;
    const auto fail = [this, error](const QString &message) {
        if (error) *error = message;
        stop();
        return false;
    };
    const auto systemError = [](const char *operation) {
        return QStringLiteral("EKZ status: %1 failed (Win32 %2)")
            .arg(QString::fromLatin1(operation)).arg(GetLastError());
    };
    // 互斥量只供替代程序防止并发抢占；不改变原版共享区或 Explorer 的字段。
    s.ownerMutex = CreateMutexW(nullptr, FALSE, (s.mappingName + L"_PvaStatusOwner").c_str());
    if (!s.ownerMutex) return fail(systemError("CreateMutex"));
    const auto wait = WaitForSingleObject(s.ownerMutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        return fail(QStringLiteral("EKZ status: another replacement server owns the status channel"));
    s.ownsMutex = true;
    s.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  Impl::Size, s.mappingName.c_str());
    if (!s.mapping) return fail(systemError("CreateFileMapping"));
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    s.bytes = static_cast<std::uint8_t *>(MapViewOfFile(s.mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, Impl::Size));
    if (!s.bytes) return fail(systemError("MapViewOfFile"));
    const auto existing = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(s.publishedWindow()));
    if (s.bytes[Impl::Running] && existing && IsWindow(existing))
        return fail(QStringLiteral("EKZ status: an existing Sherlock server is still running"));
    // 提供有效的消息目标句柄；本次只发布状态，不执行外部采集/退出命令。
    s.window = CreateWindowExW(0, L"STATIC", L"PVA Sherlock status", 0,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s.window) return fail(systemError("CreateWindow"));
    if (created) {
        const std::uint32_t cameraEvents = 0x00650064; // 原服务器默认的两个相机事件值。
        const std::uint32_t mode = 1;
        std::memcpy(s.bytes + 8, &cameraEvents, sizeof(cameraEvents));
        std::memcpy(s.bytes + 12, &mode, sizeof(mode));
    }
    // 保留 Explorer HWND、相机事件、模式和 ExplorerRunning，不清空整块共享内存。
    s.flag(Impl::Ready, false);
    const auto id = s.windowId();
    std::memcpy(s.bytes + Impl::ServerWindow, &id, sizeof(id));
    MemoryBarrier();
    s.flag(Impl::Running, true);
#endif
    return true;
}

void EkzServerStatus::setReady(bool ready)
{
#ifdef _WIN32
    auto &s = *impl_;
    if (s.window && s.bytes && s.publishedWindow() == s.windowId()) {
        MemoryBarrier();
        s.flag(Impl::Ready, ready);
    }
#else
    (void)ready;
#endif
}

void EkzServerStatus::stop()
{
#ifdef _WIN32
    auto &s = *impl_;
    if (s.window && s.bytes && s.publishedWindow() == s.windowId()) {
        s.flag(Impl::Ready, false);
        s.flag(Impl::Running, false);
        const std::uint32_t zero{};
        std::memcpy(s.bytes + Impl::ServerWindow, &zero, sizeof(zero));
        MemoryBarrier();
    }
    if (s.bytes) UnmapViewOfFile(s.bytes);
    if (s.window) DestroyWindow(s.window);
    if (s.mapping) CloseHandle(s.mapping);
    if (s.ownsMutex) ReleaseMutex(s.ownerMutex);
    if (s.ownerMutex) CloseHandle(s.ownerMutex);
    s.bytes = nullptr;
    s.window = nullptr;
    s.mapping = s.ownerMutex = nullptr;
    s.ownsMutex = false;
#endif
}
}
