#pragma once

#include <QMetaType>
#include <deque>

// Session 596 review, B-4: who a dictation result belongs to.
//
// Three parts of the product start dictation and receive its results: the main
// window's editor, the floating bar (which types into other applications) and
// Vocabulary Training. Results used to be broadcast to all three, and each guessed
// whether a result was meant for it -- the floating bar by disconnecting and
// reconnecting the editor around its sessions, the editor with a one-shot "ignore
// the next result" flag set on two of the six ways the bar can stop. The engine
// deliberately delivers results that were still in flight when Stop was pressed,
// so the bar's last phrase landed in the hidden Dictation Pro document, and on
// the other stop paths the flag was left set and swallowed the first phrase of
// the customer's next editor session.
//
// Every result now carries the owner of the session that produced it, and each
// receiver acts only on its own.
enum class RecognitionOwner : int {
    None = 0,             // no session yet: a result for nobody is shown nowhere
    Editor = 1,
    FloatingBar = 2,
    VocabularyTraining = 3,
};
Q_DECLARE_METATYPE(RecognitionOwner)

// Session 599 review, D-3: whether a phrase is dictation or a command is decided by
// the session's Ctrl mode, chosen when the session starts.
//
// FollowKeyboard is every session before hold-to-talk, and stays the default:
// each captured block records whether Ctrl was held (Session 596 review, B-7), so
// holding Ctrl and saying "select all" while dictating runs the command. A
// hold-to-talk session cannot follow the keyboard -- with Ctrl + Win chosen as the
// dictation hotkey (the default is Shift + Win, Session 609 review, K-1), Ctrl is
// held for the whole session and every phrase would be read as a command. It fixes
// the answer instead: the dictation hotkey's session
// is always dictation, the voice-command hotkey's always a command. The speech
// check still runs on everything (D-6). How the audio is cut follows the mode too:
// see wholeUtteranceFor() below (Session 604 review, R-2).
enum class CtrlMode : int {
    FollowKeyboard = 0,
    AlwaysDictation = 1,
    AlwaysCommand = 2,
};
Q_DECLARE_METATYPE(CtrlMode)

// The Ctrl state recorded for a block: the mode's answer, or the key itself.
constexpr bool resolveCtrlHeld(CtrlMode mode, bool physicalCtrlHeld) noexcept
{
    return mode == CtrlMode::AlwaysCommand
        ? true
        : (mode == CtrlMode::AlwaysDictation ? false : physicalCtrlHeld);
}

// Session 604 review, R-2 and Z-15: whether a session's audio is decoded as one
// utterance when it ends, instead of in slices cut at every pause.
//
// Only the dictation hotkey's session (AlwaysDictation). Nothing it produces is
// typed until the keys are released (HeldOutputQueue), so slices buy no earlier
// text -- and short slices give the model less context, which is the accuracy
// loss testing on Windows found. Everything else keeps the slices:
//   - the voice-command hotkey (AlwaysCommand): commands are short, and the
//     pause between "select all" and "bold that" must keep them apart -- one
//     utterance would be "select all bold that", which matches no command;
//   - the editor's and the floating bar's Start/Stop dictation (FollowKeyboard):
//     their text appears while the customer speaks.
constexpr bool wholeUtteranceFor(CtrlMode mode) noexcept
{
    return mode == CtrlMode::AlwaysDictation;
}

// The bookkeeping that stamps results with their session's owner. Results come
// back in the order their slices were sent (the engine's worker runs one queued
// transcription at a time), so a first-in, first-out record of the owner each
// slice was sent for answers "whose result is this?" exactly -- including for a
// result that arrives after Stop, and after the next session has already started.
// Not thread-safe by itself: WhisperRecognizer uses it under its own mutex.
class RecognitionSessions
{
public:
    // A new session for `owner`. Slices sent from now on are this owner's; slices
    // already in flight keep the owner they were sent for.
    void begin(RecognitionOwner owner) noexcept
    {
        m_current = owner;
        ++m_session;
    }
    // A slice was sent for the current session.
    void noteDispatch() { m_inFlight.push_back({ m_current, m_session }); }
    // The owner of the result arriving now. With nothing recorded in flight (a
    // result the bookkeeping did not see sent), the current session's owner.
    RecognitionOwner takeResultOwner()
    {
        if (m_inFlight.empty())
            return m_current;
        const RecognitionOwner owner = m_inFlight.front().owner;
        m_inFlight.pop_front();
        return owner;
    }
    RecognitionOwner currentOwner() const noexcept { return m_current; }
    // The engine was rebuilt: nothing sent before will produce a result.
    void dropInFlight() noexcept { m_inFlight.clear(); }
    // Session 599 review, D-7: the current session was cancelled. Its slices still
    // come back from the engine, in order, but their results now belong to nobody
    // and are shown nowhere -- not even if another session has started meanwhile.
    // An earlier session's slices still in flight are not this session's and keep
    // their owner: the editor's last phrase, stopped a moment before a hotkey
    // session that is then cancelled, still reaches the editor.
    void discardInFlight() noexcept
    {
        for (InFlight& slice : m_inFlight) {
            if (slice.session == m_session)
                slice.owner = RecognitionOwner::None;
        }
    }
    int inFlight() const noexcept { return static_cast<int>(m_inFlight.size()); }

private:
    struct InFlight {
        RecognitionOwner owner;
        unsigned session;
    };
    std::deque<InFlight> m_inFlight;
    RecognitionOwner m_current = RecognitionOwner::None;
    unsigned m_session = 0;     // counts begin(); tells one session's slices from another's
};
