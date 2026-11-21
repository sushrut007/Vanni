#pragma once

namespace Vaani {

class AutoStartManager
{
public:
    static bool isSupported();

    static bool isEnabled();
    static bool setEnabled(bool enabled);
};

} // namespace Vaani