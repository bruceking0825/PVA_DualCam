#include "ekz_server_status.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QUuid>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <windows.h>

namespace {
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Peer {
    HANDLE mapping{};
    std::uint8_t *bytes{};
    explicit Peer(const std::wstring &name, bool create = false) {
        mapping = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 19, name.c_str())
                         : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
        if (mapping) bytes = static_cast<std::uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 19));
    }
    ~Peer() { if (bytes) UnmapViewOfFile(bytes); if (mapping) CloseHandle(mapping); }
    std::uint32_t window() const {
        std::uint32_t id{};
        if (bytes) std::memcpy(&id, bytes + 4, 4);
        return id;
    }
};
bool readFromAnotherProcess(const QString &name, int running, int ready) {
    QProcess peer;
    peer.start(QCoreApplication::applicationFilePath(),
               {"--read", name, QString::number(running), QString::number(ready)});
    return peer.waitForStarted(5000) && peer.waitForFinished(5000) &&
           peer.exitStatus() == QProcess::NormalExit && peer.exitCode() == 0;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() == 5 && args[1] == "--read") {
        Peer peer(args[2].toStdWString());
        return peer.bytes && peer.bytes[17] == args[3].toInt() && peer.bytes[18] == args[4].toInt() ? 0 : 1;
    }
    if (args.size() == 3 && args[1] == "--owner") {
        pva::EkzServerStatus owner(args[2].toStdWString());
        if (!owner.start()) return 1;
        owner.setReady(true);
        std::cout << "READY" << std::endl;
        return application.exec();
    }
    const auto name = QStringLiteral("PvaEkzStatusTest_") + QUuid::createUuid().toString(QUuid::Id128);
    const auto nativeName = name.toStdWString();
    {
        pva::EkzServerStatus server(nativeName);
        QString error;
        check(server.start(&error), "Start with Explorer absent");
        Peer latePeer(nativeName);
        check(latePeer.bytes && latePeer.bytes[17] == 1 && latePeer.bytes[18] == 0,
              "Late Explorer sees Camera is starting");
        check(latePeer.window() && IsWindow(reinterpret_cast<HWND>(std::uintptr_t(latePeer.window()))),
              "Publishes valid fixed-width Windows message target");
        check(readFromAnotherProcess(name, 1, 0), "Independent process sees starting state");
        server.setReady(true);
        check(readFromAnotherProcess(name, 1, 1), "Independent process sees ready state");
        server.start();
        check(latePeer.bytes && latePeer.bytes[18] == 1, "Repeated start preserves readiness");
        pva::EkzServerStatus duplicate(nativeName);
        check(!duplicate.start(&error) && !error.isEmpty(), "Reject concurrent original/replacement server");
        check(readFromAnotherProcess(name, 1, 1), "Rejected instance preserves current server status");
        server.setReady(false);
        check(readFromAnotherProcess(name, 1, 0), "Shutdown first removes readiness");
        server.stop();
        server.setReady(true);
        server.stop();
        check(readFromAnotherProcess(name, 0, 0) && latePeer.window() == 0,
              "Stop clears server-owned state and late ready cannot revive it");
    }
    {
        // 模拟先启动的 Explorer，其他字段必须原样保留。
        Peer explorer(nativeName, true);
        if (!explorer.bytes) return 1;
        const std::uint8_t original[19] = {11,22,33,44, 0,0,0,0, 9,8,7,6, 2,0,0,0, 1,0,0};
        std::memcpy(explorer.bytes, original, 19);
        {
            pva::EkzServerStatus server(nativeName);
            check(server.start(), "Attach to existing Explorer mapping");
            server.setReady(true);
        }
        check(std::memcmp(explorer.bytes, original, 19) == 0,
              "Destructor clears status while preserving Explorer handle, mode and camera events");
        // 模拟服务器异常终止：Windows 回收窗口及互斥量后，重启应能接管旧状态。
        QProcess crashed;
        crashed.start(QCoreApplication::applicationFilePath(), {"--owner", name});
        const bool launched = crashed.waitForStarted(5000) && crashed.waitForReadyRead(5000) &&
                              crashed.readAllStandardOutput().contains("READY");
        check(launched, "Start independent owner process");
        if (launched) {
            pva::EkzServerStatus contender(nativeName);
            check(!contender.start(), "Reject owner in another process");
        }
        crashed.kill();
        crashed.waitForFinished(5000);
        pva::EkzServerStatus recovery(nativeName);
        check(recovery.start(), "Recover stale flags after owner process terminates");
        check(explorer.bytes[17] == 1 && explorer.bytes[18] == 0, "Recovery starts unready");
        recovery.stop();
        check(std::memcmp(explorer.bytes, original, 19) == 0, "Recovery cleanup preserves peer fields");
    }
    return failures ? 1 : 0;
}
