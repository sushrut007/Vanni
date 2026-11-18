#include "Core/VaaniCoreController.h"
#include "Utils/SingleInstanceGuard.h"
#include "Utils/VaaniBrandIcon.h"
#include "Utils/VaaniColdStart.h"

#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QSharedMemory>
#include <QSystemTrayIcon>
#include <QThread>

namespace {

constexpr const char kIpcServerName[] = "Vaani.SingleInstance";
constexpr const char kSharedMemoryKey[] = "VaaniUniqueKey";

bool handOffToRunningCopyWithRetry(const QString& serverName)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (SingleInstanceGuard::sendActivateToExistingInstance(serverName)) {
            return true;
        }
        QThread::msleep(50);
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("com.vaani"));
    QApplication::setOrganizationDomain(QStringLiteral("vaani"));
    QApplication::setApplicationName(QStringLiteral("Vaani"));
    QApplication::setApplicationDisplayName(QStringLiteral("Vaani"));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(Vaani::brandIcon());

    Vaani::ColdStart::logBegin();
    Vaani::ColdStart::PhaseTimer phase;

    const QString serverName = QString::fromLatin1(kIpcServerName);

    if (SingleInstanceGuard::sendActivateToExistingInstance(serverName)) {
        return 0;
    }

    QSharedMemory singleInstanceLock(
        QSharedMemory::legacyNativeKey(QString::fromLatin1(kSharedMemoryKey)));
    if (!singleInstanceLock.create(1)) {
        if (singleInstanceLock.error() == QSharedMemory::AlreadyExists) {
            if (handOffToRunningCopyWithRetry(serverName))
                return 0;
            // Stale lock from a crashed previous run — reclaim and continue.
            if (singleInstanceLock.attach()) {
                singleInstanceLock.detach();
            }
            QLocalServer::removeServer(serverName);
            if (!singleInstanceLock.create(1)) {
                qWarning() << "[Vaani] Could not create single-instance lock:"
                           << singleInstanceLock.errorString();
            }
        } else {
            qWarning() << "[Vaani] Single-instance lock:" << singleInstanceLock.errorString();
        }
    }
    Vaani::ColdStart::logPhase("single_instance", phase.elapsedMs());

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "System tray is not available on this platform.";
        return 1;
    }

    phase.restart();
    VaaniCoreController core;
    Vaani::ColdStart::logPhase("core_controller_ctor", phase.elapsedMs());

    SingleInstanceGuard guard(&app, serverName, nullptr);
    core.attachSingleInstanceGuard(&guard);
    QObject::connect(&guard, &SingleInstanceGuard::activateRequested, &core,
                     &VaaniCoreController::handleSecondInstanceActivate);
    QObject::connect(&app, &QApplication::aboutToQuit, &core,
                     &VaaniCoreController::prepareForQuit);

    phase.restart();
    core.start();
    Vaani::ColdStart::logPhase("tray_show", phase.elapsedMs());

    Vaani::ColdStart::logEnd();
    return app.exec();
}