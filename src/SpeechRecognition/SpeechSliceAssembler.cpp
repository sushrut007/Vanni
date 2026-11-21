#include "SpeechRecognition/SpeechSliceAssembler.h"
#include "SpeechRecognition/VoiceActivityDetector.h"

#include <algorithm>

void SpeechSliceAssembler::setOverlapMs(int keepMs) noexcept
{
    m_keepMs = std::max(100, std::min(keepMs, 1000));
}

void SpeechSliceAssembler::reset() noexcept
{
    m_buffer.clear();
    m_carryBytes = 0;
    m_heardSpeech = false;
    m_speechCtrlHeld = false;
    m_heldBack = false;
    m_lastSpeechEndBytes = 0;
}

void SpeechSliceAssembler::clearAudio() noexcept
{
    reset();
}

bool SpeechSliceAssembler::tailIsSilence(const QByteArray& buffer)
{
    const int tailBytes = bytesForMs(kSilenceTailMs);
    if (buffer.size() < tailBytes)
        return false;

    // ALIGNMENT: QByteArray::constData() is malloc-aligned to at least 8 bytes and
    // tailBytes is a multiple of 2, so the offset pointer keeps 2-byte alignment.
    const char* tailStart = buffer.constData() + (buffer.size() - tailBytes);
    const auto* samples = reinterpret_cast<const qint16*>(tailStart);
    const int sampleCount = tailBytes / kBytesPerSample;

    std::vector<float> pcm;
    pcm.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
        pcm.push_back(samples[i] / 32768.0f);

    return !VoiceActivityDetector::detectSpeech(pcm);
}

bool SpeechSliceAssembler::tailIsSilentFor(const QByteArray& buffer, int ms)
{
    const int needBytes = bytesForMs(ms);
    if (ms <= 0 || buffer.size() < needBytes)
        return false;
    const char* start = buffer.constData() + (buffer.size() - needBytes);
    const auto* samples = reinterpret_cast<const qint16*>(start);
    const int sampleCount = needBytes / kBytesPerSample;
    std::vector<float> pcm;
    pcm.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
        pcm.push_back(samples[i] / 32768.0f);
    constexpr size_t kWindowSamples = static_cast<size_t>(kSilenceTailMs) * kSampleRate / 1000;
    constexpr size_t kHopSamples = static_cast<size_t>(kSampleRate) / 10;
    return !VoiceActivityDetector::detectSpeechInAnyWindow(pcm, kWindowSamples, kHopSamples);
}

bool SpeechSliceAssembler::holdsSpeech(const QByteArray& pcm)
{
    const int sampleCount = static_cast<int>(pcm.size() / kBytesPerSample);
    if (sampleCount <= 0)
        return false;
    const auto* samples = reinterpret_cast<const qint16*>(pcm.constData());
    std::vector<float> floats;
    floats.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
        floats.push_back(samples[i] / 32768.0f);
    // 300 ms windows every 100 ms: WhisperDecoding::sliceHoldsSpeech() asks the
    // same question with the same numbers, so a final slice this check accepts is
    // not then thrown away by the worker as silence.
    constexpr size_t kWindowSamples = static_cast<size_t>(kSilenceTailMs) * kSampleRate / 1000;
    constexpr size_t kHopSamples = static_cast<size_t>(kSampleRate) / 10;
    return VoiceActivityDetector::detectSpeechInAnyWindow(floats, kWindowSamples, kHopSamples);
}

QByteArray SpeechSliceAssembler::padToFinalLength(const QByteArray& pcm)
{
    const int padTo = bytesForMs(kFinalPadMs);
    if (pcm.size() >= padTo)
        return pcm;
    QByteArray padded = pcm;
    padded.append(QByteArray(padTo - pcm.size(), '\0'));   // 16-bit zeros: silence
    return padded;
}

SpeechSliceAssembler::Slice SpeechSliceAssembler::takeBuffer(bool ctrlFallback)
{
    Slice slice;
    slice.pcm = m_buffer;
    slice.ctrlHeld = m_heardSpeech ? m_speechCtrlHeld : ctrlFallback;
    slice.heldBack = m_heldBack;
    m_heardSpeech = false;
    m_speechCtrlHeld = false;
    m_heldBack = false;
    return slice;
}

SpeechSliceAssembler::FeedResult SpeechSliceAssembler::feed(const short* samples, int sampleCount,
                                                            bool ctrlNow, bool workerBusy)
{
    FeedResult result;
    if (samples && sampleCount > 0)
        m_buffer.append(reinterpret_cast<const char*>(samples), sampleCount * kBytesPerSample);

    const int keepBytes     = bytesForMs(m_keepMs);
    const int minSliceBytes = bytesForMs(kMinSliceMs);
    const int maxHeldBytes  = bytesForMs(kMaxHeldMs);
    const int tailBytes     = bytesForMs(kSilenceTailMs);

    // B-3: examine EVERY block, as soon as the buffer holds one tail's worth of
    // audio. The one-second minimum below still governs when a slice may be cut;
    // it no longer governs when the audio is looked at. Each 64 ms block now falls
    // inside several checked windows before any trim can reach it, so no audio is
    // discarded without being examined. Cost: one 300 ms RMS per block.
    bool tailQuiet = false;
    bool speechNow = false;
    if (m_buffer.size() >= tailBytes) {
        tailQuiet = tailIsSilence(m_buffer);
        speechNow = !tailQuiet;
    }

    // B-7: Ctrl is judged on the speech in THIS block, not on the 300 ms tail --
    // the tail still holds the end of the previous words, so a Ctrl released as a
    // command ends would otherwise cut the command's last syllables off as dictation.
    const int blockBytes = (samples && sampleCount > 0) ? sampleCount * kBytesPerSample : 0;
    bool blockSpeech = false;
    if (blockBytes > 0) {
        std::vector<float> block;
        block.reserve(static_cast<size_t>(sampleCount));
        for (int i = 0; i < sampleCount; ++i)
            block.push_back(samples[i] / 32768.0f);
        blockSpeech = VoiceActivityDetector::detectSpeech(block);
    }

    // Speech under a different Ctrl state from the speech already held closes the
    // held speech as a slice of its own -- even while the worker is busy (the call
    // is queued behind the current one, so nothing is merged). The cut falls at the
    // end of the last speech spoken under the old state; the quiet gap after it
    // goes with the new speech, so the new speech's onset is not cut off.
    if (blockSpeech && m_heardSpeech && ctrlNow != m_speechCtrlHeld) {
        qsizetype splitAt = m_lastSpeechEndBytes;
        if (splitAt <= 0 || splitAt > m_buffer.size() - blockBytes)
            splitAt = m_buffer.size() - blockBytes;
        if (splitAt > 0) {
            Slice held;
            held.pcm = m_buffer.left(splitAt);
            held.ctrlHeld = m_speechCtrlHeld;
            held.heldBack = m_heldBack;
            result.slices.push_back(std::move(held));
            m_buffer = m_buffer.mid(splitAt);
            m_carryBytes = 0;
        }
        m_heldBack = false;
        m_heardSpeech = true;           // the new speech, under the new state
        m_speechCtrlHeld = ctrlNow;
        result.speechStarted = true;
    }
    if (blockSpeech)
        m_lastSpeechEndBytes = m_buffer.size();

    if (speechNow && !m_heardSpeech) {
        m_heardSpeech = true;
        m_speechCtrlHeld = ctrlNow;
        result.speechStarted = true;
    }

    // A buffer that is only silence is not sent and does not grow (FCR S472, A1):
    // a long pause must not post a slice of silence every second, each answered
    // with an empty result that counts towards stopping dictation.
    if (tailQuiet && !m_heardSpeech) {
        if (m_buffer.size() > keepBytes)
            m_buffer = m_buffer.right(keepBytes);
        m_carryBytes = std::min(m_carryBytes, m_buffer.size());
        m_lastSpeechEndBytes = 0;
        return result;
    }

    // Session 624 Pass 9 review, SH-1 (Session 625): a segment ends where the speaker
    // pauses -- never because a stopwatch ran out. The pause is kStreamPauseMs (about
    // 700 ms, so a breath is not taken for the end of a phrase); speech spoken with
    // Ctrl held keeps the short kSilenceTailMs so a spoken command still runs at
    // once. A pause ends a slice only once the slice is at least a second long.
    const bool pausedNow = tailQuiet && m_heardSpeech && m_buffer.size() >= minSliceBytes
        && (m_speechCtrlHeld || tailIsSilentFor(m_buffer, kStreamPauseMs));
    // With no pause at all, the ceiling cuts (kMaxHeldMs, 28 s: still one model window).
    const bool atCeiling = m_buffer.size() >= maxHeldBytes;
    if (!pausedNow && !atCeiling)
        return result;

    // Back-pressure (FCR S472, A1): while the worker is busy the audio is held and
    // the next slice carries it. B-21: and the slice remembers that it was held,
    // because that -- not its length -- is the sign that the engine is behind.
    if (workerBusy && !atCeiling) {
        m_heldBack = true;
        return result;
    }

    Slice slice = takeBuffer(ctrlNow);
    // The overlap (kKeepMs) is carried only across a ceiling cut, where a word may
    // straddle the cut. After a pause nothing is carried: the speaker had stopped.
    if (!pausedNow && m_buffer.size() > keepBytes)
        m_buffer = m_buffer.right(keepBytes);
    else
        m_buffer.clear();
    m_carryBytes = m_buffer.size();   // already sent as the end of this slice
    m_lastSpeechEndBytes = 0;
    result.slices.push_back(std::move(slice));
    return result;
}

// Session 604 review, R-1. With hold-to-talk the keys are released the moment the
// speaker stops, so a sentence ending "... for the meeting. [pause] Thanks" left
// "Thanks" as a rest of under half a second, and the old rule dropped it.
std::optional<SpeechSliceAssembler::Slice> SpeechSliceAssembler::takeFinal(bool ctrlNow)
{
    std::optional<Slice> out;
    if (m_buffer.size() >= bytesForMs(kFinalPadMs)) {
        out = takeBuffer(ctrlNow);
    } else if (holdsSpeech(m_buffer.mid(std::min(m_carryBytes, m_buffer.size())))) {
        out = takeBuffer(ctrlNow);
        out->pcm = padToFinalLength(out->pcm);
    }
    reset();
    return out;
}
