#include "SpeechRecognition/SileroVad.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <limits>

namespace SileroVad {

namespace {
std::atomic<bool> g_unavailableReported{ false };
} // namespace

QString locateModel(const QString& speechModelPath, const QString& installedModelsDir,
                    const std::function<bool(const QString&)>& exists)
{
    QStringList candidates;
    if (!speechModelPath.isEmpty())
        candidates << QFileInfo(speechModelPath).absoluteDir().filePath(QLatin1String(kModelFileName));
    if (!installedModelsDir.isEmpty())
        candidates << QDir(installedModelsDir).filePath(QLatin1String(kModelFileName));
    for (const QString& path : candidates) {
        if (exists(path))
            return path;
    }
    return QString();
}

QString locateInstalled(const QString& speechModelPath, const QString& applicationDir)
{
    const QString installed = applicationDir.isEmpty()
        ? QString()
        : QDir(applicationDir).filePath(QStringLiteral("models"));
    return locateModel(speechModelPath, installed,
                       [](const QString& path) { return QFile::exists(path); });
}

whisper_vad_params decodeParams()
{
    // Every field written here, so the values are visible and testable without
    // the library: the review's three, and the library's own defaults
    // (whisper_vad_default_params() in whisper.cpp 1.7.6) for the rest.
    whisper_vad_params p{};
    p.threshold = kThreshold;
    p.min_speech_duration_ms = kMinSpeechMs;
    p.min_silence_duration_ms = 100;
    p.max_speech_duration_s = std::numeric_limits<float>::max();
    p.speech_pad_ms = kSpeechPadMs;
    p.samples_overlap = 0.1f;
    return p;
}

void enableFor(whisper_full_params& params, const char* modelPath)
{
    params.vad = true;
    params.vad_model_path = modelPath;
    params.vad_params = decodeParams();
}

Availability::State Availability::check(const std::string& path, const Loader& load)
{
    if (m_state != State::Unchecked)
        return m_state;
    m_state = (!path.empty() && load(path)) ? State::Ready : State::Unavailable;
    return m_state;
}

bool firstUnavailableReport() noexcept
{
    return !g_unavailableReported.exchange(true);
}

void resetReportForTests() noexcept
{
    g_unavailableReported.store(false);
}

bool libraryLoads(const std::string& path)
{
    whisper_vad_context* ctx =
        whisper_vad_init_from_file_with_params(path.c_str(), whisper_vad_default_context_params());
    if (!ctx)
        return false;
    whisper_vad_free(ctx);
    return true;
}

int speechSegmentsIn(const std::string& path, const std::vector<float>& pcm)
{
    whisper_vad_context* ctx =
        whisper_vad_init_from_file_with_params(path.c_str(), whisper_vad_default_context_params());
    if (!ctx)
        return -1;
    int count = 0;
    whisper_vad_segments* segments = whisper_vad_segments_from_samples(
        ctx, decodeParams(), pcm.data(), static_cast<int>(pcm.size()));
    if (segments) {
        count = whisper_vad_segments_n_segments(segments);
        whisper_vad_free_segments(segments);
    } else {
        count = -1;
    }
    whisper_vad_free(ctx);
    return count;
}

} // namespace SileroVad
