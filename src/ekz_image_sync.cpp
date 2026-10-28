#include "ekz_image_sync.hpp"
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pva {
EkzImageWriteGuard::EkzImageWriteGuard(const std::wstring &channel)
{
#ifdef _WIN32
    if (channel.empty()) return;
    // 每次重新打开，兼容 Explorer 晚启动或重启；不创建/重置它尚未建立的事件。
    reader_ = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                        (channel + L"Reader_Lock_Name").c_str());
    const DWORD readerError = reader_ ? ERROR_SUCCESS : GetLastError();
    writer_ = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                        (channel + L"Writer_Lock_Name").c_str());
    const DWORD writerError = writer_ ? ERROR_SUCCESS : GetLastError();
    const auto fail = [this](const char *operation, DWORD code) {
        close();
        throw std::runtime_error(std::string("EKZ image synchronization: ") + operation +
                                 " failed (Win32 " + std::to_string(code) + ")");
    };
    if (!reader_ && readerError != ERROR_FILE_NOT_FOUND) fail("OpenEvent(reader)", readerError);
    if (!writer_ && writerError != ERROR_FILE_NOT_FOUND) fail("OpenEvent(writer)", writerError);
    if (!reader_ || !writer_) {
        close();
        return; // Explorer 未运行时，仍正常输出离线 BMP。
    }
    const DWORD result = WaitForSingleObject(reader_, 50);
    if (result == WAIT_TIMEOUT) {
        // 读图未结束则跳过本次覆盖，避免阻塞测量线程或覆盖读取中的图像。
        writable_ = false;
        // 与原写线程超时分支一致，唤醒读取方后等待下一份最新结果。
        if (!SetEvent(writer_)) fail("SetEvent(writer)", GetLastError());
        return;
    }
    if (result != WAIT_OBJECT_0) fail("WaitForSingleObject(reader)", GetLastError());
    if (!ResetEvent(writer_)) fail("ResetEvent(writer)", GetLastError());
    acquired_ = true;
#else
    (void)channel;
#endif
}

EkzImageWriteGuard::~EkzImageWriteGuard()
{
#ifdef _WIN32
    // 两张 BMP 均完成提交后再唤醒 Explorer；异常时也释放握手，防止读图停住。
    if (acquired_) SetEvent(writer_);
#endif
    close();
}

void EkzImageWriteGuard::close() noexcept
{
#ifdef _WIN32
    if (reader_) CloseHandle(reader_);
    if (writer_) CloseHandle(writer_);
#endif
    reader_ = writer_ = nullptr;
}
}
