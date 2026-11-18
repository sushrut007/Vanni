#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QWidget;

/// Local-socket hand-off between Vaani instances (pattern from Dictation Pro).
class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstanceGuard(QObject* parent,
                                 const QString& serverName,
                                 QWidget* mainWindow = nullptr);

    bool isPrimary() const;

    void setMainWindow(QWidget* mainWindow);

    static bool sendActivateToExistingInstance(const QString& serverName);

    bool requestActivateExisting();

signals:
    void activateRequested();

private slots:
    void onNewConnection();

private:
    void bringToForeground();

    QString _serverName;
    QLocalServer* _server{nullptr};
    QWidget* _mainWindow{nullptr};
    bool _isPrimary{false};
    bool _pendingActivate{false};
};