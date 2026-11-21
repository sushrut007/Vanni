#include "Settings/VaaniSettingsStyle.h"

#include <QFile>
#include <QWidget>
#include <QGuiApplication>
#include <QStyleHints>

namespace Vaani {

UiTheme effectiveTheme(UiTheme preference)
{
    if (preference == UiTheme::Light)
        return UiTheme::Light;
    if (preference == UiTheme::Dark)
        return UiTheme::Dark;
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark ? UiTheme::Dark : UiTheme::Light;
}

void applySettingsStyle(QWidget* root, UiTheme theme)
{
    if (!root)
        return;
    const UiTheme resolved = effectiveTheme(theme);
    const QString resource =
        resolved == UiTheme::Dark ? QStringLiteral(":/vaani/settings_dark.qss")
                                  : QStringLiteral(":/vaani/settings_light.qss");
    QFile file(resource);
    if (file.open(QIODevice::ReadOnly))
        root->setStyleSheet(QString::fromUtf8(file.readAll()));
}

} // namespace Vaani