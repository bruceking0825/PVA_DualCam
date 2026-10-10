#include "sherlock_tcp_server.hpp"

#include <QAbstractSocket>
#include <QDebug>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace pva
{
    SherlockTcpServer::SherlockTcpServer(QObject *parent, quint16 commandPort, quint16 resultPort)
        : QObject(parent), commandServer_(new QTcpServer(this)), resultServer_(new QTcpServer(this)),
          commandPort_(commandPort), resultPort_(resultPort)
    {
        connect(commandServer_, &QTcpServer::newConnection, this, &SherlockTcpServer::acceptCommandConnection);
        connect(resultServer_, &QTcpServer::newConnection, this, &SherlockTcpServer::acceptResultConnection);
        for (auto *server : {commandServer_, resultServer_})
            connect(server, &QTcpServer::acceptError, this, [this, server](QAbstractSocket::SocketError) {
                communicationError(QString("Sherlock TCP %1 accept error: %2")
                    .arg(server->serverPort()).arg(server->errorString()));
            });
    }

    SherlockTcpServer::~SherlockTcpServer() { stop(); }

    bool SherlockTcpServer::start(QString *error)
    {
        if (commandServer_->isListening() && resultServer_->isListening()) return true;
        stop();
        if (!commandServer_->listen(QHostAddress::AnyIPv4, commandPort_))
        {
            if (error)
                *error = QString("Cannot listen on TCP %1: %2")
                             .arg(commandPort_)
                             .arg(commandServer_->errorString());
            return false;
        }
        if (!resultServer_->listen(QHostAddress::AnyIPv4, resultPort_))
        {
            if (error)
                *error = QString("Cannot listen on TCP %1: %2")
                             .arg(resultPort_)
                             .arg(resultServer_->errorString());
            commandServer_->close();
            return false;
        }
        emit logMessage(QString("Sherlock service started on TCP %1/%2; waiting for PLC connection")
            .arg(commandPort()).arg(resultPort()));
        return true;
    }

    void SherlockTcpServer::stop()
    {
        const bool wasListening = commandServer_->isListening() || resultServer_->isListening();
        commandServer_->close();
        resultServer_->close();
        closeConnections();
        if (wasListening) emit logMessage("Sherlock service closed");
    }

    void SherlockTcpServer::closeConnections()
    {
        const auto command = commandSocket_;
        const auto result = resultSocket_;
        commandSocket_.clear();
        resultSocket_.clear();
        commandBuffer_.clear();
        pendingPackets_.clear();
        for (auto socket : {command, result})
            if (socket) {
                emit logMessage(QString("PLC TCP %1 disconnected: %2:%3")
                    .arg(socket->property("serverPort").toUInt())
                    .arg(socket->property("peerAddress").toString()).arg(socket->property("peerPort").toUInt()));
                socket->abort();
                socket->deleteLater();
            }
        updateConnectionState();
        if ((command || result) && commandServer_->isListening() && resultServer_->isListening())
            emit logMessage("Sherlock service waiting for PLC connection");
    }

    void SherlockTcpServer::communicationError(const QString &message)
    {
        emit failed(message);
        closeConnections();
    }

    bool SherlockTcpServer::fullyConnected() const
    {
        return commandSocket_ && resultSocket_ &&
               commandSocket_->state() == QAbstractSocket::ConnectedState &&
               resultSocket_->state() == QAbstractSocket::ConnectedState;
    }
    quint16 SherlockTcpServer::commandPort() const { return commandServer_->serverPort(); }
    quint16 SherlockTcpServer::resultPort() const { return resultServer_->serverPort(); }

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
        // 打印协议有效内容；二进制帧头和CR/LF不作为ASCII文本输出。
        qDebug().noquote() << "PLC TX ASCII:" << QString::fromLatin1(payload);
        if (resultSocket_->write(packet) != packet.size())
        {
            const QString message = "PLC response write failed: " + resultSocket_->errorString();
            if (error)
                *error = message;
            communicationError(message);
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
            closeConnections();
        commandSocket_ = socket;
        socket->setProperty("serverPort", socket->localPort());
        socket->setProperty("peerAddress", socket->peerAddress().toString());
        socket->setProperty("peerPort", socket->peerPort());
        emit logMessage(QString("PLC TCP 5000 connected: %1:%2")
            .arg(socket->peerAddress().toString()).arg(socket->peerPort()));
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            if (commandSocket_ == socket) communicationError("PLC TCP 5000 error: " + socket->errorString());
        });
        commandBuffer_.clear();
        connect(socket, &QTcpSocket::readyRead, this, &SherlockTcpServer::readCommands);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]
                {
                    if (commandSocket_ == socket)
                    {
                        closeConnections();
                    }
                    socket->deleteLater();
                });
        updateConnectionState();
    }

    void SherlockTcpServer::setResultSocket(QTcpSocket *socket)
    {
        if (resultSocket_ && resultSocket_ != socket)
            closeConnections();
        resultSocket_ = socket;
        socket->setProperty("serverPort", socket->localPort());
        socket->setProperty("peerAddress", socket->peerAddress().toString());
        socket->setProperty("peerPort", socket->peerPort());
        emit logMessage(QString("PLC TCP 5001 connected: %1:%2")
            .arg(socket->peerAddress().toString()).arg(socket->peerPort()));
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            if (resultSocket_ == socket) communicationError("PLC TCP 5001 error: " + socket->errorString());
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]
                {
                    if (resultSocket_ == socket)
                    {
                        closeConnections();
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
            communicationError("PLC command buffer exceeded 4096 bytes");
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
            {
                qDebug() << "PLC empty line ignored";
                continue;
            }
            QString parseError;
            const auto command = SherlockProtocol::parseCommand(line, &parseError);
            if (command)
                emit commandReceived(*command);
            else {
                communicationError("Invalid PLC command: " + parseError);
                return;
            }
        }
    }

    void SherlockTcpServer::flushPendingPackets()
    {
        if (!resultSocket_ || resultSocket_->state() != QAbstractSocket::ConnectedState)
            return;
        while (!pendingPackets_.isEmpty())
        {
            const QByteArray packet = pendingPackets_.dequeue();
            // 队列中的响应在真正补发时同样打印，避免把“入队”误认为已经发送。
            qDebug().noquote() << "PLC TX queued hex:" << packet.toHex(' ');
            if (resultSocket_->write(packet) != packet.size())
            {
                communicationError("Cannot flush queued PLC response: " + resultSocket_->errorString());
                return;
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
        emit logMessage(connected ? "PLC dual connection established" : "PLC dual connection lost");
        emit connectionChanged(connected);
    }
}
