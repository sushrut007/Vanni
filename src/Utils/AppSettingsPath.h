#pragma once

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace Vaani {

inline QString appSettingsIniPath()
{
    const QString appDataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appDataPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absoluteFilePath(QStringLiteral("settings.ini"));
}

class UserStateSettings : public QSettings
{
public:
    UserStateSettings() : QSettings() {}
};

} // namespace Vaani