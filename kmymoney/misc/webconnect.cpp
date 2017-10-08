/*
 * This file is part of KMyMoney, A Personal Finance Manager by KDE
 * Copyright (C) 2017 Thomas Baumgart <tbaumgart@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "webconnect.h"

#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QLocalServer>
#include <QStandardPaths>
#include <QDataStream>
#include <QUrl>

class WebConnect::Private
{
public:
    Private(WebConnect* parent)
    : q(parent)
    , clientSocket(new QLocalSocket(parent))
    , serverSocket(0)
    , server(new QLocalServer(parent))
    , serverFail(false)
    , blockSize(0)
    {
    }

    WebConnect* q;

    QString         socketName;
    QLocalSocket*   clientSocket;
    QLocalSocket*   serverSocket;
    QLocalServer*   server;
    bool            serverFail;
    quint32         blockSize;

    void startup()
    {
        // create a per user socket name
        socketName = QString("%1/KMyMoney-WebConnect").arg(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
        // try to find a server
        if (!connectToServer()) {
            // no other instance seems to be running, so we start the server
            if (!server->listen(socketName)) {
              qWarning() << "WebConnect: starting server failed. Try to remove stale socket.";
              server->removeServer(socketName);
              if(!server->listen(socketName)) {
                qWarning() << "WebConnect: starting server failed again. WebConnect not available.";
                serverFail = true;
              }
            }
        } else {
            clientSocket->disconnectFromServer();
        }
    }

    bool connectToServer()
    {
        // try to find a server
        clientSocket->setServerName(socketName);
        clientSocket->connectToServer();
        return clientSocket->waitForConnected(200);
    }
};

WebConnect::WebConnect(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
    connect(d->clientSocket, &QLocalSocket::connected, this, &WebConnect::serverConnected);
    connect(d->clientSocket, &QLocalSocket::disconnected, this, &WebConnect::serverDisconnected);
    connect(d->server, &QLocalServer::newConnection, this, &WebConnect::clientConnected);

    d->startup();
}

WebConnect::~WebConnect()
{
    if (d->server->isListening()) {
        d->server->close();
    }
    delete d;
}

void WebConnect::clientConnected()
{
    disconnect(d->server, &QLocalServer::newConnection, this, &WebConnect::clientConnected);
    if (!d->serverSocket) {
        d->serverSocket = d->server->nextPendingConnection();
        connect(d->serverSocket, &QLocalSocket::disconnected, this, &WebConnect::clientDisconnected);
        connect(d->serverSocket, &QLocalSocket::readyRead, this, &WebConnect::dataAvailable);
    }
}

void WebConnect::clientDisconnected()
{
    d->serverSocket->deleteLater();
    d->serverSocket = 0;
    if (d->server->hasPendingConnections()) {
        clientConnected();
    } else {
        connect(d->server, &QLocalServer::newConnection, this, &WebConnect::clientConnected);
    }
}

void WebConnect::serverConnected()
{
    d->blockSize = 0;
}

void WebConnect::serverDisconnected()
{
}

void WebConnect::dataAvailable()
{
    QDataStream in(d->serverSocket);
    in.setVersion(QDataStream::Qt_4_0);

    if (d->blockSize == 0) {
        // Relies on the fact that we put the length into the QDataStream
        if (d->serverSocket->bytesAvailable() < (int)sizeof(quint32)) {
            return;
        }
        in >> d->blockSize;
    }

    if (d->serverSocket->bytesAvailable() < (int)sizeof(quint32) || d->serverSocket->atEnd()) {
        return;
    }
    QUrl url;
    in >> url;
    qDebug() << "Webconnect:" << url;
    emit gotUrl(url);
}

void WebConnect::loadFile(const QUrl& url)
{
    if (d->connectToServer()) {
        // transfer filename
        QByteArray block;
        QDataStream stream(&block, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_4_0);
        stream << (quint32) 0;
        stream << url;
        stream.device()->seek(0);
        stream << (quint32)(block.size() - sizeof(quint32));
        d->clientSocket->write(block);
        d->clientSocket->flush();
        d->clientSocket->disconnectFromServer();
    } else {
        qWarning() << "Webconnect loadfile connection failed on client side";
    }
}

bool WebConnect::isClient() const
{
    return !d->server->isListening() && !d->serverFail;
}
