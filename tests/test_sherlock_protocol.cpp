#include "sherlock_protocol.hpp"

#include <QByteArray>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool condition, const char *message)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

int main()
{
    using pva::SherlockProtocol;

    QString error;
    const auto command = SherlockProtocol::parseCommand("dia_thr=5000\r\n", &error);
    check(command.has_value(), "command parses");
    check(command && command->name == "dia_thr", "command name");
    check(command && command->parameters == QStringList{"5000"}, "command parameter");
    check(SherlockProtocol::fromPlcNumber("5000") == 50.0, "PLC input scale");
    check(SherlockProtocol::toPlcNumber(389.3899) == 38939, "PLC output scale");
    check(SherlockProtocol::toPlcNumber(-1.0) == 0, "negative output clamps to zero");
    check(SherlockProtocol::toPlcNumber(20000.0) == SherlockProtocol::MaximumPlcNumber,
          "large output clamps to protocol maximum");

    const QByteArray payload = "dip=1234";
    const QByteArray packet = SherlockProtocol::framePayload(payload, &error);
    check(packet.size() == payload.size() + 5, "ordinary frame size");
    check(static_cast<unsigned char>(packet[0]) == 0x21, "frame marker");
    check(static_cast<unsigned char>(packet[1]) == 0xff, "frame maximum-length byte");
    check(static_cast<unsigned char>(packet[2]) == payload.size() + 2, "frame actual-length byte");
    check(packet.mid(3) == payload + "\r\n", "ordinary frame body");

    const QByteArray ninety(90, 'x');
    const QByteArray special = SherlockProtocol::framePayload(ninety, &error);
    check(static_cast<unsigned char>(special[2]) == 93, "90-byte workaround length");
    check(special.endsWith("\r\n\n"), "90-byte workaround terminator");

    return failures == 0 ? 0 : 1;
}
