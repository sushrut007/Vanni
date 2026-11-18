#include "SpeechRecognition/WhisperWorker.h"
#include "Utils/LanguageUtils.h"
#include "SpeechRecognition/DictationScript.h"
#include "AudioProcessing/AdaptiveProcessingManager.h"
#include "AudioProcessing/AdvancedNoiseReduction.h"
#include "LanguageProcessing/AntiRepetitionManager.h"
#include "SpeechRecognition/VoiceActivityDetector.h"
#include "LanguageProcessing/OptimizedPostProcessor.h"
#include <QDebug>
#include <vector>
#include <cmath>
#include <algorithm>
#include <QRegularExpression>
#include <QThread>
#include <QGuiApplication>
// FCR S472 (A1): finishSlice() hands held audio back through the event queue.
#include <QMetaObject>
#include "Utils/VaaniLogging.h"

QString cleanWhisperOutput(const QString& input, bool keepShort)
{
    if (input.isEmpty()) {
        return QString();
    }

    QString cleaned = input.simplified();
    static const QRegularExpression nonSpeechAnyBracket(
        R"((?:\(|\[|<|\{|\*)(?:\s*[^()\[\]{}<>*]{1,80}\s*)(?:\)|\]|\>|\}|\*))",
        QRegularExpression::CaseInsensitiveOption);
    cleaned.replace(nonSpeechAnyBracket, " ");

    // Also remove known placeholders like [BLANK_AUDIO] etc. (covered above, but keep for safety)
    static const QRegularExpression upperPlaceholders(
        R"(\[(?:BLANK_AUDIO|MUSIC|NOISE|SILENCE)\])",
        QRegularExpression::CaseInsensitiveOption);
    cleaned.replace(upperPlaceholders, " ");

    // Remove standalone music symbols
    static const QRegularExpression musicNotes(R"([♪♫♬♩♭♯])");
    cleaned.replace(musicNotes, " ");

    // Collapse runs of punctuation (Session 579 review, N-2). Four or more full
    // stops become one; a run of "!", "?" or "," becomes ONE of the same mark.
    // It used to turn every run into a full stop, so "Really??" lost its question.
    static const QRegularExpression excessivePunctuation(QStringLiteral("([.]){4,}|([!?,])\\2+"));
    cleaned.replace(excessivePunctuation, QStringLiteral("\\1\\2"));

    // Fix spacing around punctuation
    static const QRegularExpression spacingFix("\\s+([,.!?;:])");
    cleaned.replace(spacingFix, "\\1");

    // Normalize whitespace after removals
    cleaned = cleaned.simplified();

    // Trim a leading or trailing ellipsis -- three full stops or the single
    // ellipsis character -- removing exactly what matched (Session 579 review,
    // N-2). Removing three characters for the single "…" cut two letters off the
    // word before it: "I was thinking…" became "I was thinki".
    static const QRegularExpression leadingEllipsis(QStringLiteral("^(?:\\.{3}|\\x{2026})\\s*"));
    static const QRegularExpression trailingEllipsis(QStringLiteral("\\s*(?:\\.{3}|\\x{2026})$"));
    cleaned.remove(leadingEllipsis);
    cleaned.remove(trailingEllipsis);
    cleaned = cleaned.trimmed();
    static const QRegularExpression kAlnumRx("[\\p{L}\\p{N}]");
    const bool hasAlnum = cleaned.contains(kAlnumRx);
    if (!hasAlnum) {
        return QString();
    }
    if (!keepShort && cleaned.length() <= 2) {
        return QString();
    }

    return cleaned;
}

QString cleanWhisperOutput(const QString& input)
{
    return cleanWhisperOutput(input, false);
}

QString WhisperDecoding::cleanedSliceText(const QString& whisperText, bool rawMode, bool postProcessedPath)
{
    QString cleaned = cleanWhisperOutput(whisperText.trimmed(), /*keepShort=*/rawMode);
    if (!rawMode && postProcessedPath) {
        // Bug 1 fix: only the stop at the END of the slice, so the post-processor
        // alone decides whether the slice ends a sentence and the text never ends
        // with two stops. Not the stop of an abbreviation ("9 a.m.", "etc."):
        // that is part of the word, and removing it left "9 a.m". Static:
        // compiled once.
        static const QRegularExpression kTrailingPeriod(QStringLiteral("\\.$"));
        if (!isAbbreviationBeforePeriod(cleaned, cleaned.size() - 1))
            cleaned.replace(kTrailingPeriod, QString());
    }
    return cleaned;
}

WhisperWorker::~WhisperWorker() = default;

WhisperCalls WhisperCalls::library()
{
    WhisperCalls calls;
    calls.defaultParams = &whisper_full_default_params;
    calls.full = &whisper_full;
    calls.nSegments = &whisper_full_n_segments;
    calls.segmentText = &whisper_full_get_segment_text;
    calls.nTokens = &whisper_full_n_tokens;
    calls.tokenId = &whisper_full_get_token_id;
    calls.tokenP = &whisper_full_get_token_p;
    calls.tokenEot = &whisper_token_eot;
    calls.sileroLoads = &SileroVad::libraryLoads;
    return calls;
}

WhisperWorker::WhisperWorker(whisper_context* ctx, QObject* parent)
    : WhisperWorker(ctx, WhisperCalls::library(), parent)
{
}

WhisperWorker::WhisperWorker(whisper_context* ctx, const WhisperCalls& calls, QObject* parent)
    : QObject(parent), m_ctx(ctx)
    , m_noiseReduction(std::make_unique<AdvancedNoiseReduction>(this))
    , m_antiRepetition(std::make_unique<AntiRepetitionManager>())
    , m_postProcessor(std::make_unique<OptimizedPostProcessor>())
    , m_lib(calls)
{
    // Bug 1 fix: initialize post-processor once at construction; context
    // state persists across calls within a session and is reset on resetState().
    m_postProcessor->initialize();
}

void WhisperWorker::resetState()
{
    m_busy.store(false);
    // FCR S472 (A1): held audio is deliberately NOT discarded here.
    //
    // resetState() has two callers and they mean different things. One is the
    // start of a dictation session, where dropping anything held is correct --
    // and that caller now says so explicitly, by invoking discardPendingAudio()
    // as well. The other fires MID-SESSION, when the recogniser has seen several
    // empty results in a row and clears its context to recover.
    //
    // On that second path the customer is still speaking, and audio waiting in
    // the pending slot has not been transcribed yet: it is not the audio that
    // produced the empty results, and throwing it away here would be the exact
    // behaviour this item exists to remove, reintroduced through a recovery
    // path. Never discard captured speech means never, including here.
    m_antiRepetition->reset();
    m_noiseReduction->resetNoiseProfile();
    // Bug 1 fix: reset sentence-context state at session boundary so stale
    // context from a prior session does not bleed into the new one.
    if (m_postProcessor) {
        m_postProcessor->resetBuffer();
    }
    m_lastEmittedText.clear();
    m_scriptNoticeShown = false;   // SH-2: one notice per session
}

void WhisperWorker::setLanguage(const QString& language)
{
    std::lock_guard<std::mutex> lock(m_contextMutex);

    // Bugs 5-7 Fix (Session 44): Reset recognition state when language changes
    // This clears language-specific cached patterns and ensures clean slate
    if (m_language != language) {
        m_language = language;

        // Reset all language-dependent state:
        // - Anti-repetition manager (patterns differ by language)
        // - Noise reduction profile (phonetics differ by language)
        // - Any cached recognition state
        m_busy.store(false);
        // FCR S472 (A1): held audio is in the language that has just been
        // switched away from. Transcribing it under the new language would put
        // the wrong words on screen, so it goes with the rest of the state.
        discardPendingAudio();
        m_antiRepetition->reset();
        m_noiseReduction->resetNoiseProfile();
    }
}

void WhisperWorker::setRawMode(bool enabled)
{
    // INVARIANT (do not break): this slot must remain a leaf atomic store.
    // WhisperRecognizer::setRawMode invokes this cross-thread while holding
    // WhisperRecognizer::m_mutex. If this body ever locks another mutex or calls
    // back into the recognizer, that locking order can deadlock. Adding work?
    // Add it on the recognizer side after the invokeMethod returns, not here.
    //
    // S455 N-10 / N-25 -- THE THREADING RULE, written where both halves of it
    // can be seen. This note addresses lock ordering, which is a real hazard but
    // a DIFFERENT one from the deadlock that nearly shipped here. That one needs
    // no locks at all:
    //
    //   The interface thread must never block waiting on a recognition worker,
    //   because a recognition worker blocks waiting on the interface thread.
    //
    // The second half is real and deliberate: when a voice command is
    // recognised, the worker calls into the object that executes commands --
    // which lives on the interface thread -- and waits, because it has to return
    // the right answer to its own caller. That is safe only for as long as the
    // interface thread never waits on the worker in return. Today it does not:
    // the two waits that used to exist on the speech path (a sleep loop and a
    // nested event loop) were both removed, and the call that reaches this slot
    // is queued rather than blocking as of S455 N-25.
    //
    // Anyone who reintroduces a wait on the interface thread -- a progress
    // dialog that pumps events while waiting for recognition to stop, for
    // example -- creates a deadlock that appears only when a voice command lands
    // in the same instant. There are now two places that would break this rule
    // if it were forgotten, which is the argument for recording it rather than
    // remembering it.
    m_rawMode.store(enabled, std::memory_order_relaxed);
}

void WhisperWorker::updateProcessingConfig(const ProcessingConfig& config)
{
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_currentConfig = config;

    // Update noise reduction settings
    m_noiseReduction->setEnabled(config.enableNoiseReduction);
    if (config.enableNoiseReduction) {
        float aggressiveness = (config.mode == ProcessingMode::Quality) ? 0.7f : 0.5f;
        m_noiseReduction->setAggressiveness(aggressiveness);
    }
}

std::vector<float> WhisperWorker::convertPcmSafely(const QByteArray& pcmBuffer) const
{
    if (pcmBuffer.isEmpty() || pcmBuffer.size() % sizeof(qint16) != 0) {
        return {};
    }

    // ALIGNMENT: QByteArray::constData() is malloc-aligned to at least 8 bytes.
    // qint16 requires 2-byte alignment, so this cast is guaranteed safe on all
    // platforms. The size() % sizeof(qint16) guard above ensures no partial sample.
    const qint16* samples = reinterpret_cast<const qint16*>(pcmBuffer.constData());
    const int sampleCount = static_cast<int>(pcmBuffer.size() / sizeof(qint16));

    std::vector<float> pcm;
    pcm.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        pcm.push_back(samples[i] / 32768.0f);
    }

    return pcm;
}

float WhisperDecoding::geometricMeanConfidence(const std::vector<int>& tokenIds,
                                               const std::vector<float>& probabilities,
                                               int eotToken)
{
    // Session 579 review (S-4): whisper_full_get_token_p() returns a probability
    // (0..1), not its logarithm. The old code averaged the probabilities and took
    // exp() of the average, so every figure lay between 1.0 and 2.72: the
    // low-accuracy advice could never appear, the post-processor always took its
    // high-confidence path and the voice-command retry never fired. This is the
    // geometric mean of the text tokens' probabilities -- exp of their average
    // log-probability, the measure the speech model's authors use.
    double totalLogProb = 0.0;
    int textTokens = 0;
    const size_t n = std::min(tokenIds.size(), probabilities.size());
    for (size_t k = 0; k < n; ++k) {
        if (tokenIds[k] >= eotToken)
            continue;   // control tokens (end of text and the like) are not speech
        totalLogProb += std::log(static_cast<double>(std::max(probabilities[k], 1.0e-6f)));
        ++textTokens;
    }
    if (textTokens == 0)
        return 0.0f;
    return static_cast<float>(std::exp(totalLogProb / textTokens));
}

float WhisperWorker::calculateTranscriptionConfidence(whisper_context* ctx, int segmentCount) const
{
    std::vector<int> ids;
    std::vector<float> probabilities;
    for (int i = 0; i < segmentCount; ++i) {
        const int tokenCount = m_lib.nTokens(ctx, i);
        for (int j = 0; j < tokenCount; ++j) {
            ids.push_back(m_lib.tokenId(ctx, i, j));
            probabilities.push_back(m_lib.tokenP(ctx, i, j));
        }
    }
    return WhisperDecoding::geometricMeanConfidence(ids, probabilities, m_lib.tokenEot(ctx));
}

void WhisperDecoding::applyPreset(whisper_full_params& params, const ProcessingConfig& preset,
                                  float noSpeechThreshold)
{
    params.beam_search.beam_size = preset.whisperBeamSize;
    params.no_speech_thold = preset.enableAdvancedVAD ? 0.4f : noSpeechThreshold;
    params.temperature = 0.0f;
    // Session 579 review (S-3): no token limit -- the speech library's own
    // default. A limit (32, 48 or 64 by preset) ended the segment and, with
    // timestamps off, the library then moved past the rest of the audio window,
    // so the end of long speech was never recognised. The library still stops at
    // its internal maximum (half the text context) and keeps its repetition
    // safeguards, so a runaway transcription cannot run longer than before.
    params.max_tokens = 0;
}

bool WhisperDecoding::sliceHoldsSpeech(const std::vector<float>& pcm)
{
    // 300 ms windows (4,800 samples at 16 kHz) every 100 ms -- the window the
    // recognizer's own tail check uses (SpeechSliceAssembler::kSilenceTailMs).
    return VoiceActivityDetector::detectSpeechInAnyWindow(pcm, 4800, 1600);
}

bool WhisperDecoding::abortRequested(void* data)
{
    const auto* flag = static_cast<const std::atomic<bool>*>(data);
    return flag && flag->load(std::memory_order_acquire);
}

void WhisperWorker::applyProcessingConfig(whisper_full_params& params) const
{
    WhisperDecoding::applyPreset(params, m_currentConfig, NO_SPEECH_THRESHOLD);
}

// FCR S472 (A1, cause 2) — held, never discarded.
//
// Merge a slice into the single pending slot. The slot is one buffer rather
// than a queue on purpose: a queue of slices is an unbounded backlog wearing a
// different name, and Whisper transcribes one contiguous stretch of audio more
// accurately than the same audio cut into pieces, so merging is both the
// cheaper and the better answer.
void WhisperWorker::holdForLater(const QByteArray& pcmBuffer,
                                 const QVector<whisper_token>& promptTokens,
                                 bool ctrlHeld, bool wholeUtterance)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);

    if (m_hasPending) {
        m_pendingPcm.append(pcmBuffer);
        m_pendingWholeUtterance = m_pendingWholeUtterance || wholeUtterance;
    } else {
        m_pendingPcm = pcmBuffer;
        m_pendingWholeUtterance = wholeUtterance;
        m_hasPending = true;
    }

    // The newest prompt tokens describe the most recently recognised words, so
    // they are the better context for whatever is transcribed next. Older ones
    // describe audio that is still sitting ahead of it in the slot.
    if (!promptTokens.isEmpty()) {
        m_pendingTokens = promptTokens;
    }
    // The physical modifier state that matters is the one at the moment the
    // audio was captured most recently.
    m_pendingCtrlHeld = ctrlHeld;

    // The ceiling. Whisper reads at most 30 seconds in one call, so audio older
    // than that could not be transcribed even if it were kept. Reaching this
    // means the model is running slower than speech on this machine for half a
    // minute without a pause, which is a fact worth counting rather than a
    // condition to hide: droppedAudioBytes() carries it to the performance
    // monitor, which reports it.
    if (m_pendingPcm.size() > kMaxPendingBytes) {
        const qint64 overflow =
            static_cast<qint64>(m_pendingPcm.size()) - kMaxPendingBytes;
        m_pendingPcm = m_pendingPcm.right(kMaxPendingBytes);
        m_droppedAudioBytes.fetch_add(overflow, std::memory_order_relaxed);
    }
}

void WhisperWorker::finishSlice()
{
    m_busy.store(false, std::memory_order_release);

    QByteArray pcm;
    QVector<whisper_token> tokens;
    bool ctrlHeld = false;
    bool wholeUtterance = false;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_hasPending) {
            return;
        }
        pcm = m_pendingPcm;
        tokens = m_pendingTokens;
        ctrlHeld = m_pendingCtrlHeld;
        wholeUtterance = m_pendingWholeUtterance;
        m_pendingPcm.clear();
        m_pendingTokens.clear();
        m_pendingWholeUtterance = false;
        m_hasPending = false;
    }

    // Queued rather than a direct call: a direct call would recurse once per
    // held slice and could run the stack out on a machine that has fallen a
    // long way behind. Going through the event queue also lets a resetState()
    // or setLanguage() that is already waiting be delivered first, which is
    // what makes the discard at those two boundaries reliable.
    QMetaObject::invokeMethod(this, "transcribeWithTokens", Qt::QueuedConnection,
                              Q_ARG(QByteArray, pcm),
                              Q_ARG(QVector<whisper_token>, tokens),
                              Q_ARG(bool, ctrlHeld),
                              Q_ARG(bool, true),    // held back: merged while busy (B-21)
                              Q_ARG(bool, wholeUtterance));
}

bool WhisperWorker::checkSileroOnce()
{
    if (m_silero.state() != SileroVad::Availability::State::Unchecked)
        return false;
    // The library keeps the model it loads for the decodes; this load only proves
    // the file is there and readable (a v6.2.0 file this library cannot read
    // shows here, not as a failed decode on every slice).
    if (m_silero.check(m_sileroModelPath, m_lib.sileroLoads)
            == SileroVad::Availability::State::Ready) {
        qCInfo(lcSpeech) << "Silero voice detection loaded from"
                         << QString::fromStdString(m_sileroModelPath);
        return false;
    }
    qCWarning(lcSpeech) << "Silero voice detection unavailable"
                        << (m_sileroModelPath.empty() ? "(model file not found)"
                                                      : "(model file could not be loaded)")
                        << "- the loudness check is used";
    return SileroVad::firstUnavailableReport();
}

void WhisperWorker::discardPendingAudio()
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPcm.clear();
    m_pendingTokens.clear();
    m_pendingCtrlHeld = false;
    m_pendingWholeUtterance = false;
    m_hasPending = false;
}

void WhisperWorker::transcribeWithTokens(const QByteArray& pcmBuffer, const QVector<whisper_token>& promptTokens,
                                         bool ctrlHeld, bool heldBack, bool wholeUtterance)
{
    // Session 596 review, B-45: the application is closing. Nothing new starts.
    if (isAbortRequested())
        return;

    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true)) {
        // FCR S472 (A1, cause 2): this used to `return`, and the audio ceased
        // to exist. It is now held and merged into the next transcription. See
        // the note on m_pendingPcm in the header for what the delivered code was
        // really doing here and why the fix belongs in both places.
        holdForLater(pcmBuffer, promptTokens, ctrlHeld, wholeUtterance);
        return;
    }

    if (!m_ctx || pcmBuffer.isEmpty()) {
        finishSlice();
        return;
    }

    // Session 311 fix: scope m_contextMutex to the inference block only.
    // std::lock_guard has no unlock() method, so the original code held the
    // mutex for the entire function including all emit calls. Any connected
    // slot that calls setLanguage(), updateProcessingConfig(), or resetState()
    // would deadlock trying to re-acquire m_contextMutex on the same thread.
    // Fix: use a nested scope so the lock_guard destructs before any emit.

    // Locals populated inside the lock, used after it releases.
    QString result;
    // S399 (FCR Pass 16, N-7): the token ids for the segments just produced.
    // Populated inside the lock and emitted after it releases, following the
    // same pattern as every other value this function hands out.
    QVector<whisper_token> segmentTokens;
    float confidence = 0.0f;
    int n_segments = 0;
    bool vadSilence = false;          // VAD decided: no speech, silence path
    bool inferFailed = false;         // whisper_full() returned non-zero
    // Session 609 review, W-1: reports made after the lock is released.
    bool reportSileroMissing = false;
    bool reportSileroFailed = false;
    bool emptyAudio = false;          // converted PCM is empty (W1 fix)
    const bool rawMode = m_rawMode.load(std::memory_order_relaxed);
    // N-1 fix (Session 350): mods is set from the ctrlHeld parameter, which was
    // captured via GetAsyncKeyState on the GUI thread at dispatch time. This replaces
    // the unreliable QGuiApplication::queryKeyboardModifiers() worker-thread read.
    auto mods = ctrlHeld ? Qt::ControlModifier : Qt::KeyboardModifiers{};

    {
        std::lock_guard<std::mutex> lock(m_contextMutex);

        // Convert audio data
        std::vector<float> pcm = convertPcmSafely(pcmBuffer);
        if (pcm.empty()) {
            emptyAudio = true;
        }

        // Inference runs only when audio converted successfully.
        if (!emptyAudio) {
        // Session 609 review, W-1: Silero decides whether the slice holds speech
        // when its model is installed -- inside the decode below, on the whole
        // slice, with its answer alone. It replaces the loudness check where that
        // check runs, except for voice commands (raw mode). Without the model the
        // loudness check below runs exactly as before.
        reportSileroMissing = checkSileroOnce();
        const bool useSilero = m_currentConfig.enableAdvancedVAD && m_silero.useFor(rawMode);
        // The loudness check's answer, kept for a slice whose Silero decode fails.
        bool loudnessHeardSpeech = true;
        // Enhanced voice activity detection -- Session 596 review, B-2: any 300 ms
        // window, the question the recognizer asked when it cut this slice, and
        // BEFORE noise reduction, so the level tested is the level it saw. The
        // 0.01 threshold is unchanged. Without any check the model invents short
        // phrases from noise; averaging the whole slice threw away quiet speech.
        if (m_currentConfig.enableAdvancedVAD) {
            const bool speech = WhisperDecoding::sliceHoldsSpeech(pcm);
            loudnessHeardSpeech = speech;
            if (!useSilero && !speech && !rawMode) {
                vadSilence = true;
                // Lock releases at block exit; emit happens below.
            }
        }

        // Apply noise reduction if enabled
        if (!vadSilence && m_currentConfig.enableNoiseReduction) {
            pcm = m_noiseReduction->processAudio(pcm);
        }

        if (!vadSilence) {
            // Session 579 review (S-6): the presets' beam size now takes effect.
            // Greedy decoding ignores beam_size, so beam search is chosen when the
            // preset asks for a beam wider than 1 -- except for a held-back slice
            // (Session 596 review, B-21): audio the recognizer kept back because
            // the engine was busy, the real sign that it is behind the speaker;
            // greedy decoding lets it catch up.
            const long long sliceSamples = static_cast<long long>(pcm.size());
            const int presetBeamSize = qMax(1, m_currentConfig.whisperBeamSize);
            // Session 604 review, Z-12: one rule for the strategy, the segment and
            // the context -- a whole utterance keeps the preset's strategy.
            const WhisperDecoding::SliceDecoding decoding = WhisperDecoding::decodingFor(
                presetBeamSize, heldBack, wholeUtterance, mods.testFlag(Qt::ControlModifier));
            whisper_full_params params = m_lib.defaultParams(
                decoding.beamSearch ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
            params.print_realtime = false;
            params.print_progress = false;
            params.print_special = false;
            // Use physical core count capped at 8. QThread::idealThreadCount() returns
            // logical cores on most platforms; halving approximates physical cores on
            // HT/SMT systems. Whisper.cpp benchmarks show diminishing returns above ~8
            // threads and slowdowns when exceeding physical core count (context-switching
            // overhead). Minimum of 1 guards against zero on unusual hardware.
            const int logicalCores = QThread::idealThreadCount();
            const int physicalCoreEstimate = qMax(1, logicalCores > 1 ? logicalCores / 2 : logicalCores);
            params.n_threads = qMin(physicalCoreEstimate, 8);
            params.translate = false;
            params.single_segment = decoding.singleSegment;
            // N-1 fix (Session 350): mods is initialized from the ctrlHeld function
            // parameter (captured via GetAsyncKeyState on the GUI thread at dispatch).
            // A command (Ctrl held) and a whole utterance are decoded without the
            // previous text as context (decodingFor()); meta.ctrlHeld is set below.
            params.no_context = decoding.noContext;
            params.no_timestamps = true;
            params.max_len = 0;
            // Session 579 review (S-5): 768 frames (15.36 s) for ordinary slices,
            // the model's full window for longer ones. A slice can hold up to 30 s
            // of audio that waited while the engine was busy, and everything after
            // 15.36 s of it used to be skipped without a word.
            params.audio_ctx = WhisperDecoding::audioContextFor(sliceSamples);
            const std::string languageStd = m_language.isEmpty() ? std::string("en") : m_language.toStdString();
            params.language = languageStd.c_str();
            params.detect_language = false;

            // Apply adaptive configuration
            applyProcessingConfig(params);
            if (rawMode) {
                params.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
                params.no_context = true;
                params.single_segment = true;
                params.max_tokens = std::max(params.max_tokens, 24);
                // 0 means the model's full window, which is already wider than
                // 1024 frames; raising it to 1024 would shorten it.
                if (params.audio_ctx != 0)
                    params.audio_ctx = std::max(params.audio_ctx, 1024);
                params.beam_search.beam_size = std::max(params.beam_search.beam_size, 5);
                params.no_speech_thold = std::min(params.no_speech_thold, 0.30f);
                params.temperature = 0.0f;
                params.temperature_inc = 0.2f;
                params.suppress_blank = false;
                params.suppress_nst = false;
            }

            // Session 596 review, B-45: a transcription running at application exit
            // returns at once instead of holding the exit up (or being destroyed).
            params.abort_callback = &WhisperDecoding::abortRequested;
            params.abort_callback_user_data = &m_abortRequested;

            // Session 624 Pass 9 review, SH-2 (Session 625): a non-Latin language is
            // decoded without the rolling prompt, which carried the previous slice's
            // wording, and in a wrong script it kept the next slice there too. The
            // three Devanagari languages get a one-sentence prompt in their own script
            // instead. Commands (raw mode) get no prompt of their own.
            const bool nonLatinLanguage = LanguageUtils::nonLatinLanguageCodes().contains(m_language.toLower());
            std::vector<whisper_token> tokens;
            std::string scriptPrompt;
            if (nonLatinLanguage) {
                params.no_context = true;
                if (!rawMode) {
                    scriptPrompt = DictationScript::initialPromptFor(m_language).toStdString();
                    if (!scriptPrompt.empty())
                        params.initial_prompt = scriptPrompt.c_str();
                }
            } else {
                tokens.assign(promptTokens.begin(), promptTokens.end());
            }
            params.prompt_tokens = tokens.empty() ? nullptr : tokens.data();
            params.prompt_n_tokens = static_cast<int>(tokens.size());

            // Session 609 review, W-1: the library runs Silero over the whole
            // slice, decodes only the speech it finds and returns no segment when
            // it finds none.
            if (useSilero)
                SileroVad::enableFor(params, m_sileroModelPath.c_str());

            int ret = m_lib.full(m_ctx, params, pcm.data(), static_cast<int>(pcm.size()));
            bool sileroDecided = useSilero;
            if (ret != 0 && useSilero) {
                // Silero failed inside the decode: it is off for the rest of the
                // run, and this slice falls back to the loudness check's answer --
                // decoded again without Silero when that check heard speech.
                m_silero.markFailed();
                reportSileroFailed = SileroVad::firstUnavailableReport();
                qCWarning(lcSpeech) << "Silero voice detection failed in a decode; "
                                       "the loudness check is used for the rest of this run";
                sileroDecided = false;
                params.vad = false;
                if (loudnessHeardSpeech)
                    ret = m_lib.full(m_ctx, params, pcm.data(), static_cast<int>(pcm.size()));
                else
                    vadSilence = true;
            }
            if (vadSilence) {
                // The loudness check decided after a failed Silero decode.
            } else if (ret != 0) {
                inferFailed = true;
                // Lock releases at block exit; emit happens below.
            } else {
                QStringList segments;
                n_segments = m_lib.nSegments(m_ctx);
                // Session 609 review, W-1: Silero heard no speech -- the silence
                // path, so counts and messages behave as the loudness check's did.
                if (SileroVad::silenceAfterDecode(sileroDecided, n_segments))
                    vadSilence = true;
                segments.reserve(n_segments);
                for (int i = 0; i < n_segments; ++i) {
                    segments.append(QString::fromStdString(m_lib.segmentText(m_ctx, i)));

                    // S399 (FCR Pass 16, N-7): collect the token ids for the
                    // segment at the same point the text is read. The receiving
                    // half of the rolling context -- the store, its size limit,
                    // its trimming, the clear on reset and language change, the
                    // queued connection and the hand-off to the engine on every
                    // chunk -- has always been complete. The worker simply never
                    // sent the tokens, so the store was empty on every chunk and
                    // the engine was primed with nothing each time. Names,
                    // spellings, terminology and sentence capitalisation were
                    // guessed again at every chunk boundary.
                    //
                    // Special and timestamp tokens (id >= the end-of-transcript
                    // token) are left out: they are markers rather than text and
                    // are not what a prompt should carry forward.
                    const int tokenCount = m_lib.nTokens(m_ctx, i);
                    for (int j = 0; j < tokenCount; ++j) {
                        const whisper_token id = m_lib.tokenId(m_ctx, i, j);
                        if (id < m_lib.tokenEot(m_ctx)) {
                            segmentTokens.append(id);
                        }
                    }
                }
                result = segments.join("");

                confidence = calculateTranscriptionConfidence(m_ctx, n_segments);

                if (rawMode && confidence < 0.35f) {
                    whisper_full_params retry = params;
                    retry.temperature = 0.4f;
                    retry.temperature_inc = 0.2f;
                    retry.beam_search.beam_size = std::max(retry.beam_search.beam_size, 6);
                    retry.max_tokens = std::max(retry.max_tokens, 32);

                    int ret2 = m_lib.full(m_ctx, retry, pcm.data(), static_cast<int>(pcm.size()));
                    if (ret2 == 0) {
                        // C4458: renamed from 'segments' to 'retrySegments' to avoid
                        // shadowing the outer 'segments' variable.
                        QStringList retrySegments;
                        n_segments = m_lib.nSegments(m_ctx);
                        retrySegments.reserve(n_segments);
                        for (int i = 0; i < n_segments; ++i) {
                            retrySegments.append(QString::fromStdString(m_lib.segmentText(m_ctx, i)));
                        }
                        result = retrySegments.join("");
                        confidence = calculateTranscriptionConfidence(m_ctx, n_segments);
                        params = retry;
                    }
                }

                // Session 624 Pass 9 review, SH-2 (Session 625): the text came back in a
                // different non-Latin script (Hindi written in Urdu script is the case
                // reported). One more decode with a wider search -- only for this slice,
                // so every other slice keeps its speed -- and the result of that decode
                // replaces the first. What is still in the wrong script is removed below.
                if (!rawMode && nonLatinLanguage && DictationScript::containsWrongScript(result, m_language)) {
                    whisper_full_params wider = params;
                    wider.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
                    wider.beam_search.beam_size = std::max(wider.beam_search.beam_size, 5);
                    wider.temperature = 0.0f;
                    wider.temperature_inc = 0.2f;
                    if (m_lib.full(m_ctx, wider, pcm.data(), static_cast<int>(pcm.size())) == 0) {
                        QStringList widerSegments;
                        QVector<whisper_token> widerTokens;
                        n_segments = m_lib.nSegments(m_ctx);
                        for (int i = 0; i < n_segments; ++i) {
                            widerSegments.append(QString::fromStdString(m_lib.segmentText(m_ctx, i)));
                            const int count = m_lib.nTokens(m_ctx, i);
                            for (int j = 0; j < count; ++j) {
                                const whisper_token id = m_lib.tokenId(m_ctx, i, j);
                                if (id < m_lib.tokenEot(m_ctx))
                                    widerTokens.append(id);
                            }
                        }
                        result = widerSegments.join("");
                        segmentTokens = widerTokens;
                        confidence = calculateTranscriptionConfidence(m_ctx, n_segments);
                    }
                }

                // Session 579 review (S-4): confidence is used for advice and
                // measurement only, never to delete what the customer said. A
                // slice under the preset's threshold used to be replaced by
                // nothing; with the figure on its correct 0..1 scale that would
                // have silently removed real, quiet speech.
            }
        }
        } // end if (!emptyAudio)
    } // m_contextMutex released here -- all emits below are lock-free

    // Session 609 review, W-1: once per run. The installer always carries the
    // model, so this means a broken installation, not noise.
    if (reportSileroMissing)
        qCWarning(lcSpeech) << "Silero VAD model missing or unreadable:" << QString::fromStdString(m_sileroModelPath);
    if (reportSileroFailed)
        qCWarning(lcSpeech) << "Silero VAD decode failed";

    // W1 fix: empty audio means nothing to transcribe. Send the same
    // no-result signal the VAD silence path uses so callers are notified.
    // Every emit stays outside the lock (same pattern as session 311 fix).
    if (emptyAudio) {
        emit resultReady(QStringLiteral(""), true);
        finishSlice();
        return;
    }

    // VAD silence: no speech detected, emit empty result.
    if (vadSilence) {
        emit resultReady("", true);
        finishSlice();
        return;
    }

    // Inference failed.
    if (inferFailed) {
        // S339-F4: Replace technical jargon with a user-friendly message.
        emit errorOccurred(tr("Speech recognition encountered an issue. Please try dictating again."));
        finishSlice();
        return;
    }

    // Clean and process the result
    // Session 579 review (S-2): full stops INSIDE the text are kept -- "$3.50",
    // "deskshare.com", "Mr. Smith" and the stop between two sentences in one
    // slice. Every one used to be deleted here, for every language. Only the stop
    // at the very end of a slice on the post-processed (Latin) path is removed.
    const bool nonLatin = LanguageUtils::nonLatinLanguageCodes().contains(m_language.toLower());
    QString cleanedResult = WhisperDecoding::cleanedSliceText(result, rawMode, /*postProcessedPath=*/!nonLatin);

    if (rawMode) {
        if (!cleanedResult.isEmpty()) {
            emit confidenceReady(confidence);
        }
        emit resultReady(cleanedResult, true);
        finishSlice();
        return;
    }
    // NF-8: Use the shared 26-language set from LanguageUtils so WhisperWorker
    // and WhisperRecognizer always agree on which scripts bypass the English
    // post-processor. Replaces the former local 16-language kNonLatinLangs set.
    if (nonLatin) {
        // For non-Latin scripts: emit raw Whisper output directly, bypassing
        // the English-only post-processor and dedup pipeline.
        // Note: cleanWhisperOutput() strips hallucination tokens (e.g. [BLANK_AUDIO])
        // but does not apply Latin-specific homophone or capitalisation logic.
        //
        // Session 623 Pass 9 review, B-9: the customer's trained vocabulary is not
        // Latin-only, and this branch used to return before the one place it is
        // applied (OptimizedPostProcessor::processFull), after which the manager is
        // told the result is already corrected. So a trained name was ignored in the
        // 26 languages that take this branch and corrected in the other six, with no
        // message. It is applied here at the same 0.7 threshold, before the text is
        // remembered for the repetition check or handed on. The Latin post-processor
        // stays bypassed, and so does the voice-command path.
        // Session 624 Pass 9 review, SH-2 (Session 625): only letters of a different
        // non-Latin script are removed (an English word, a number, punctuation, the
        // joiners Indic spelling needs and line breaks all stay). If nothing usable
        // is left the customer is told once per session, not shown nothing.
        {
            bool removedLetters = false;
            cleanedResult = DictationScript::enforce(cleanedResult, m_language, &removedLetters);
            if (removedLetters && !DictationScript::hasLetterOrNumber(cleanedResult)) {
                cleanedResult.clear();
                if (!m_scriptNoticeShown) {
                    m_scriptNoticeShown = true;
                    emit noticeReady(tr("Could not write this in the selected script - try a clearer recording or choose another language."));
                }
            }
        }
        if (!cleanedResult.isEmpty()) {
            m_antiRepetition->addSegment(cleanedResult);
            emit confidenceReady(confidence);
            m_lastEmittedText = cleanedResult;
        }
        // S399 (N-7): feed the rolling context before the text is handed on, so
        // the next chunk is primed with what this one recognised.
        if (!segmentTokens.isEmpty()) {
            emit tokensReady(segmentTokens);
        }
        emit resultReady(cleanedResult, true);
        finishSlice();
        return;
    }

    // Bug 1 fix (Session 186 FCR — isGenuinelyNew): use the extension-aware
    // dedup check instead of isRepetitive().  isGenuinelyNew() uses a stricter
    // 0.88 similarity threshold and a prefix-ratio guard that lets genuine
    // continuations pass through (e.g. "play some" → "play some music").
    // The previous isRepetitive() at 0.70 was too aggressive and dropped valid
    // short follow-up utterances.  Ctrl-held path is unchanged (manual override).
    //
    // Session 609 review, W-2: not for a whole utterance. The check compares a
    // result with the previous one in the same session; a whole utterance's parts
    // never overlap, so there is nothing for it to catch, and a customer who
    // repeats a sentence across the 28-second point would lose it.
    if (!wholeUtterance && !mods.testFlag(Qt::ControlModifier)
            && !m_antiRepetition->isGenuinelyNew(cleanedResult, m_lastEmittedText)) {
        emit resultReady(QStringLiteral(""), true);
        finishSlice();
        return;
    }
    // Session 579 review (S-1): the recognised text keeps its capitals -- names,
    // places, acronyms and German nouns. It used to be lower-cased here, and the
    // post-processor restored only a sentence's first letter and the English "I".
    // Everything after this point compares without regard to case (trained
    // vocabulary normalises its own key, the comma and question rules are
    // case-insensitive, voice commands lower-case the words themselves). The
    // capital Whisper puts on the first word of a slice that continues a
    // sentence is handled in OptimizedPostProcessor::processFull().

    QString domainTransformed = cleanedResult;
    // Bug 1 fix: m_postProcessor is a member (not function-static) so
    // sentence-context state persists across calls within a session.
    // resetBuffer() is now called only in resetState(), not on every transcription.
    m_postProcessor->setFastPathThreshold(0.92f);
    m_postProcessor->setContextCorrectionEnabled(true);
    m_postProcessor->setHomophoneDetectionEnabled(true);

    TranscriptionMetadata meta;
    meta.averageConfidence = confidence;
    meta.segmentCount = n_segments;
    meta.durationMs = 0; // unknown here
    meta.language = m_language;
    // Propagate the physical Ctrl state to processFull so voice-command
    // routing in OptimizedPostProcessor uses the same authoritative value.
    meta.ctrlHeld = mods.testFlag(Qt::ControlModifier);

    QString finalText = m_postProcessor->processFull(domainTransformed, meta);

    // Final repetition tracking on the refined text; update dedup baseline.
    if (!finalText.isEmpty()) {
        m_antiRepetition->addSegment(finalText);
        m_lastEmittedText = finalText;
        emit confidenceReady(confidence);
    }

    // S399 (N-7): the rolling context is fed from the same call that produced
    // the text, before the text is handed on. The connection is queued and the
    // store is guarded by its own mutex, so the append happens on the
    // recogniser's thread rather than this one.
    if (!segmentTokens.isEmpty()) {
        emit tokensReady(segmentTokens);
    }
    emit resultReady(finalText, true);
    finishSlice();
}

void WhisperWorker::transcribe(const QByteArray& pcmBuffer)
{
    transcribeWithTokens(pcmBuffer, QVector<whisper_token>());
}

