#include "Utils/UnicodeKeystrokes.h"

namespace UnicodeKeystrokes {

std::vector<Key> plan(const QString& text)
{
    std::vector<Key> keys;
    keys.reserve(static_cast<size_t>(text.size()) * 2);
    const auto press = [&keys](quint16 virtualKey, quint16 unit) {
        keys.push_back(Key{ virtualKey, unit, false });
        keys.push_back(Key{ virtualKey, unit, true });
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t u = text.at(i).unicode();
        if (u == u'\r') {
            press(kVirtualKeyReturn, 0);
            if (i + 1 < text.size() && text.at(i + 1).unicode() == u'\n')
                ++i;
            continue;
        }
        if (u == u'\n') {
            press(kVirtualKeyReturn, 0);
            continue;
        }
        if (u == u'\t') {
            press(kVirtualKeyTab, 0);
            continue;
        }
        press(0, static_cast<quint16>(u));
    }
    return keys;
}

} // namespace UnicodeKeystrokes