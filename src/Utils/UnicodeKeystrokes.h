#pragma once

#include <QString>
#include <QtGlobal>
#include <vector>

namespace UnicodeKeystrokes {

inline constexpr quint16 kVirtualKeyReturn = 0x0D;
inline constexpr quint16 kVirtualKeyTab = 0x09;

struct Key {
    quint16 virtualKey = 0;
    quint16 unit = 0;
    bool keyUp = false;
};

std::vector<Key> plan(const QString& text);

} // namespace UnicodeKeystrokes