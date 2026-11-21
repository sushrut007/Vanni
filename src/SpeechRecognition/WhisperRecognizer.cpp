#include "SpeechRecognition/WhisperRecognizer.h"
#include "Utils/LanguageUtils.h"
#include "Utils/VaaniLogging.h"
#include "SpeechRecognition/WhisperWorker.h"
// FCR S472 (A1): slice boundaries are decided by the voice-activity detector
// that this product already contains, rather than by a stopwatch alone. Since
// the Session 596 review (B-3, B-7, B-21) those decisions live in
// SpeechSliceAssembler, where they are tested with synthetic audio.
#include "SpeechRecognition/SpeechSliceAssembler.h"
#include "LanguageProcessing/LanguageDetector.h"
#include <vector>
// X-2 (FCR S493): std::min and std::max are used in three places in this file
// and <algorithm> was never included. It compiled only because another header
// pulled it in indirectly -- luck, not correctness, and it breaks the day an
// unrelated include changes. Include what you use.
#include <algorithm>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <QSet>
#include <QCoreApplication>
#include <QFile>
#include "SpeechRecognition/SileroVad.h"
#ifdef Q_OS_WIN
// B-3 (FCR S493): NOMINMAX and WIN32_LEAN_AND_MEAN are now defined for the
// whole project (see CMakeLists.txt), so this translation unit no longer needs
// its own #define and none should be added back here. Without NOMINMAX the
// Windows headers define min and max as macros and the compiler expands them
// inside the std::min / std::max calls below -- which is the reported
// "C2589: illegal token on the right side of '::'".
#include <windows.h>  // GetAsyncKeyState, VK_CONTROL (N-1 Whisper fix)
#endif

extern QString cleanWhisperOutput(const QString& input);

WhisperRecognizer::WhisperRecognizer(const QString& modelPath, QObject* parent)
    : QObject(parent)
    , m_modelPath(modelPath)
    , m_languageDetector(std::make_unique<LanguageDetector>(this))
{
}

WhisperRecognizer::~WhisperRecognizer()
{
    // Session 596 review, B-45: shutdown CANCELS instead of waiting. Buffered
    // audio is dropped before anything can queue a final transcription, and a
    // transcription already running is told to stop through the speech library's
    // abort callback, which it reads between its own steps -- so the worker
    // normally stops within a fraction of a second.
    shutdown();

    m_workerThread.quit();
    // The wait still has a limit, because the product must never hang on exit.
    // The old 3-second limit then destroyed the still-running QThread member,
    // which Qt treats as fatal. Now a worker that has not stopped in time is
    // stuck inside the speech library: its thread is ended, and the model it may
    // still be using is left for the operating system to reclaim at exit rather
    // than freed under it.
    if (!m_workerThread.wait(kWorkerStopTimeoutMs)) {
        qCCritical(lcSpeech) << "Speech worker did not stop within" << kWorkerStopTimeoutMs
                             << "ms of being cancelled; ending its thread";
        m_workerThread.terminate();
        m_workerThread.wait(kWorkerStopTimeoutMs);
        m_ctx = nullptr;
        return;
    }

    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }
}

void WhisperRecognizer::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slicer.clearAudio();       // nothing left for stop() to dispatch
        m_holdAudio.reset();
        m_isRunning = false;         // stop() returns immediately
        m_expectedResultsAfterStop = 0;
    }
    if (m_worker)
        m_worker->requestAbort();    // atomic; read by the running transcription
}

bool WhisperRecognizer::initialize()
{
    // Session 394: a unique_lock, not a lock_guard, because the model-load
    // failure path below must release m_mutex before it emits. A synchronous
    // emit while m_mutex is held deadlocks if a connected slot re-enters any
    // recognizer method that also takes m_mutex. Same discipline as start(),
    // handleTranscriptionResult() and clearContext().
    std::unique_lock<std::mutex> lock(m_mutex);

    // FCR-277 fix: replace the unbounded m_workerThread.wait() with the same
    // bounded-wait-and-use-after-free-guard pattern the destructor already uses.
    // Without a timeout, a model reload that lands while the worker is mid-inference
    // blocks the calling thread (typically the UI thread) for the full transcription
    // duration, which can be several seconds and triggers a Windows "Not Responding".
    if (m_worker) {
        // Clear the pending audio buffer first so the worker has nothing new to
        // start, mirroring the destructor's approach.
        // Session 394: m_mutex is ALREADY held by this function and std::mutex is
        // non-recursive, so the nested lock_guard that used to stand here was
        // undefined behaviour - in practice a permanent self-deadlock on the first
        // re-initialisation (any model reload reaches this branch, because it only
        // runs when a worker already exists). Clearing the buffer directly is
        // correct and already mutually excluded. Do not reintroduce a lock here.
        m_slicer.clearAudio();
        m_holdAudio.reset();
        // Session 599 review: a stopped session still owed results is finished
        // here -- the worker that owes them is about to be replaced, and they
        // will never arrive.
        if (!m_isRunning && m_expectedResultsAfterStop > 0)
            postSessionFinished(m_finishingOwner);
        m_isRunning = false;
        // B-4: the worker is about to be replaced; nothing it holds will answer.
        m_sessions.dropInFlight();
        m_dispatchesInFlight = 0;
        m_expectedResultsAfterStop = 0;
        m_workerThread.quit();

        // Bounded wait with the same use-after-free guard as the destructor:
        // if the worker is still mid-inference after the timeout, do NOT free
        // the model here -- skip re-init this cycle so the worker's own
        // deleteLater cleanup can run safely. The caller handles a false return
        // from initialize() as 'Whisper temporarily unavailable'.
        if (!m_workerThread.wait(3000)) {
            qCWarning(lcSpeech) << "WhisperRecognizer: worker still running during "
                                   "model reload; deferring re-init to avoid use-after-free";
            // Do NOT set m_worker = nullptr here: the worker is still alive and
            // owns m_ctx via its reference. Release the mutex and return false;
            // the old model stays loaded and the caller can retry later.
            return false;
        }
        // Worker stopped cleanly; it is safe to free the model below.
        m_worker = nullptr;
    }

    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }

    // S-2 (Session 198): whisper_init_from_file is deprecated. The parameter-taking
    // overload accepts a defaulted whisper_context_params struct that is held as a
    // local so any future flag tweak (e.g. enabling GPU acceleration) is a one-line
    // edit at the call site.
    whisper_context_params ctxParams = whisper_context_default_params();
    // ctxParams.use_gpu = false;   // current default on Windows MSVC
    m_ctx = whisper_init_from_file_with_params(
        m_modelPath.toStdString().c_str(), ctxParams);
    if (!m_ctx) {
        qCWarning(lcSpeech) << "WhisperRecognizer: failed to load model from:" << m_modelPath;
        lock.unlock();  // Release before emitting to prevent re-entry deadlock.
        emit errorOccurred(tr("Speech recognition model could not be loaded. Please restart Dictation Pro. If the problem continues, reinstall the application."));
        return false;
    }

    // Always create worker with new context (fix for use-after-free)
    m_worker = new WhisperWorker(m_ctx);
    // Session 609 review, W-1: the Silero model, beside the speech model or in the
    // installed models folder (the speech model is loaded from a copy in the
    // application data folder, which has no Silero file beside it). The worker
    // loads it once, on its first slice.
    m_worker->setSileroModelPath(
        SileroVad::locateInstalled(m_modelPath, QCoreApplication::applicationDirPath()));
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &WhisperWorker::resultReady, this, &WhisperRecognizer::handleTranscriptionResult, Qt::QueuedConnection);
    connect(m_worker, &WhisperWorker::errorOccurred, this, &WhisperRecognizer::handleTranscriptionError, Qt::QueuedConnection);
    connect(m_worker, &WhisperWorker::tokensReady, this, &WhisperRecognizer::handleTokens, Qt::QueuedConnection);
    connect(m_worker, &WhisperWorker::confidenceReady, this, &WhisperRecognizer::handleConfidence, Qt::QueuedConnection);
    connect(m_worker, &WhisperWorker::noticeReady, this, &WhisperRecognizer::handleNotice, Qt::QueuedConnection);

    m_workerThread.start();

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setLanguage", Qt::QueuedConnection, Q_ARG(QString, m_language));
    }

    return true;
}

bool WhisperRecognizer::start()
{
    return startFor(RecognitionOwner::None);
}

bool WhisperRecognizer::startFor(RecognitionOwner owner, CtrlMode ctrlMode)
{
    // Check m_ctx under the lock, but emit outside it.
    // Emitting a signal while holding m_mutex risks deadlock if any connected slot
    // re-enters a method on this object that also acquires m_mutex.
    bool modelReady = false;
    // Session 599 review, D-3 step 4: a stopped session still owed results when
    // this one starts; its count is reset below, so it is reported finished here
    // (its results still arrive, stamped with its owner -- B-4).
    bool previousUnfinished = false;
    RecognitionOwner previousOwner = RecognitionOwner::None;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        modelReady = (m_ctx != nullptr);
        if (modelReady) {
            previousUnfinished = !m_isRunning && m_expectedResultsAfterStop > 0;
            previousOwner = m_finishingOwner;
            m_slicer.reset();
            m_holdAudio.reset();         // Session 604 review, R-2
            m_sessions.begin(owner);     // Session 596 review, B-4
            m_ctrlMode = ctrlMode;       // Session 599 review, D-3
            m_isRunning = true;
            // FCR S483 (N1): a new session owes nothing from the previous one.
            // (Session 599 review: the count of slices in flight is NOT reset --
            // the previous session's are still coming back, and a count that
            // forgot them would end this session before its own last phrase.)
            m_expectedResultsAfterStop = 0;
            m_lastResult.clear();
            m_promptTokens.clear();
            m_consecutiveEmptyResults = 0;
            m_consecutiveContextClears = 0;
        }
    }
    if (previousUnfinished)
        emit sessionFinished(previousOwner);   // at once: before this session is anyone's
    if (!modelReady) {
        // S339-F3: Replace technical jargon with a user-friendly message.
        emit errorOccurred(tr("The speech recognition engine is not ready. Please restart Dictation Pro."));
        return false;
    }
    // Bug 1 fix: reset worker state at every session start so stale
    // sentence-context and dedup history from prior sessions do not bleed in.
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "resetState", Qt::QueuedConnection);
        // FCR S472 (A1): and only here, at a real session start, is held audio
        // let go. It belongs to the session that just ended, so carrying it into
        // this one would put the previous customer's words on screen. This is
        // the one place in the product where captured speech is deliberately
        // discarded, and it is not a loss -- the customer had stopped.
        QMetaObject::invokeMethod(m_worker, "discardPendingAudio", Qt::QueuedConnection);
    }
    return true;
}

QString WhisperRecognizer::stopAfterRelease(qint64 msSinceRelease)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopLatencyLeadMs = std::max<qint64>(0, msSinceRelease);
    }
    return stop();
}

QString WhisperRecognizer::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const qint64 latencyLeadMs = m_stopLatencyLeadMs;
    m_stopLatencyLeadMs = 0;
    if (!m_isRunning)
        return QString();

    m_isRunning = false;
    m_finishingOwner = m_sessions.currentOwner();   // Session 599 review, D-3 step 4
    // FCR S483 (N1): every transcription this class actually asked for is still
    // owed its result -- including any the worker had not finished when the
    // customer pressed stop. The final dispatch below adds one more if it is
    // made. Anything beyond this count belongs to a session that has ended.
    m_expectedResultsAfterStop = m_dispatchesInFlight;
    QString finalResult;
    if (wholeUtteranceFor(m_ctrlMode)) {
        // Session 604 review, R-2: the dictation hotkey's hold, as one utterance.
        // The release is the end of speech, so its delay is measured -- from the
        // release (stopAfterRelease()), which is what the end-of-speech-to-text
        // figures and "High latency detected" read.
        const std::optional<SpeechSliceAssembler::Slice> last = m_holdAudio.takeFinal();
        if (last) {
            ++m_expectedResultsAfterStop;      // the final dispatch owes one result
            dispatchSlice(*last, /*measureLatency=*/true, /*wholeUtterance=*/true);
            m_sliceLatencyLeadMs = latencyLeadMs;
            finalResult = m_lastResult;
        }
    } else {
        // Session 596 review, B-7: the final slice carries the Ctrl state recorded
        // during its own speech; the key read now is used only when no speech was
        // found in it.
        // Session 599 review, D-3: through the session's Ctrl mode, like every block.
        // Session 604 review, R-1: a short rest that holds speech is padded and
        // sent, not dropped (SpeechSliceAssembler::takeFinal()).
        const std::optional<SpeechSliceAssembler::Slice> last =
            m_slicer.takeFinal(resolveCtrlHeld(m_ctrlMode, readCtrlHeld()));
        if (last) {
            ++m_expectedResultsAfterStop;      // the final dispatch owes one result
            // No latency measurement for the final slice: it was not cut at a pause.
            dispatchSlice(*last, /*measureLatency=*/false);
            finalResult = m_lastResult;
        }
    }
    // Session 599 review, D-3 step 4: nothing owed means the session is finished
    // now. Posted rather than emitted, because m_mutex is held here.
    if (m_expectedResultsAfterStop == 0)
        postSessionFinished(m_finishingOwner);
    return finalResult;
}

void WhisperRecognizer::cancel()
{
    RecognitionOwner owner = RecognitionOwner::None;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isRunning) {
            // Cancelled after Stop (the keys released, the last phrases still
            // being transcribed): those phrases go nowhere either. The owed count
            // is unchanged, so the session still finishes when they are back.
            m_sessions.discardInFlight();
            return;
        }
        m_isRunning = false;
        // Slices already sent still come back. Their results now belong to
        // nobody (shown nowhere, even after another session starts), and the
        // session finishes when the last of them has arrived -- so whoever runs
        // it can hold its commands back until then.
        m_sessions.discardInFlight();
        m_expectedResultsAfterStop = m_dispatchesInFlight;
        m_slicer.reset();                 // the held speech is dropped, not sent
        m_holdAudio.reset();
        owner = m_sessions.currentOwner();
        m_finishingOwner = owner;
        if (m_expectedResultsAfterStop > 0)
            owner = RecognitionOwner::None;   // reported by the last result instead
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "discardPendingAudio", Qt::QueuedConnection);
    if (owner != RecognitionOwner::None)
        postSessionFinished(owner);
}

RecognitionOwner WhisperRecognizer::finishingOwner() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (!m_isRunning && m_expectedResultsAfterStop > 0) ? m_finishingOwner : RecognitionOwner::None;
}

bool WhisperRecognizer::settleOneOwedResult()
{
    if (m_isRunning || m_expectedResultsAfterStop <= 0)
        return false;
    --m_expectedResultsAfterStop;
    return m_expectedResultsAfterStop == 0;
}

void WhisperRecognizer::postSessionFinished(RecognitionOwner owner)
{
    QTimer::singleShot(0, this, [this, owner]() { emit sessionFinished(owner); });
}

void WhisperRecognizer::setLanguage(const QString& lang)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_language = lang;

    if (m_worker) {
        // Slot call marshalled through the worker's event queue so it executes
        // on m_workerThread, not on the caller's thread. This prevents a data
        // race with an in-progress transcription that may be holding m_contextMutex.
        QMetaObject::invokeMethod(m_worker, "setLanguage",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, lang));
    }

    if (m_languageDetector) {
        m_languageDetector->setLanguageOverride(lang);
    }

    m_consecutiveContextClears = 0;
    clearContext("Language changed to " + lang);

    if (m_isRunning && m_worker) {
        QMetaObject::invokeMethod(m_worker, "resetState", Qt::QueuedConnection);
    }
}

void WhisperRecognizer::updateProcessingConfig(const ProcessingConfig& config)
{
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "updateProcessingConfig", Qt::QueuedConnection,
            Q_ARG(ProcessingConfig, config));
    }
}

void WhisperRecognizer::setChunkingOverlapMs(int keepMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keepMs = std::max(100, std::min(keepMs, 1000));
    m_slicer.setOverlapMs(m_keepMs);
}

bool WhisperRecognizer::readCtrlHeld()
{
    // N-1 fix (Session 350): the physical Ctrl state, read with GetAsyncKeyState.
#ifdef Q_OS_WIN
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
#else
    return false;
#endif
}

// Called with m_mutex held.
void WhisperRecognizer::dispatchSlice(const SpeechSliceAssembler::Slice& slice, bool measureLatency,
                                       bool wholeUtterance)
{
    // Trim context tokens if too many
    while (m_promptTokens.size() > MAX_CONTEXT_TOKENS) {
        m_promptTokens.removeFirst();
    }
    // FCR S472 (A1, fix 5): the clock starts at the moment the slice leaves,
    // which -- with slices cut at silence -- is the moment the speaker stopped.
    if (measureLatency) {
        m_sliceTimer.restart();
        m_sliceLatencyLeadMs = 0;
    }
    ++m_dispatchesInFlight;   // FCR S483 (N1)
    m_sessions.noteDispatch();   // Session 596 review, B-4: whose result it will be

    QMetaObject::invokeMethod(m_worker, "transcribeWithTokens", Qt::QueuedConnection,
        Q_ARG(QByteArray, slice.pcm),
        Q_ARG(QVector<whisper_token>, m_promptTokens),
        Q_ARG(bool, slice.ctrlHeld),
        Q_ARG(bool, slice.heldBack),
        Q_ARG(bool, wholeUtterance));
}

// FCR S472 (A1) and the Session 596 review (B-3, B-7, B-21).
//
// Where slices are cut -- at a pause or on the stopwatch, never a slice of pure
// silence, held back while the worker is busy, bounded by a ceiling -- is decided
// by SpeechSliceAssembler (see its header for the three faults that used to sit
// here). This function supplies the two facts only it has: the Ctrl key for THIS
// block, and whether the worker can take a slice now. Back-pressure is unchanged:
// a slice is never posted to a worker that is busy, except at the ceiling, and
// except when a change of Ctrl state closes the held speech (B-7) -- that call
// is queued behind the current one, so it is never merged with it.
void WhisperRecognizer::processAudio(const short* data, int length)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRunning || !m_ctx || !m_worker)
        return;

    // Session 604 review (R-2, Z-4): the dictation hotkey's audio is collected and
    // decoded at release, as one utterance; a hold longer than the speech model's
    // window goes in whole parts, so nothing is lost. The speech check still runs
    // on every part in the worker, so silence held on the keys types nothing.
    if (wholeUtteranceFor(m_ctrlMode)) {
        const HoldUtteranceAssembler::FeedResult held = m_holdAudio.feed(data, length);
        if (held.speechStarted)
            emit speechDetected();
        if (held.part)
            dispatchSlice(*held.part, /*measureLatency=*/false, /*wholeUtterance=*/true);
        return;
    }

    // Session 599 review, D-3: the block's Ctrl state is the session's Ctrl mode's
    // answer -- the key itself only in the default mode.
    const SpeechSliceAssembler::FeedResult fed =
        m_slicer.feed(data, length, resolveCtrlHeld(m_ctrlMode, readCtrlHeld()), m_worker->isBusy());

    // FCR S483 (C11 / E10): the silence-to-speech transition opens the latency
    // measurement for the phrase. The connection to the manager is queued, so the
    // emit costs a posted event on this (audio) thread.
    if (fed.speechStarted)
        emit speechDetected();

    for (const SpeechSliceAssembler::Slice& slice : fed.slices)
        dispatchSlice(slice, /*measureLatency=*/true);
}

qint64 WhisperRecognizer::droppedAudioBytes() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_worker ? m_worker->droppedAudioBytes() : 0;
}

bool WhisperRecognizer::isRunning() const
{
    return m_isRunning;
}

void WhisperRecognizer::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Session 599 review: the owed count is cleared below, so a stopped session
    // still waiting for it is finished now rather than never.
    if (!m_isRunning && m_expectedResultsAfterStop > 0)
        postSessionFinished(m_finishingOwner);
    m_slicer.reset();
    m_holdAudio.reset();
    m_expectedResultsAfterStop = 0;       // FCR S483 (N1)
    m_dispatchesInFlight = 0;
    m_lastResult.clear();
    m_promptTokens.clear();
    m_consecutiveEmptyResults = 0;
    m_consecutiveContextClears = 0;
}

void WhisperRecognizer::clearContext(const QString& reason, bool fromEmptyResults)
{
    m_promptTokens.clear();
    // Bug 1 fix: do NOT clear the speech buffer while recording is active.
    // clearContext() is called from setLanguage() which can fire mid-utterance;
    // discarding the buffer drops audio the user is currently speaking.
    if (!m_isRunning) {
        m_slicer.clearAudio();
        m_holdAudio.reset();
    }
    m_lastResult.clear();
    m_consecutiveEmptyResults = 0;

    // S163-B1: defer contextCleared emit until after the caller releases m_mutex.
    // setLanguage() and handleTranscriptionResult() both call clearContext() while
    // holding m_mutex; a synchronous emit here would deadlock if any future slot
    // re-enters a recognizer method that also takes m_mutex. Matches the deferred
    // shouldStopDictation pattern below.
    QTimer::singleShot(0, this, [this, reason]() {
        emit contextCleared(reason);
    });

    if (fromEmptyResults) {
        m_consecutiveContextClears++;
        qCDebug(lcSpeech) << "Context cleared consecutively:" << m_consecutiveContextClears << "times. Reason:" << reason;
        if (m_consecutiveContextClears >= m_contextClearThreshold) {
            // S377 audit: wrapped in tr() so the string is translatable if
            // a future slot displays it to the user.
            QString stopReason = tr("Dictation stopped after %1 consecutive context clears due to poor recognition")
                .arg(m_consecutiveContextClears);

            m_consecutiveContextClears = 0;
            QTimer::singleShot(0, this, [this, stopReason]() {
                emit shouldStopDictation(stopReason);
                });
        }
    }
}

LanguageDetector* WhisperRecognizer::getLanguageDetector() const
{
    return m_languageDetector.get();
}

void WhisperRecognizer::handleTranscriptionResult(const QString& text, bool isFinal)
{
    // S164-A1: emit resultReady outside m_mutex to remove a live emit-under-lock
    // deadlock hazard. resultReady connects to SpeechRecognitionManager on the same
    // thread (Qt::AutoConnection -> Qt::DirectConnection), then propagates to
    // SpeechRecognitionManager::recognitionResult, then to slots including
    // VocabularyTrainingControlsPanel::onRecognitionResult. The vocab-panel slot can
    // open a modal CustomContentDialog::exec() (ShowErrorDialog) which spins the
    // event loop; if a queued m_srm->stop() (posted earlier in the same slot via
    // requestAsyncStopRecognition) fires during the modal, it calls
    // WhisperRecognizer::stop() which acquires m_mutex on the same thread that
    // already holds it. std::mutex is non-recursive -> undefined behavior (deadlock).
    // The discipline here mirrors start() (lines 108-130) and clearContext()'s
    // S163-B1 deferred emit: compute under lock, release, emit outside.
    QObject* workerToReset = nullptr;
    // FCR S472 (A1, fix 5): read under the lock, emit outside it, like every
    // other value this function hands out. -1 means no slice was in flight --
    // the final dispatch from stop(), for instance -- and is not reported.
    qint64 sliceLatency = -1;
    // FCR S483 (N1) — results that arrive after the customer stopped dictating.
    //
    // This handler was the one result path with no check that the session it
    // belongs to is still wanted; the Windows online engine (removed in FCR
    // S573) already checked its session generation, and this engine had no
    // equivalent at all.
    //
    // It is not hypothetical. Sushrut's log for 2026-08-25 shows stop() at
    // 15:48:02.887 and then two more results, at 15:48:03.421 and 15:48:04.687,
    // whose text -- "windows pcp" and "I should have bought properly." -- had
    // already been inserted as one sentence at 15:47:57.207. The customer
    // pressed stop and watched half of what they had said appear a second time,
    // in pieces, in their document.
    //
    // The final dispatch from stop() is deliberate and must still be delivered,
    // so this is a count rather than a flag: the one result stop() asked for is
    // let through, and everything after it is dropped with a line in the log.
    bool discardAfterStop = false;
    // Session 596 review, B-4: the owner of the session this result belongs to.
    // Taken first, on every path, so the record stays in step with the results.
    RecognitionOwner owner = RecognitionOwner::None;
    // Session 599 review, D-3 step 4: this was the last result a stopped session
    // was owed. Emitted after the result itself, outside the lock.
    bool sessionFinishedNow = false;
    // Session 599 review, D-3: whether the result is handed on. The sentinel (a
    // command already run) and a result after the session ended are not, but the
    // session can still finish on them.
    bool deliver = true;
    RecognitionOwner finishedOwner = RecognitionOwner::None;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        owner = m_sessions.takeResultOwner();
        // FCR S483 (N1): decided under the lock with everything else, acted on
        // outside it, like every other value this function hands out.
        if (m_dispatchesInFlight > 0)
            --m_dispatchesInFlight;
        if (!m_isRunning) {
            if (m_expectedResultsAfterStop > 0) {
                // a transcription this class asked for
                sessionFinishedNow = settleOneOwedResult();
                finishedOwner = m_finishingOwner;
            } else {
                discardAfterStop = true;
            }
        }
        if (m_sliceTimer.isValid()) {
            sliceLatency = m_sliceTimer.elapsed() + m_sliceLatencyLeadMs;
            m_sliceTimer.invalidate();   // one measurement per slice, never two
            m_sliceLatencyLeadMs = 0;
        }
        if (!discardAfterStop)
            m_lastResult = text;

        if (discardAfterStop) {
            // Deliberately does NOT touch the empty-result counters or the
            // context: this text belongs to a session that has ended, so it is
            // evidence about nothing. Logged rather than silently dropped,
            // because a result arriving here at all is worth seeing.
            qCInfo(lcSpeech) << "Discarding a transcription result that arrived after "
                                "dictation was stopped; length:" << text.size();
            deliver = false;
        }
        else if (text.isEmpty()) {
            m_consecutiveEmptyResults++;
            qCDebug(lcSpeech) << "Empty result received. Consecutive count:" << m_consecutiveEmptyResults;
            if (m_consecutiveEmptyResults >= m_emptyResultThreshold) {
                // S377 audit: wrapped in tr() defensively.
                QString reason = tr("Consecutive empty results threshold reached (%1)")
                    .arg(m_emptyResultThreshold);
                clearContext(reason, true);
                if (m_isRunning && m_worker) {
                    workerToReset = m_worker;
                }
            }
        }
        else {
            m_consecutiveEmptyResults = 0;
            m_consecutiveContextClears = 0;
        }
    }

    if (deliver) {
        if (sliceLatency >= 0) {
            emit sliceLatencyMs(sliceLatency);
        }

        emit resultReady(text, isFinal, owner);

        if (workerToReset) {
            QMetaObject::invokeMethod(workerToReset, "resetState", Qt::QueuedConnection);
        }
    }

    // Session 599 review, D-3 step 4: after the result, so whoever waits for the
    // session's end has already received its last phrase.
    if (sessionFinishedNow)
        emit sessionFinished(finishedOwner);
}

void WhisperRecognizer::handleTranscriptionError(const QString& message)
{
    // Session 599 review: a slice whose transcription failed produces this in
    // place of a result. It is counted like one, so the bookkeeping stays in step
    // and a stopped session still finishes (D-3 step 4) -- otherwise the floating
    // bar would wait for a result that never comes.
    bool sessionFinishedNow = false;
    RecognitionOwner finishedOwner = RecognitionOwner::None;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        (void)m_sessions.takeResultOwner();
        if (m_dispatchesInFlight > 0)
            --m_dispatchesInFlight;
        m_sliceTimer.invalidate();
        m_sliceLatencyLeadMs = 0;
        sessionFinishedNow = settleOneOwedResult();
        finishedOwner = m_finishingOwner;
    }
    emit errorOccurred(message);
    if (sessionFinishedNow)
        emit sessionFinished(finishedOwner);
}

void WhisperRecognizer::handleTokens(const QVector<whisper_token>& tokens)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_promptTokens.append(tokens);

    while (m_promptTokens.size() > MAX_CONTEXT_TOKENS) {
        m_promptTokens.removeFirst();
    }
}

void WhisperRecognizer::handleConfidence(float confidence)
{
    emit confidenceReady(confidence);
}

void WhisperRecognizer::handleNotice(const QString& message)
{
    emit noticeReady(message);
}
// S455 N-25: this call is now QUEUED rather than BLOCKING, and that is a
// deadlock fix rather than a tidiness change.
//
// The hazard: this reaches the worker by a cross-thread call. That same worker
// thread blocks on the interface thread in the OTHER direction whenever a voice
// command is recognised, because the object that executes commands lives on the
// interface thread and the worker needs its answer before it can return one
// (see LanguageProcessing/OptimizedPostProcessor.cpp). Two threads, each able
// to block waiting for the other. With a blocking call here, raw mode being
// switched from the interface thread WHILE the worker is inside a voice command
// means neither can proceed and the product stops responding entirely -- no
// message, no recovery short of ending the process.
//
// The correction document that raised this recorded the hazard as dormant, on
// the grounds that nothing calls the entry point. That is not true of the
// delivered code and the difference matters: SpeechRecognitionManager::
// setWhisperRawMode() is called from three places in
// VocabularyTraining/vocabularytrainingcontrolspanel.cpp -- starting dictation,
// stopping it, and the listening timeout -- all of which run on the interface
// thread. So the timing this needs is not hypothetical: it is a customer
// speaking a voice command at the moment vocabulary training starts or stops
// listening. The item is a live defect, not a latent one.
//
// Why queued is sufficient rather than a compromise: nothing needs the answer.
// The worker slot is deliberately a leaf atomic store and returns nothing, and
// no caller reads a result or depends on the flag having landed before the next
// statement runs. A queued call delivers the same store a few microseconds
// later and cannot block, which removes one of the two directions the deadlock
// needs.
//
// The same-thread fast path below is kept: when this is already called on the
// worker's own thread there is no cross-thread call to make, and a direct store
// is correct.
void WhisperRecognizer::setRawMode(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_worker) return;
    if (QThread::currentThread() == m_worker->thread()) {
        m_worker->setRawMode(enabled);
        return;
    }
    QMetaObject::invokeMethod(m_worker, "setRawMode", Qt::QueuedConnection, Q_ARG(bool, enabled));
}
