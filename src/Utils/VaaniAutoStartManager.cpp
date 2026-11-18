#include "Utils/VaaniAutoStartManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

#ifdef Q_OS_WIN
#include <QStringList>
#endif

namespace Vaani {

namespace {

constexpr const char kRunKey[] =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const char kRegistryValueName[] = "Vaani";

QString runCommandLine()
{
    const QString target = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\"").arg(target);
}

} // namespace

bool AutoStartManager::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool AutoStartManager::isEnabled()
{
    if (!isSupported())
        return false;
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return !run.value(QLatin1String(kRegistryValueName)).toString().isEmpty();
}

bool AutoStartManager::setEnabled(bool enabled)
{
    if (!isSupported())
        return false;

    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (!enabled) {
        run.remove(QLatin1String(kRegistryValueName));
        return !isEnabled();
    }

    run.setValue(QLatin1String(kRegistryValueName), runCommandLine());
    return isEnabled();
}

} // namespace Vaani