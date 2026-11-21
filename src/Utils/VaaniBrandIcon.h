#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Vaani {

inline QString brandIconResourcePath()
{
    return QStringLiteral(":/icons/vaani-logo.png");
}

inline QIcon brandIcon()
{
    QIcon icon(brandIconResourcePath());
    if (!icon.pixmap(32, 32).isNull())
        return icon;
    return QIcon(QStringLiteral(":/vaani/vaani_app_icon.png"));
}

inline QPixmap brandPixmap(int size)
{
    QPixmap pixmap = brandIcon().pixmap(size, size, QIcon::Normal, QIcon::On);
    if (!pixmap.isNull())
        return pixmap;
    return QPixmap(size, size);
}

} // namespace Vaani