#include "SpeechRecognition/HoldUtteranceAssembler.h"
#include "SpeechRecognition/VoiceActivityDetector.h"

#include <algorithm>
#include <vector>

namespace {

constexpr qsizetype bytesFor(int ms)
{
    return static_cast<qsizetype>(SpeechSliceAssembler::bytesForMs(ms));
}

// Byte offsets inside 16-bit audio stay on a sample boundary.
constexpr qsizetype onSample(qsizetype bytes)
{
    return bytes - (bytes % SpeechSliceAssembler::kBytesPerSample);
}

bool blockHoldsSpeech(const short* samples, int sampleCount)
{
    std::vector<float> block;
    block.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
        block.push_back(samples[i] / 32768.0f);
    return VoiceActivityDetector::detectSpeech(block);
}

} // namespace

void HoldUtteranceAssembler::reset() noexcept
{
    m_buffer.clear();
    m_heardSpeech = false;
    m_speechReported = false;
    m_trailingSilenceBytes = 0;
    m_cutAtBytes = 0;
    m_speechAfterCut = false;
}

HoldUtteranceAssembler::Slice HoldUtteranceAssembler::sliceOf(const QByteArray& pcm) const
{
    Slice slice;
    slice.pcm = pcm;
    slice.ctrlHeld = false;   // the dictation hotkey's session is always dictation (D-3)
    slice.heldBack = false;   // nothing was held back for a busy engine
    return slice;
}

HoldUtteranceAssembler::FeedResult HoldUtteranceAssembler::feed(const short* samples, int sampleCount)
{
    FeedResult result;
    if (!samples || sampleCount <= 0)
        return result;

    const qsizetype blockBytes = static_cast<qsizetype>(sampleCount) * SpeechSliceAssembler::kBytesPerSample;
    m_buffer.append(reinterpret_cast<const char*>(samples), blockBytes);

    const qsizetype leadInBytes = bytesFor(kLeadInMs);
    if (blockHoldsSpeech(samples, sampleCount)) {
        // The first speech: the silence before it is already no longer than the
        // lead-in (below), so it is kept as it is.
        m_heardSpeech = true;
        if (!m_speechReported) {
            m_speechReported = true;
            result.speechStarted = true;
        }
        m_trailingSilenceBytes = 0;
        if (m_cutAtBytes > 0)
            m_speechAfterCut = true;
    } else {
        m_trailingSilenceBytes += blockBytes;
        if (!m_heardSpeech) {
            // Silence held on the keys does not grow the buffer: only the lead-in
            // before the first speech is kept.
            if (m_buffer.size() > leadInBytes)
                m_buffer = m_buffer.right(leadInBytes);
            m_trailingSilenceBytes = std::min(m_trailingSilenceBytes, m_buffer.size());
        } else if (m_trailingSilenceBytes >= bytesFor(kPauseMs)) {
            // A pause after speech: the cut point is its middle, so each side keeps
            // some silence and the next word's soft start is not clipped.
            m_cutAtBytes = onSample(m_buffer.size() - m_trailingSilenceBytes / 2);
            m_speechAfterCut = false;
        }
    }

    if (!m_heardSpeech || m_buffer.size() < bytesFor(kMaxPartMs))
        return result;

    // Z-4: the part is sent whole and collecting goes on. Cut at the latest pause
    // when there is one; otherwise (speech with no pause for the whole part) at
    // the end of the buffer.
    const bool atPause = m_cutAtBytes > 0 && m_cutAtBytes < m_buffer.size();
    const qsizetype cut = atPause ? m_cutAtBytes : m_buffer.size();
    result.part = sliceOf(m_buffer.left(cut));
    m_buffer = m_buffer.mid(cut);
    m_heardSpeech = atPause && m_speechAfterCut;
    m_trailingSilenceBytes = std::min(m_trailingSilenceBytes, m_buffer.size());
    m_cutAtBytes = 0;
    m_speechAfterCut = false;
    if (!m_heardSpeech && m_buffer.size() > leadInBytes)
        m_buffer = m_buffer.right(leadInBytes);
    return result;
}

std::optional<HoldUtteranceAssembler::Slice> HoldUtteranceAssembler::takeFinal()
{
    std::optional<Slice> out;
    // R-1: the rest is sent whenever it holds speech, padded when it is short.
    if (m_heardSpeech || SpeechSliceAssembler::holdsSpeech(m_buffer))
        out = sliceOf(SpeechSliceAssembler::padToFinalLength(m_buffer));
    reset();
    return out;
}
