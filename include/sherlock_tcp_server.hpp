#pragma once

#include "sherlock_protocol.hpp"
#include <QObject>
#include <QPointer>
#include <QQueue>

class QTcpServer;
class QTcpSocket;

namespace pva
{
    class SherlockTcpServer final : public QObject
    {
        Q_OBJECT
    public:
        explicit SherlockTcpServer(QObject *parent = nullptr);
        ~SherlockTcpServer() override;

        bool start(QString *error = nullptr);
        void stop();
        bool sendPayload(const QByteArray &payload, QString *error = nullptr);
        [[nodiscard]] bool fullyConnected() const;

    signals:
        void connectionChanged(bool connected);
        void commandReceived(const pva::SherlockCommand &command);
        void failed(const QString &message);

    private:
        void acceptCommandConnection();
        void acceptResultConnection();
        void readCommands();
        void setCommandSocket(QTcpSocket *socket);
        void setResultSocket(QTcpSocket *socket);
        void flushPendingPackets();
        void updateConnectionState();

        QTcpServer *commandServer_{};
        QTcpServer *resultServer_{};
        QPointer<QTcpSocket> commandSocket_;
        QPointer<QTcpSocket> resultSocket_;
        QByteArray commandBuffer_;
        QQueue<QByteArray> pendingPackets_;
        bool lastConnectionState_{false};
    };
}

Q_DECLARE_METATYPE(pva::SherlockCommand)
