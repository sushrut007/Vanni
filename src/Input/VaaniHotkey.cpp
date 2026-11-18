#include "Input/VaaniHotkey.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QSet>
#include <algorithm>

namespace {

bool vkDown(int vk)
{
#ifdef Q_OS_WIN
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    Q_UNUSED(vk);
    return false;
#endif
}

bool isModifierVk(int vk)
{
    switch (vk) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x5B:
    case 0x5C:
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
        return true;
    default:
        return false;
    }
}

bool anyNonChordKeyDown(const QSet<int>& chord)
{
#ifdef Q_OS_WIN
    for (int vk = 0x08; vk <= 0xFE; ++vk) {
        if (isModifierVk(vk))
            continue;
        if (chord.contains(vk))
            continue;
        if (vkDown(vk))
            return true;
    }
#endif
    return false;
}

QString vkLabel(int vk)
{
    switch (vk) {
    case 0x10:
    case 0xA0:
    case 0xA1:
        return QStringLiteral("Shift");
    case 0x11:
    case 0xA2:
    case 0xA3:
        return QStringLiteral("Ctrl");
    case 0x12:
    case 0xA4:
    case 0xA5:
        return QStringLiteral("Alt");
    case 0x5B:
    case 0x5C:
        return QStringLiteral("Win");
    default:
        if (vk >= 0x70 && vk <= 0x87)
            return QStringLiteral("F%1").arg(vk - 0x6F);
        if (vk == 0x20)
            return QStringLiteral("Space");
        return QStringLiteral("0x%1").arg(vk, 0, 16);
    }
}

} // namespace

QString VaaniHotkey::displayName() const
{
    if (virtualKeys.isEmpty())
        return QStringLiteral("Off");
    QStringList parts;
    for (int vk : virtualKeys)
        parts.append(vkLabel(vk));
    return parts.join(QStringLiteral(" + "));
}

QString VaaniHotkey::storageString() const
{
    QStringList hex;
    for (int vk : virtualKeys)
        hex.append(QString::number(vk, 16));
    return hex.join(QLatin1Char(','));
}

VaaniHotkey VaaniHotkey::fromStorage(const QString& value, const VaaniHotkey& fallback)
{
    VaaniHotkey hk;
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        bool ok = false;
        const int vk = p.toInt(&ok, 16);
        if (ok && vk > 0)
            hk.virtualKeys.push_back(vk);
    }
    std::sort(hk.virtualKeys.begin(), hk.virtualKeys.end());
    hk.virtualKeys.erase(std::unique(hk.virtualKeys.begin(), hk.virtualKeys.end()),
                         hk.virtualKeys.end());
    if (!hk.isValid())
        return fallback;
    return hk;
}

VaaniHotkey VaaniHotkey::defaultPushToTalk()
{
    VaaniHotkey hk;
    hk.virtualKeys = { 0x77 }; // F8
    return hk;
}

VaaniHotkey::Snapshot VaaniHotkey::readSnapshot() const
{
    Snapshot s;
    if (virtualKeys.isEmpty())
        return s;

    QSet<int> chord(virtualKeys.cbegin(), virtualKeys.cend());
    int downCount = 0;
    for (int vk : virtualKeys) {
        if (vkDown(vk))
            ++downCount;
    }
    s.fullyHeld = downCount == virtualKeys.size();
    s.partiallyHeld = downCount > 0 && !s.fullyHeld;
    s.extraKeyDown = anyNonChordKeyDown(chord);
    return s;
}

namespace VaaniReservedKeys {

QString validateHotkey(const VaaniHotkey& hotkey)
{
    if (hotkey.isEmpty())
        return QStringLiteral("Choose at least one key.");
    if (!hotkey.isValid())
        return QStringLiteral("Use at most three keys.");

    QSet<int> keys(hotkey.virtualKeys.cbegin(), hotkey.virtualKeys.cend());

    if (keys.contains(0x73) && keys.contains(0x12))
        return QStringLiteral("Alt+F4 closes applications — pick another chord.");
    if (keys.contains(0x09) && keys.contains(0x12))
        return QStringLiteral("Alt+Tab switches apps — pick another chord.");
    if (keys.contains(0x4C) && (keys.contains(0x5B) || keys.contains(0x5C)))
        return QStringLiteral("Win+L locks the PC — pick another chord.");
    if (keys.contains(0x1B) && keys.size() == 1)
        return QStringLiteral("Esc alone is not a good push-to-talk key.");

    static const QVector<QPair<int, int>> blocked = {
        { 0x12, 0x73 }, // Alt+F4
        { 0x12, 0x09 }, // Alt+Tab
    };
    for (const auto& pair : blocked) {
        if (keys.contains(pair.first) && keys.contains(pair.second))
            return QStringLiteral("That combination is reserved by Windows.");
    }
    return {};
}

QStringList reservedChordDescriptions()
{
    return { QStringLiteral("Alt+F4 (close window)"),
             QStringLiteral("Alt+Tab (switch app)"),
             QStringLiteral("Win+L (lock workstation)") };
}

} // namespace VaaniReservedKeys