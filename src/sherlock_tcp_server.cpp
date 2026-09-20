#include "sherlock_tcp_server.hpp"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace pva
{
    SherlockTcpServer::SherlockTcpServer(QObject *parent)
        : QObject(parent), commandServer_(new QTcpServer(this)), resultServer_(new QTcpServer(this))
    {
        connect(commandServer_, &QTcpServer::newConnection, this, &SherlockTcpServer::acceptCommandConnection);
        connect(resultServer_, &QTcpServer::newConnection, this, &SherlockTcpServer::acceptResultConnection);
    }

    SherlockTcpServer::~SherlockTcpServer() { stop(); }

    bool SherlockTcpServer::start(QString *error)
    {
        stop();
        if (!commandServer_->listen(QHostAddress::AnyIPv4, SherlockProtocol::CommandPort))
        {
            if (error)
                *error = QString("Cannot listen on TCP %1: %2")
                             .arg(SherlockProtocol::CommandPort)
                             .arg(commandServer_->errorString());
            return false;
        }
        if (!resultServer_->listen(QHostAddress::AnyIPv4, SherlockProtocol::ResultPort))
        {
            if (error)
                *error = QString("Cannot listen on TCP %1: %2")
                             .arg(SherlockProtocol::ResultPort)
                             .arg(resultServer_->errorString());
            commandServer_->close();
            return false;
        }
        return true;
    }

    void SherlockTcpServer::stop()
    {
        commandServer_->close();
        resultServer_->close();
        if (commandSocket_)
            commandSocket_->disconnectFromHost();
        if (resultSocket_)
            resultSocket_->disconnectFromHost();
        commandSocket_.clear();
        resultSocket_.clear();
        commandBuffer_.clear();
        pendingPackets_.clear();
        updateConnectionState();
    }

    bool SherlockTcpServer::fullyConnected() const
    {
        return commandSocket_ && resultSocket_ &&
               commandSocket_->state() == QAbstractSocket::ConnectedState &&
               resultSocket_->state() == QAbstractSocket::ConnectedState;
    }

    bool SherlockTcpServer::sendPayload(const QByteArray &payload, QString *error)
    {
        QString frameError;
        const QByteArray packet = SherlockProtocol::framePayload(payload, &frameError);
        if (packet.isEmpty())
        {
            if (error)
                *error = frameError;
            return false;
        }

        if (!resultSocket_ || resultSocket_->state() != QAbstractSocket::ConnectedState)
        {
            // PLC通常先建立两个连接；短暂的连接先后顺序由队列吸收。
            if (pendingPackets_.size() >= 16)
                pendingPackets_.dequeue();
            pendingPackets_.enqueue(packet);
            if (error)
                *error = "PLC result connection on TCP 5001 is not connected; response queued";
            return false;
        }
        if (resultSocket_->write(packet) != packet.size())
        {
            if (error)
                *error = resultSocket_->errorString();
            return false;
        }
        resultSocket_->flush();
        return true;
    }

    void SherlockTcpServer::acceptCommandConnection()
    {
        while (commandServer_->hasPendingConnections())
            setCommandSocket(commandServer_->nextPendingConnection());
    }

    void SherlockTcpServer::acceptResultConnection()
    {
        while (resultServer_->hasPendingConnections())
            setResultSocket(resultServer_->nextPendingConnection());
        flushPendingPackets();
    }

    void SherlockTcpServer::setCommandSocket(QTcpSocket *socket)
    {
        if (commandSocket_ && commandSocket_ != socket)
            commandSocket_->disconnectFromHost();
        commandSocket_ = socket;
        commandBuffer_.clear();
        connect(socket, &QTcpSocket::readyRead, this, &SherlockTcpServer::readCommands);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]
                {
                    if (commandSocket_ == socket)
                    {
                        commandSocket_.clear();
                        commandBuffer_.clear();
                        updateConnectionState();
                    }
                    socket->deleteLater();
                });
        updateConnectionState();
    }

    void SherlockTcpServer::setResultSocket(QTcpSocket *socket)
    {
        if (resultSocket_ && resultSocket_ != socket)
            resultSocket_->disconnectFromHost();
        resultSocket_ = socket;
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]
                {
                    if (resultSocket_ == socket)
                    {
                        resultSocket_.clear();
                        updateConnectionState();
                    }
                    socket->deleteLater();
                });
        updateConnectionState();
    }

    void SherlockTcpServer::readCommands()
    {
        if (!commandSocket_)
            return;
        commandBuffer_.append(commandSocket_->readAll());
        if (commandBuffer_.size() > 4096)
        {
            commandBuffer_.clear();
            emit failed("PLC command buffer exceeded 4096 bytes and was cleared");
            return;
        }

        while (true)
        {
            const qsizetype carriageReturn = commandBuffer_.indexOf('\r');
            const qsizetype lineFeed = commandBuffer_.indexOf('\n');
            qsizetype end = carriageReturn;
            if (end < 0 || (lineFeed >= 0 && lineFeed < end))
                end = lineFeed;
            if (end < 0)
                break;
            qsizetype terminator = 1;
            if (commandBuffer_.at(end) == '\r' && end + 1 < commandBuffer_.size() &&
                commandBuffer_.at(end + 1) == '\n')
                terminator = 2;

            const QByteArray line = commandBuffer_.left(end);
            commandBuffer_.remove(0, end + terminator);
            if (line.isEmpty())
                continue;
            QString parseError;
            const auto command = SherlockProtocol::parseCommand(line, &parseError);
            if (command)
                emit commandReceived(*command);
            else
                emit failed("Invalid PLC command: " + parseError);
        }
    }

    void SherlockTcpServer::flushPendingPackets()
    {
        if (!resultSocket_ || resultSocket_->state() != QAbstractSocket::ConnectedState)
            return;
        while (!pendingPackets_.isEmpty())
        {
            const QByteArray packet = pendingPackets_.dequeue();
            if (resultSocket_->write(packet) != packet.size())
            {
                emit failed("Cannot flush queued PLC response: " + resultSocket_->errorString());
                break;
            }
        }
        resultSocket_->flush();
    }

    void SherlockTcpServer::updateConnectionState()
    {
        const bool connected = fullyConnected();
        if (connected == lastConnectionState_)
            return;
        lastConnectionState_ = connected;
        emit connectionChanged(connected);
    }
}
