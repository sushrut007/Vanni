#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include "AudioProcessing/AdaptiveProcessingManager.h"
#include "SpeechRecognition/SileroVad.h"

extern "C" {
#include "whisper.h"
}

// Session 579 review (S-3 to S-6): the decisions this worker takes about one
// slice of audio, as pure functions of plain values. The worker calls them with
// what the speech library returned; the tests call them with chosen values, so
// the rules are exercised without a speech model or a microphone.
namespace WhisperDecoding {

// One frame of the model's audio context is 20 ms: 320 samples at 16 kHz.
inline constexpr int kSamplesPerAudioCtxFrame = 320;
// The short window read for ordinary slices: 768 frames = 15.36 s.
inline constexpr int kShortAudioCtxFrames = 768;
inline constexpr int kShortAudioCtxSamples = kShortAudioCtxFrames * kSamplesPerAudioCtxFrame;

// The confidence of a transcription, 0..1: the geometric mean of the text
// tokens' probabilities, exp of their average log-probability. tokenIds and
// probabilities run in parallel; ids at or above eotToken are the model's
// control tokens, not speech, and are left out. No text tokens gives 0.
float geometricMeanConfidence(const std::vector<int>& tokenIds,
                              const std::vector<float>& probabilities,
                              int eotToken);

// True when the slice is longer than the short window, so the model's full window
// must be read. (Session 596 review, B-21: this is about the WINDOW only. It used
// to double as the test for "held", and no slice can be this long -- the recognizer
// sends at 15 s in 1,024-sample blocks, 15.06 s at most -- so that test never fired.)
constexpr bool needsFullAudioWindow(long long samples) noexcept
{
    return samples > kShortAudioCtxSamples;
}

// The audio context for a slice: the short window up to 15.36 s, the model's
// full window (0) beyond it, so nothing after 15.36 s of a long slice is skipped.
constexpr int audioContextFor(long long samples) noexcept
{
    return needsFullAudioWindow(samples) ? 0 : kShortAudioCtxFrames;
}

// Beam search when the preset asks for a beam wider than 1, except for a held-back
// slice (Session 596 review, B-21): audio the recognizer kept back because the
// engine was still busy with the previous slice. That -- not the slice's length --
// is the sign that the engine is behind the speaker, and greedy decoding lets it
// catch up.
constexpr bool useBeamSearch(int presetBeamSize, bool heldBack) noexcept
{
    return presetBeamSize > 1 && !heldBack;
}

// How one slice is decoded (Session 604 review, Z-12). A whole utterance -- the
// dictation hotkey's audio, decoded at release (R-2) -- is one segment with no
// earlier text as context; its search strategy still follows the preset and the
// held-back rule, like every other slice. The developer version forced beam
// search for every whole utterance, which is the slowest case on the slowest PC,
// before the end-of-speech-to-text time there has been measured.
struct SliceDecoding {
    bool beamSearch = false;
    bool singleSegment = false;
    bool noContext = false;
};
constexpr SliceDecoding decodingFor(int presetBeamSize, bool heldBack, bool wholeUtterance,
                                    bool ctrlHeld) noexcept
{
    return SliceDecoding{ useBeamSearch(presetBeamSize, heldBack), wholeUtterance,
                          wholeUtterance || ctrlHeld };
}

// The second speech check, made by the worker before the model is run (Session
// 596 review, B-2). The model invents short phrases ("Thank you.", "Bye.") from
// noise, and this check is what stops that, so it stays -- but it asks the same
// question the recognizer asked when it cut the slice: does ANY 300 ms window
// hold speech? It used to average the whole slice, and a three-second slice of
// quiet speech with pauses averages below the threshold even when its loudest
// 300 ms does not, so speech the recognizer had accepted was thrown away.
bool sliceHoldsSpeech(const std::vector<float>& pcm);

// The speech library's abort callback (Session 596 review, B-45): data points to
// the worker's std::atomic<bool> abort flag. Returns true to stop a running
// transcription.
bool abortRequested(void* data);

// The text of one slice, ready for the next step (Session 579 review, S-2).
// Whisper's own markers and hallucinations are removed (cleanWhisperOutput).
// Full stops INSIDE the text are kept: prices, decimals, web addresses,
// abbreviations and the stop between two sentences. On the post-processed
// (Latin, dictation) path only the stop at the very END is removed, so the
// post-processor alone decides whether the slice ends a sentence. Voice-command
// (raw) text and the non-Latin path keep Whisper's stops as they are.
QString cleanedSliceText(const QString& whisperText, bool rawMode, bool postProcessedPath);

// Copy a processing preset into the decoding parameters: its beam size, the
// no-speech threshold for its voice-activity setting, temperature 0, and no
// token limit (Session 579 review, S-3).
void applyPreset(whisper_full_params& params, const ProcessingConfig& preset, float noSpeechThreshold);

} // namespace WhisperDecoding

// Forward declarations for new components
class AdvancedNoiseReduction;
class AntiRepetitionManager;
class OptimizedPostProcessor;

// Forward declare ProcessingConfig instead of redefining it
struct ProcessingConfig;

// Session 609 review (loop-back): every speech-library call the worker makes on a
// slice, in one table. The product uses library(). A check passes its own table
// to prove what the worker asks of the library -- Silero on or off, the second
// decode after a failed one, what is reported -- on machines where the library
// itself cannot run.
struct WhisperCalls {
    whisper_full_params (*defaultParams)(whisper_sampling_strategy) = nullptr;
    int (*full)(whisper_context*, whisper_full_params, const float*, int) = nullptr;
    int (*nSegments)(whisper_context*) = nullptr;
    const char* (*segmentText)(whisper_context*, int) = nullptr;
    int (*nTokens)(whisper_context*, int) = nullptr;
    whisper_token (*tokenId)(whisper_context*, int, int) = nullptr;
    float (*tokenP)(whisper_context*, int, int) = nullptr;
    whisper_token (*tokenEot)(whisper_context*) = nullptr;
    // True when the library loads the file as a Silero model.
    bool (*sileroLoads)(const std::string&) = nullptr;

    static WhisperCalls library();
};

class WhisperWorker : public QObject
{
    Q_OBJECT

public:
    explicit WhisperWorker(whisper_context* ctx, QObject* parent = nullptr);
    // For the checks: the same worker over a stand-in for the library.
    WhisperWorker(whisper_context* ctx, const WhisperCalls& calls, QObject* parent = nullptr);
    // Declared here and defaulted in the .cpp, where AntiRepetitionManager is a
    // complete type. Left implicit, the destructor is generated inline wherever
    // it is needed -- including the meta-object code moc writes for this class --
    // and destroying a std::unique_ptr to a forward-declared type there does not
    // compile under GCC or Clang.
    ~WhisperWorker() override;

public slots:
    // Language / state management -- promoted to slots so WhisperRecognizer can
    // safely marshal these calls via QMetaObject::invokeMethod(QueuedConnection)
    // across the worker-thread boundary. Raw cross-thread calls on these members
    // could race against an in-progress transcription holding m_contextMutex.
    void setLanguage(const QString& language);
    void updateProcessingConfig(const ProcessingConfig& config);
    void resetState();

    // Transcription entry points (always called via QueuedConnection)
    // heldBack (Session 596 review, B-21): the recognizer kept this audio back
    // because the previous slice was still being transcribed; it is decoded greedily.
    // wholeUtterance (Session 604 review, R-2): the dictation hotkey's audio,
    // decoded as one utterance (WhisperDecoding::decodingFor()).
    void transcribeWithTokens(const QByteArray& pcmBuffer, const QVector<whisper_token>& promptTokens,
                              bool ctrlHeld = false, bool heldBack = false,
                              bool wholeUtterance = false);
    void transcribe(const QByteArray& pcmBuffer);
    void setRawMode(bool enabled);
    // FCR S472 (A1): let held audio go, at a session boundary only. A slot
    // rather than a private helper because the recogniser has to be able to say
    // "the customer has stopped" from its own thread -- and because keeping it
    // separate from resetState() is what stops a mid-session recovery quietly
    // becoming a place where speech is thrown away.
    void discardPendingAudio();

public:
    // FCR S472 (A1) -----------------------------------------------------------
    // True while a transcription is in flight. WhisperRecognizer reads this to
    // decide whether to hand over the slice it has just filled or to keep
    // collecting into the same buffer, so that speech is never handed to a
    // worker that cannot take it yet. See processAudio() for the reasoning.
    bool isBusy() const noexcept { return m_busy.load(std::memory_order_acquire); }

    // Total captured audio, in bytes, that this worker has had to let go of
    // because the pending slot reached its ceiling. On a healthy machine this
    // stays at zero for the life of the process; any non-zero value means the
    // model cannot keep up with speech on this hardware, which is the one fact
    // that makes A1 diagnosable across machines rather than one at a time.
    qint64 droppedAudioBytes() const noexcept
    {
        return m_droppedAudioBytes.load(std::memory_order_relaxed);
    }

    // Session 596 review, B-45: stop a running transcription now and refuse new
    // ones. Safe from any thread (one atomic store) -- WhisperRecognizer calls it
    // from the interface thread at shutdown, while this worker's own thread may be
    // inside whisper_full(), which polls the flag through params.abort_callback.
    void requestAbort() noexcept { m_abortRequested.store(true, std::memory_order_release); }
    bool isAbortRequested() const noexcept { return m_abortRequested.load(std::memory_order_acquire); }

    // Session 609 review, W-1: the Silero model the decode uses
    // (SileroVad::locateModel(); empty when there is none). Set once, by the
    // recognizer, before this worker is moved to its thread.
    void setSileroModelPath(const QString& path) { m_sileroModelPath = path.toStdString(); }
    SileroVad::Availability::State sileroState() const noexcept { return m_silero.state(); }

signals:
    void resultReady(const QString& text, bool isFinal);
    void errorOccurred(const QString& message);
    void tokensReady(const QVector<whisper_token>& tokens);
    void confidenceReady(float confidence);
    // Session 625 (SH-2): a short notice for the customer, shown once per session.
    void noticeReady(const QString& message);

private:
    whisper_context* m_ctx = nullptr;
    QString m_language = "en";
    std::atomic<bool> m_busy{ false };
    // Session 596 review, B-45: set once, at application exit; never cleared.
    std::atomic<bool> m_abortRequested{ false };
    mutable std::mutex m_contextMutex;
    std::atomic<bool> m_rawMode{ false };
    // Enhanced components
    std::unique_ptr<AdvancedNoiseReduction> m_noiseReduction;
    std::unique_ptr<AntiRepetitionManager> m_antiRepetition;
    // Bug 1 fix: promoted from function-static to member so sentence-context
    // state persists across transcription calls within the same session.
    std::unique_ptr<OptimizedPostProcessor> m_postProcessor;
    // Bug 1 fix: tracks last emitted text for symmetric dedup comparisons.
    QString m_lastEmittedText;
    // Session 625 (SH-2): the "could not write this in the selected script" notice
    // has been sent in this session. Worker thread only.
    bool m_scriptNoticeShown = false;
    ProcessingConfig m_currentConfig;
    // Session 609 review, W-1: Silero, checked once on the first slice.
    std::string m_sileroModelPath;
    SileroVad::Availability m_silero;
    WhisperCalls m_lib;

    static constexpr float NO_SPEECH_THRESHOLD = 0.6f;

    // FCR S472 (A1, cause 2) — the single pending slot.
    //
    // What the correction document describes: a slice arriving while the worker
    // is busy was returned from immediately and the audio ceased to exist.
    // What the delivered code actually does: transcribeWithTokens() is reachable
    // only as a queued slot on this worker's own thread, and the two other
    // writers of m_busy (resetState, setLanguage) are queued slots on that same
    // thread, so the compare-exchange below cannot fail and nothing was being
    // dropped here. The slices were QUEUED instead, one per step interval, for
    // as long as the model stayed behind -- an unbounded backlog rather than a
    // loss. The customer-visible result is the same report (text arriving later
    // and later, then a tail that never arrives once dictation stops), so the
    // fix the document asks for is the right one; only the mechanism is
    // restated. Back-pressure now lives in WhisperRecognizer::processAudio(),
    // which keeps the audio rather than posting a slice the worker cannot take.
    //
    // This slot is the second line of defence and the thing that makes "never
    // discard" true by construction: if a slice does arrive while a
    // transcription is running -- today only possible through a future second
    // producer -- it is held and merged into the next transcription instead of
    // being returned from. It is bounded, because Whisper itself reads at most
    // 30 seconds of audio in one call, so a slot larger than that could not be
    // transcribed anyway.
    std::mutex m_pendingMutex;
    QByteArray m_pendingPcm;
    QVector<whisper_token> m_pendingTokens;
    bool m_pendingCtrlHeld = false;
    // Session 604 review, R-2: the held audio contains a whole utterance. Merged
    // audio is held back by definition, so it is decoded greedily either way.
    bool m_pendingWholeUtterance = false;
    bool m_hasPending = false;
    std::atomic<qint64> m_droppedAudioBytes{ 0 };

    static constexpr int kSampleRate = 16000;
    static constexpr int kBytesPerSample = static_cast<int>(sizeof(qint16));
    // Whisper's own context window. Audio beyond this in a single call is not
    // transcribed, so holding more than this would not preserve anything.
    static constexpr int kMaxPendingBytes = 30 * kSampleRate * kBytesPerSample;

    // Hold a slice that arrived while a transcription was running, merging it
    // with anything already held.
    void holdForLater(const QByteArray& pcmBuffer,
                      const QVector<whisper_token>& promptTokens,
                      bool ctrlHeld, bool wholeUtterance);
    // A merged slot is held-back audio by definition (Session 596 review, B-21).
    // Clear the busy flag and, if audio was held while it was set, hand that
    // audio straight back to this worker. Every exit from transcribeWithTokens()
    // goes through here; that is what makes the held audio impossible to forget.
    void finishSlice();

    // Session 609 review, W-1: load the Silero model once, on this worker's thread,
    // and say in the log which speech check runs. Returns true when Silero turned
    // out to be unavailable and this is the run's first report of it.
    bool checkSileroOnce();

    std::vector<float> convertPcmSafely(const QByteArray& pcmBuffer) const;
    float calculateTranscriptionConfidence(whisper_context* ctx, int segmentCount) const;
    void applyProcessingConfig(whisper_full_params& params) const;
};
