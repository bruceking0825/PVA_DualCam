#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace pva
{
    struct SherlockCommand
    {
        QString name;
        QStringList parameters;
        QByteArray raw;
    };

    class SherlockProtocol final
    {
    public:
        static constexpr quint16 CommandPort = 5000;
        static constexpr quint16 ResultPort = 5001;
        static constexpr int Scale = 100;
        static constexpr qint64 MaximumPlcNumber = 999900;

        static std::optional<SherlockCommand> parseCommand(const QByteArray &line, QString *error = nullptr);
        static qint64 toPlcNumber(double value);
        static std::optional<double> fromPlcNumber(const QString &text);
        static QByteArray scaledPayload(const QByteArray &name, const std::vector<double> &values);
        static QByteArray framePayload(const QByteArray &payload, QString *error = nullptr);
    };
}
