#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <memory>
#include <QThread>
#include <QPointer>
#include <QByteArray>
#include <QVector>
#include <QElapsedTimer>
#include "SpeechRecognition/IRecognizer.h"
#include "SpeechRecognition/SpeechSliceAssembler.h"
#include "SpeechRecognition/HoldUtteranceAssembler.h"
#include "SpeechRecognition/RecognitionSessions.h"

extern "C" {
#include "whisper.h"
}

// Forward declarations
class WhisperWorker;
class LanguageDetector;
struct ProcessingConfig;

class WhisperRecognizer : public QObject, public IRecognizer
{
    Q_OBJECT

public:
    explicit WhisperRecognizer(const QString& modelPath, QObject* parent = nullptr);
    ~WhisperRecognizer();

    // Session 596 review, B-45: end everything at once, for application exit. The
    // buffered audio is dropped (no final transcription is queued), and a
    // transcription already running is aborted through the speech library's abort
    // callback. Called by SpeechRecognitionManager's destructor before stop(), and
    // by this destructor. Not a pause: the recognizer is not started again.
    void shutdown();

    bool initialize();
    // IRecognizer: a session that belongs to nobody -- its results are shown
    // nowhere. The product always starts through startFor().
    bool start() override;
    // Session 596 review, B-4: start a session for `owner`. Every result of it --
    // including results delivered after stop() -- is stamped with that owner.
    // Session 599 review, D-3: and with the session's Ctrl mode, which decides
    // whether its phrases are dictation or commands (see CtrlMode).
    bool startFor(RecognitionOwner owner, CtrlMode ctrlMode = CtrlMode::FollowKeyboard);
    QString stop() override;
    // Session 604 review, R-2: stop() for a session whose speech ended
    // `msSinceRelease` ago -- the hotkey's keys were released, and the microphone
    // was kept open a moment longer (R-3). The end-of-speech-to-text delay of the
    // last utterance then counts from the release, as the customer experiences it.
    QString stopAfterRelease(qint64 msSinceRelease);
    // Session 599 review, D-7: end the session and transcribe nothing -- the
    // buffered audio is dropped and results still in flight are discarded. Used
    // when another key cancels a hold-to-talk session (a Windows shortcut).
    void cancel();
    // Session 599 review: the owner of a stopped session whose last results are
    // still being transcribed; None when nothing is owed.
    RecognitionOwner finishingOwner() const;
    void processAudio(const short* data, int length) override;
    bool isRunning() const override;
    void setLanguage(const QString& lang) override;
    QString getLanguage() const { return m_language; }
    void updateProcessingConfig(const ProcessingConfig& config) override;
    void reset() override;
    void setRawMode(bool enabled) override;
    LanguageDetector* getLanguageDetector() const;

    // Empty result threshold
    void setEmptyResultThreshold(int threshold) { m_emptyResultThreshold = threshold; }
    int getEmptyResultThreshold() const { return m_emptyResultThreshold; }

    // Context clear threshold
    void setContextClearThreshold(int threshold) { m_contextClearThreshold = threshold; }
    int getContextClearThreshold() const { return m_contextClearThreshold; }

    // Optional: allow overriding chunking window (ms)
    void setChunkingOverlapMs(int keepMs);

    // FCR S472 (A1, fix 5): captured speech, in bytes, that this machine could
    // not keep up with. Zero on a healthy machine.
    qint64 droppedAudioBytes() const;

signals:
    // FCR S472 (A1, fix 5): time from the end of a slice of speech to the text
    // for it being handed on. With slices now cut at silence, the start of that
    // interval is the moment the speaker stopped, so this is the delay the
    // customer actually experiences -- the "long delay before anything appears"
    // in the report. The existing latency measurement times the transcription
    // call alone and cannot see waiting in the queue.
    void sliceLatencyMs(qint64 latencyMs);

    // FCR S483 (C11 / E10): the start of a phrase, raised on the transition
    // from silence to speech. It exists so the performance monitor can open one
    // measurement per phrase instead of one per session. (FCR S573: the Windows
    // online engine, which had its own equivalent, was removed.)
    void speechDetected();

    // owner (Session 596 review, B-4): who started the session that produced
    // this result.
    void resultReady(const QString& text, bool isFinal, RecognitionOwner owner);
    void errorOccurred(const QString& message);
    void confidenceReady(float confidence);
    // Session 625 (SH-2): a notice from the worker, passed on to the status bar.
    void noticeReady(const QString& message);
    void contextCleared(const QString& reason);
    void shouldStopDictation(const QString& reason);
    // Session 599 review, D-3 step 4: the session that `owner` started has ended
    // and every result it was owed has arrived -- or nothing was owed (a release
    // whose last audio holds no speech sends no final slice, so no result would
    // ever come). Emitted once per stop() or cancel(), after the last result.
    void sessionFinished(RecognitionOwner owner);

private slots:
    void handleTranscriptionResult(const QString& text, bool isFinal);
    void handleTranscriptionError(const QString& message);
    void handleTokens(const QVector<whisper_token>& tokens);
    void handleConfidence(float confidence);
    void handleNotice(const QString& message);

private:
    // Session 626 review, B-3: `reason` is for display and is translated, so it is never
    // tested. `fromEmptyResults` says the clear came from the consecutive-empty-results
    // rule, which is what counts towards the automatic stop.
    void clearContext(const QString& reason, bool fromEmptyResults = false);
    // The physical Ctrl key, read for each captured block (Session 596 review, B-7).
    static bool readCtrlHeld();
    // Post one slice to the worker. Called with m_mutex held. wholeUtterance
    // (Session 604 review, R-2): decoded as one utterance (the dictation hotkey).
    void dispatchSlice(const SpeechSliceAssembler::Slice& slice, bool measureLatency,
                       bool wholeUtterance = false);
    // Session 599 review, D-3 step 4: sessionFinished, posted to this object's
    // thread so it is never emitted with m_mutex held.
    void postSessionFinished(RecognitionOwner owner);

    QString m_modelPath;
    whisper_context* m_ctx = nullptr;
    QString m_language = "en";
    std::atomic<bool> m_isRunning{ false };
    // Session 596 review (B-3, B-7, B-21): the speech buffer and every decision
    // about where it is cut. Guarded by m_mutex like the rest of this state.
    // Session 596 review, B-45: how long the destructor waits for a cancelled
    // worker (see ~WhisperRecognizer()).
    static constexpr unsigned long kWorkerStopTimeoutMs = 10000;
    SpeechSliceAssembler m_slicer;
    // Session 604 review (R-2, Z-4): the dictation hotkey's audio, decoded as one
    // utterance at release instead of in slices (wholeUtteranceFor()). Guarded
    // by m_mutex.
    HoldUtteranceAssembler m_holdAudio;
    // Session 604 review, R-2: added to the measured delay of the slice in
    // flight -- the time between the release and stop() (stopAfterRelease()).
    qint64 m_sliceLatencyLeadMs = 0;
    qint64 m_stopLatencyLeadMs = 0;
    // Session 596 review, B-4: which session each slice in flight was sent for.
    // Guarded by m_mutex.
    RecognitionSessions m_sessions;
    // Session 599 review, D-3: the current session's Ctrl mode. Guarded by m_mutex.
    CtrlMode m_ctrlMode = CtrlMode::FollowKeyboard;
    // Session 599 review, D-3 step 4: whose session the owed results finish --
    // set by stop() and cancel(). Guarded by m_mutex.
    RecognitionOwner m_finishingOwner = RecognitionOwner::None;
    // Take one owed result off the count (called with m_mutex held, for every
    // result or failure that arrives after a stop). True when it was the last.
    bool settleOneOwedResult();
    QString m_lastResult;
    QVector<whisper_token> m_promptTokens;

    QThread m_workerThread;
    WhisperWorker* m_worker = nullptr;
    std::unique_ptr<LanguageDetector> m_languageDetector;

    int m_consecutiveEmptyResults = 0;
    int m_emptyResultThreshold = 5;

    int m_consecutiveContextClears = 0;
    int m_contextClearThreshold = 3;

    mutable std::mutex m_mutex;

    // FCR S472 (A1, fix 5): started when a slice is handed over, read when its
    // text comes back. Only one slice is in flight at a time -- that is what
    // the back-pressure in processAudio() guarantees -- so one timer is enough
    // and no queue of timestamps is needed.
    QElapsedTimer m_sliceTimer;

    // FCR S472 (A1): whether speech has been found since the last slice was sent
    // is now SpeechSliceAssembler::heardSpeech() -- see m_slicer.

    // FCR S483 (N1, found beyond the document) — how many transcriptions are
    // still in flight, and how many of their results are still owed after a stop.
    //
    // stop() deliberately dispatches whatever audio is still buffered so the
    // last words spoken are not lost, and that one result must still be
    // delivered after m_isRunning has gone false. Everything ARRIVING AFTER IT
    // must not be: it belongs to slices that were already in flight when the
    // customer pressed stop, and Sushrut's own log shows two of them landing
    // 0.5 and 1.8 seconds after stop(), carrying text that had already been
    // inserted 5 seconds earlier -- "windows pcp" and "I should have bought
    // properly.", the two halves of a sentence the customer could already see.
    //
    // What this fixes, and what it deliberately does not. The certain defect,
    // provable by reading, is that this handler had NO check at all: a result
    // from a session the customer ended was inserted into their document
    // unconditionally, with no upper bound on how late it could be. That is
    // closed here.
    //
    // The duplication in that log — the same words arriving twice — is NOT
    // explained by reading, and is not claimed as fixed. The two post-stop
    // results carried the text of a sentence recognised five seconds EARLIER,
    // not the tail of what was said last, so the cause is upstream of this
    // handler and cannot be identified without a build. See HANDOFF.md.
    //
    // The count is of dispatches, not a flag, and that direction is chosen
    // deliberately: every result belonging to a transcription this class
    // actually asked for is delivered, and only results beyond that are dropped.
    // Getting it wrong the other way would silently lose the last thing a
    // customer said, which is a worse failure than a visible duplicate and is
    // the exact failure this round exists to stop.
    int m_dispatchesInFlight = 0;
    int m_expectedResultsAfterStop = 0;

    // Default (ms) for the audio carried over a ceiling cut. Session 625, SH-1: the
    // 3 s stopwatch step (and the 5 s one for non-Latin languages, with the language
    // switch that chose it) is gone -- the slicer cuts at pauses.
    static constexpr int kKeepMs = 200;
    static constexpr int kSampleRate = 16000;
    static constexpr int MAX_CONTEXT_TOKENS = 224;

    // FCR S472 (A1): kMinSliceMs, kSilenceTailMs and kMaxHeldMs are now
    // SpeechSliceAssembler's, with the reasons for each value.
    //
    // Not changed, deliberately: kKeepMs stays at 200 ms. The correction
    // document asks for a longer rolling window of preceding AUDIO. This
    // product already carries rolling context, and carries it in the form the
    // model actually wants -- m_promptTokens feeds up to MAX_CONTEXT_TOKENS of
    // the text just recognised into the next transcription as a prompt (S399,
    // N-7). Lengthening the audio overlap on top of that re-transcribes speech
    // that has already been put on screen, and the only thing standing between
    // that and the same words appearing twice is the dedup similarity
    // threshold, which cannot be judged without a build. See HANDOFF.md.

    // Active overlap
    int m_keepMs = kKeepMs;
};
