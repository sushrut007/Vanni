#pragma once

#include <QByteArray>
#include <optional>
#include <vector>

// Session 596 review (B-3, B-7, B-21): how captured audio is cut into slices for
// the speech engine, as one class with no thread, no engine and no microphone.
//
// WhisperRecognizer::processAudio() used to make these decisions inline, under
// its mutex, against a live worker, so none of them could be exercised without a
// speech model -- and three faults sat there unseen:
//
//   B-3  the speech check ran only once the buffer held a full second, and looked
//        at the last 300 ms of it. In steady silence the buffer is cut back to
//        200 ms, so most of each 800 ms of new audio was trimmed without ever
//        being examined; a short "Yes." after a pause could vanish.
//   B-7  the Ctrl key was read once, when a slice was sent, up to 300 ms after the
//        speech ended. While the engine was busy, a sentence and the Ctrl-held
//        command that followed it were collected into ONE slice, and the whole
//        text was treated as a command.
//   B-21 "held" (the engine is behind, so decode greedily) was decided by length
//        (> 15.36 s), and no slice can be that long; the real sign -- audio kept
//        back because the engine was busy -- was known and thrown away.
//
// The recognizer owns one assembler, feeds it every captured block with the Ctrl
// state read for that block and whether its worker is busy, and sends exactly the
// slices it returns. The tests drive the same class with synthetic audio.
//
// Units: 16-bit mono PCM at kSampleRate (the format AudioProcessor delivers).
class SpeechSliceAssembler
{
public:
    // One slice to transcribe, with what is known about how it was spoken.
    struct Slice {
        QByteArray pcm;
        // Ctrl was held during THIS slice's speech (B-7) -- read while the speech
        // was being captured, not when the slice was sent.
        bool ctrlHeld = false;
        // Audio in this slice was kept back because the engine was still busy with
        // the previous one (B-21): the engine is behind the speaker, so the worker
        // decodes it greedily to catch up.
        bool heldBack = false;
    };

    struct FeedResult {
        // In order. Usually empty or one slice; two when a change of Ctrl state
        // closes the held speech first (B-7) and the new buffer is due at once.
        std::vector<Slice> slices;
        // The silence-to-speech transition: the start of a phrase. Raised once per
        // phrase, for the latency measurement (FCR S483, C11 / E10).
        bool speechStarted = false;
    };

    static constexpr int kSampleRate = 16000;
    static constexpr int kBytesPerSample = 2;
    // The shortest slice a silence boundary may produce (FCR S472, A1).
    static constexpr int kMinSliceMs = 1000;
    // How much of the end of the buffer must be quiet before the speaker is
    // treated as having stopped -- and the window every new block is examined in.
    // Also the pause that ends speech spoken with Ctrl held (a command runs at once)
    // and the length the hold-to-talk assembler uses for its own pause and lead-in.
    static constexpr int kSilenceTailMs = 300;
    // Session 624 Pass 9 review, SH-1 (Session 625): how long the speaker must be
    // quiet before dictation streamed with Start/Stop is cut. 300 ms is a breath;
    // about 700 ms is the end of a phrase. Several consecutive quiet 300 ms windows
    // are required, so this is a length of quiet, not a new detection window (the
    // detection window stays kSilenceTailMs, in step with the worker's own check).
    static constexpr int kStreamPauseMs = 700;
    // The ceiling on audio held in one segment (FCR S472, A1; Session 625, SH-1:
    // 15 s to 28 s, so a segment still fits the model's 30 s window). A segment is
    // cut here only when the speaker has not paused at all.
    static constexpr int kMaxHeldMs = 28000;
    // Session 604 review, R-1: a final slice shorter than this is padded with
    // silence to this length when it holds speech. The speech model refuses audio
    // under 100 ms and is unreliable on very short audio; its own advice is to pad.
    static constexpr int kFinalPadMs = 1000;

    static constexpr int bytesForMs(int ms) noexcept
    {
        return (ms * kSampleRate / 1000) * kBytesPerSample;
    }

    // The audio carried into the next slice after a ceiling cut, in ms (100 to
    // 1000). Session 625, SH-1: a cut at a pause carries nothing over -- there is no
    // word to complete -- and the stopwatch step that used to cut every few seconds
    // is gone, so only the overlap is set.
    void setOverlapMs(int keepMs) noexcept;
    int keepMs() const noexcept { return m_keepMs; }

    // A new dictation session: nothing carried over.
    void reset() noexcept;

    // Append one captured block and decide what, if anything, to send now.
    //   ctrlNow     the physical Ctrl state read for this block
    //   workerBusy  the engine has not finished the previous slice
    FeedResult feed(const short* samples, int sampleCount, bool ctrlNow, bool workerBusy);

    // The customer pressed stop: the rest of the buffer as the final slice
    // (Session 604 review, R-1). A rest of at least kFinalPadMs is sent as it is.
    // A shorter one is sent padded with silence to kFinalPadMs when the audio
    // captured since the last slice holds speech -- the "Thanks" said just before
    // the keys are released -- and dropped only when it holds none. It used to be
    // dropped whenever it was under half a second, speech or not. The buffer is
    // empty afterwards either way.
    std::optional<Slice> takeFinal(bool ctrlNow);

    // Drop the buffered audio without sending it (shutdown, model reload).
    void clearAudio() noexcept;

    qsizetype bufferedBytes() const noexcept { return m_buffer.size(); }
    bool heardSpeech() const noexcept { return m_heardSpeech; }

    // True when the newest kSilenceTailMs of buffer holds no speech. False when
    // the buffer is shorter than that (nothing to judge yet).
    static bool tailIsSilence(const QByteArray& buffer);

    // Session 625, SH-1: true when the newest `ms` of buffer holds no speech in any
    // 300 ms window (stepped every 100 ms). False when the buffer is shorter than
    // `ms`. For ms == kSilenceTailMs it agrees with tailIsSilence().
    static bool tailIsSilentFor(const QByteArray& buffer, int ms);

    // Session 604 review, R-1: whether `pcm` (16-bit mono) holds speech in any
    // 300 ms window, stepped every 100 ms -- the same question the worker asks
    // before it runs the model (WhisperDecoding::sliceHoldsSpeech()). Audio
    // shorter than one window is judged whole.
    static bool holdsSpeech(const QByteArray& pcm);

    // Session 604 review, R-1: `pcm` padded with silence at the end to at least
    // kFinalPadMs. Longer audio is returned unchanged.
    static QByteArray padToFinalLength(const QByteArray& pcm);

private:
    Slice takeBuffer(bool ctrlFallback);

    QByteArray m_buffer;
    // How much of the start of m_buffer was carried over from the previous
    // slice (its last keepMs). The final slice's speech check looks only at the
    // audio after it, so the end of a word already sent never counts as new
    // speech (Session 604 review, R-1).
    qsizetype m_carryBytes = 0;
    // Speech has been found in the buffer since the last slice was sent.
    bool m_heardSpeech = false;
    // The Ctrl state recorded during that speech (valid while m_heardSpeech).
    bool m_speechCtrlHeld = false;
    // Audio was kept back for this buffer because the worker was busy.
    bool m_heldBack = false;
    // Where, in m_buffer, the latest block holding speech ended (B-7's cut point).
    qsizetype m_lastSpeechEndBytes = 0;
    int m_keepMs = 200;
};
