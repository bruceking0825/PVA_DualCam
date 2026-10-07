#pragma once

#include <QByteArray>
#include <QMetaType>
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
        static std::optional<std::vector<double>> parseScaledParameters(
            const SherlockCommand &command, qsizetype expectedCount, QString *error = nullptr);
        static QByteArray scaledPayload(const QByteArray &name, const std::vector<double> &values);
        static QByteArray framePayload(const QByteArray &payload, QString *error = nullptr);
    };
}
Q_DECLARE_METATYPE(pva::SherlockCommand)
