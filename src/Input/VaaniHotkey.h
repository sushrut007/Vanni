#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

// User-chosen dictation chord: 1–3 Windows virtual keys (modifiers and/or F-keys, etc.).
struct VaaniHotkey
{
    QVector<int> virtualKeys;

    bool isEmpty() const noexcept { return virtualKeys.isEmpty(); }
    bool isValid() const noexcept { return !virtualKeys.isEmpty() && virtualKeys.size() <= 3; }

    QString displayName() const;
    QString storageString() const;
    static VaaniHotkey fromStorage(const QString& value, const VaaniHotkey& fallback);
    static VaaniHotkey defaultPushToTalk();

    struct Snapshot {
        bool fullyHeld = false;
        bool partiallyHeld = false;
        bool extraKeyDown = false;
    };
    Snapshot readSnapshot() const;
};

Q_DECLARE_METATYPE(VaaniHotkey)

namespace VaaniReservedKeys {

QString validateHotkey(const VaaniHotkey& hotkey);
QStringList reservedChordDescriptions();

} // namespace VaaniReservedKeys