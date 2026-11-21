#pragma once

#include <QString>

// Vaani: inject only during an active external-dictation session; no UI Automation /
// browser-extension pipe (contrast Dictation Pro Utils/TypingTargetRules.h).
namespace VaaniTypingTarget {

enum class BlockReason {
    None,
    NoActiveSession,
    EmptyPhrase,
    NoForegroundWindow,
    OwnProcessForeground,
    ElevatedTarget,
    LikelyPasswordField,
    NoInputFocused,
};

inline QString blockReasonMessage(BlockReason reason)
{
    switch (reason) {
    case BlockReason::None:
        return QString();
    case BlockReason::NoActiveSession:
        return QStringLiteral("Dictation is not active.");
    case BlockReason::EmptyPhrase:
        return QStringLiteral("Nothing to insert.");
    case BlockReason::NoForegroundWindow:
        return QStringLiteral("No window is in the foreground.");
    case BlockReason::OwnProcessForeground:
        return QStringLiteral("Click the application where you want text before dictating.");
    case BlockReason::ElevatedTarget:
        return QStringLiteral("Cannot paste into a window running as administrator.");
    case BlockReason::LikelyPasswordField:
        return QStringLiteral("The focused field may be a password box — paste was not sent.");
    case BlockReason::NoInputFocused:
        return QStringLiteral("No text input field is focused — click where you want to type, or copy the transcribed text below.");
    }
    return QStringLiteral("Text could not be inserted.");
}

// Session gate: every injection path checks this first.
inline bool externalDictationSessionAllowsInjection(bool sessionActive, BlockReason* reason = nullptr)
{
    if (sessionActive)
        return true;
    if (reason)
        *reason = BlockReason::NoActiveSession;
    return false;
}

} // namespace VaaniTypingTarget