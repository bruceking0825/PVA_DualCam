#include "sherlock_protocol.hpp"

#include <algorithm>
#include <cmath>

namespace pva
{
    std::optional<SherlockCommand> SherlockProtocol::parseCommand(const QByteArray &input, QString *error)
    {
        QByteArray line = input;
        while (line.endsWith('\r') || line.endsWith('\n'))
            line.chop(1);
        if (line.isEmpty())
        {
            if (error)
                *error = "empty command";
            return {};
        }

        const qsizetype separator = line.indexOf('=');
        const QByteArray nameBytes = (separator < 0 ? line : line.left(separator)).trimmed();
        if (nameBytes.isEmpty())
        {
            if (error)
                *error = "missing command name";
            return {};
        }

        SherlockCommand command;
        command.name = QString::fromLatin1(nameBytes);
        command.raw = line;
        if (separator >= 0)
        {
            const auto fields = line.mid(separator + 1).split(';');
            command.parameters.reserve(fields.size());
            for (const auto &field : fields)
                command.parameters.push_back(QString::fromLatin1(field).trimmed());
        }
        return command;
    }

    qint64 SherlockProtocol::toPlcNumber(double value)
    {
        if (!std::isfinite(value) || value <= 0.0)
            return 0;
        const double scaled = std::round(value * Scale);
        return static_cast<qint64>(std::clamp(scaled, 0.0, double(MaximumPlcNumber)));
    }

    std::optional<double> SherlockProtocol::fromPlcNumber(const QString &text)
    {
        bool ok = false;
        const qint64 value = text.trimmed().toLongLong(&ok);
        if (!ok || value <= -MaximumPlcNumber || value >= MaximumPlcNumber)
            return {};
        return double(value) / Scale;
    }

    QByteArray SherlockProtocol::scaledPayload(const QByteArray &name, const std::vector<double> &values)
    {
        QByteArray payload = name;
        payload.append('=');
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (index)
                payload.append(';');
            payload.append(QByteArray::number(toPlcNumber(values[index])));
        }
        return payload;
    }

    QByteArray SherlockProtocol::framePayload(const QByteArray &payload, QString *error)
    {
        const int terminatorSize = payload.size() == 90 ? 3 : 2;
        const int stringSize = payload.size() + terminatorSize;
        if (stringSize > 255)
        {
            if (error)
                *error = QString("Sherlock response is too long: %1 bytes").arg(stringSize);
            return {};
        }

        QByteArray packet;
        packet.reserve(payload.size() + 6);
        packet.append('!');
        packet.append(char(0xff));
        packet.append(char(stringSize));
        packet.append(payload);
        packet.append("\r\n", 2);
        if (payload.size() == 90)
            packet.append('\n');
        return packet;
    }
}
