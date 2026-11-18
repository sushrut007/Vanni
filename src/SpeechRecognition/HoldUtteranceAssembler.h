#pragma once

#include "SpeechRecognition/SpeechSliceAssembler.h"

#include <QByteArray>
#include <optional>

// Session 604 review (R-2, Z-4, R-1): the dictation hotkey's audio, collected
// while the keys are held and decoded as ONE utterance when they are released.
//
// A dictation-hotkey session types nothing until the keys are up
// (HeldOutputQueue), so the streaming slicer's short slices bought no earlier
// text -- and each slice gave the model less context, which cost accuracy. The
// recognizer feeds this class instead of SpeechSliceAssembler for such a session
// (wholeUtteranceFor() in RecognitionSessions.h decides which).
//
// Nothing is ever dropped. The speech model reads at most 30 seconds in one call,
// so once the buffer reaches kMaxPartMs it is sent as one whole-utterance part --
// cut at the most recent pause of at least kPauseMs when there is one, so no word
// is split -- and collecting goes on. A 40-second hold arrives as two parts, both
// typed. (The developer version kept only the newest 15 seconds; the first part
// of a long hold was lost without a word.)
//
// At release, takeFinal() hands over the rest: padded with silence to one second
// when it is short and holds speech, nothing when it holds no speech at all
// (silence held on the keys types nothing).
//
// No thread, no engine, no microphone: the tests drive it with synthetic audio.
// Units as SpeechSliceAssembler: 16-bit mono PCM at 16 kHz.
class HoldUtteranceAssembler
{
public:
    using Slice = SpeechSliceAssembler::Slice;

    struct FeedResult {
        // A whole-utterance part to send now (the buffer reached kMaxPartMs).
        std::optional<Slice> part;
        // The first speech of the session: opens the end-of-speech-to-text
        // measurement once per utterance, as the slicer's transition does.
        bool speechStarted = false;
    };

    // Inside the speech model's 30-second window, with room for the padding the
    // model adds itself.
    static constexpr int kMaxPartMs = 28000;
    // The shortest pause a part may be cut at -- the slicer's silence tail.
    static constexpr int kPauseMs = SpeechSliceAssembler::kSilenceTailMs;
    // Silence kept in front of the first speech: the start of a soft first word
    // may read as silence in its own block.
    static constexpr int kLeadInMs = SpeechSliceAssembler::kSilenceTailMs;

    // A new hold: nothing carried over.
    void reset() noexcept;

    // Append one captured block.
    FeedResult feed(const short* samples, int sampleCount);

    // The keys were released: the rest of the hold, or nothing when it holds no
    // speech. The buffer is empty afterwards either way.
    std::optional<Slice> takeFinal();

    qsizetype bufferedBytes() const noexcept { return m_buffer.size(); }
    bool heardSpeech() const noexcept { return m_heardSpeech; }

private:
    Slice sliceOf(const QByteArray& pcm) const;

    QByteArray m_buffer;
    // Speech has been found in m_buffer.
    bool m_heardSpeech = false;
    // speechStarted has been raised for this hold.
    bool m_speechReported = false;
    // Length of the silence at the end of m_buffer, in bytes.
    qsizetype m_trailingSilenceBytes = 0;
    // Where to cut a part: the middle of the most recent pause of at least
    // kPauseMs that followed speech. 0 when there is none.
    qsizetype m_cutAtBytes = 0;
    // Speech was heard after m_cutAtBytes (so the audio after a cut holds speech).
    bool m_speechAfterCut = false;
};
