#include "Utils/SingleInstanceGuard.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QWidget>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

SingleInstanceGuard::SingleInstanceGuard(QObject* parent,
                                         const QString& serverName,
                                         QWidget* mainWindow)
    : QObject(parent)
    , _serverName(serverName)
    , _server(new QLocalServer(this))
    , _mainWindow(mainWindow)
{
    if (_server->listen(_serverName)) {
        connect(_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::onNewConnection);
        _isPrimary = true;
        return;
    }

    QLocalSocket probe;
    probe.connectToServer(_serverName);
    if (!probe.waitForConnected(200)) {
        QLocalServer::removeServer(_serverName);
        if (_server->listen(_serverName)) {
            connect(_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::onNewConnection);
            _isPrimary = true;
        }
    } else {
        probe.disconnectFromServer();
        _isPrimary = false;
    }
}

bool SingleInstanceGuard::isPrimary() const
{
    return _isPrimary;
}

void SingleInstanceGuard::setMainWindow(QWidget* mainWindow)
{
    _mainWindow = mainWindow;
    if (_mainWindow && _pendingActivate) {
        _pendingActivate = false;
        bringToForeground();
    }
}

bool SingleInstanceGuard::sendActivateToExistingInstance(const QString& serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(500)) {
        return false;
    }

    const QByteArray message("activate");

#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif

    if (socket.write(message) != message.size()) {
        return false;
    }
    socket.flush();
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(500)) {
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

bool SingleInstanceGuard::requestActivateExisting()
{
    return sendActivateToExistingInstance(_serverName);
}

void SingleInstanceGuard::onNewConnection()
{
    while (_server->hasPendingConnections()) {
        QLocalSocket* client = _server->nextPendingConnection();
        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            const QByteArray data = client->readAll();
            if (data == "activate") {
                if (!_mainWindow) {
                    _pendingActivate = true;
                } else {
                    bringToForeground();
                }
            }
            client->deleteLater();
        });
        connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        connect(client, &QLocalSocket::errorOccurred, client, &QObject::deleteLater);
    }
}

void SingleInstanceGuard::bringToForeground()
{
    emit activateRequested();
    if (_mainWindow) {
        _mainWindow->show();
        _mainWindow->raise();
        _mainWindow->activateWindow();
    }
}