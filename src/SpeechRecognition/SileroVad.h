#pragma once

// Session 609 review, W-1: Silero voice detection, through the speech library's
// own support.
//
// The loudness check (VoiceActivityDetector) cannot tell speech from a fan or a
// radio at the same level, and misses quiet speech under its 0.01 floor. The
// speech library the product ships (whisper.cpp 1.7.6) carries Silero: given
// whisper_full_params::vad, vad_model_path and vad_params it runs Silero over the
// whole slice it is about to decode, decodes only the speech it finds, and
// returns no text when it finds none. It loads the model once and keeps it with
// the decoding state.
//
// So Silero answers the one question that needs its accuracy -- "is there speech
// in this slice?" -- once per slice, inside the decode the worker already makes.
// The slicers (SpeechSliceAssembler, HoldUtteranceAssembler) keep the loudness
// check per 64 ms block, unchanged: it decides where a slice is cut, and a slice
// with no block above the loudness floor is not sent at all -- so speech quieter
// than that floor never reaches Silero, as it never reached the decoder before.
// Silero needs more audio than a block (its own minimum speech length).
//
// What must still NOT happen, whatever changes here:
//   - Silero's "no speech" overruled by any other measure (the developer version
//     let a high-frequency energy ratio overrule it, and microphone hiss has a
//     high ratio: a quiet room counted as speech);
//   - Silero asked about one 64 ms block or a 300 ms window;
//   - a decode failing for every slice because the model could not be loaded:
//     the library returns an error for every decode then, so the model is loaded
//     once here first and Silero is turned on only when that worked;
//   - Silero used for voice commands (raw mode), where the speech check never
//     decided anything.

#include <QString>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "whisper.h"
}

namespace SileroVad {

// The model file, shipped in the installed models folder beside the speech model.
inline constexpr char kModelFileName[] = "ggml-silero-v6.2.0.bin";

// The review's values: Silero's own threshold; 100 ms of speech is enough, so
// "no" and "yes" are kept; 100 ms of padding around what it finds.
inline constexpr float kThreshold = 0.5f;
inline constexpr int kMinSpeechMs = 100;
inline constexpr int kSpeechPadMs = 100;

// Where the model is. The speech model is loaded from a copy the first start
// makes in the application data folder; that copy has no Silero file beside it.
// So: beside the speech model first (a test or a developer's folder), then the
// installed models folder the copy was made from. Empty when neither has it.
// `exists` is QFile::exists in the product; the tests pass their own.
QString locateModel(const QString& speechModelPath, const QString& installedModelsDir,
                    const std::function<bool(const QString&)>& exists);

// The product's own search: locateModel() with the "models" folder under
// `applicationDir` (the installed copy) and the files as they are on disk.
QString locateInstalled(const QString& speechModelPath, const QString& applicationDir);

// The three values above, and the library's defaults for the rest.
whisper_vad_params decodeParams();

// Silero on for one decode.
void enableFor(whisper_full_params& params, const char* modelPath);

// After a decode with Silero: no segment at all means Silero heard no speech
// (the library returns 0 and no result then), so the slice takes the silence
// path, exactly as the loudness check's "no speech" did.
constexpr bool silenceAfterDecode(bool sileroUsed, int segments) noexcept
{
    return sileroUsed && segments == 0;
}

// Whether Silero can be used, decided once per worker.
class Availability
{
public:
    enum class State { Unchecked, Ready, Unavailable };
    // True when the speech library loaded the file as a Silero model.
    using Loader = std::function<bool(const std::string& path)>;

    // Decide once: Ready when `path` is not empty and loads, Unavailable
    // otherwise. Later calls change nothing. Returns the state.
    State check(const std::string& path, const Loader& load);

    // A decode with Silero failed: off for the rest of the run.
    void markFailed() noexcept { m_state = State::Unavailable; }

    State state() const noexcept { return m_state; }

    // Silero replaces the loudness check where that check runs, except for voice
    // commands (raw mode).
    bool useFor(bool rawMode) const noexcept { return m_state == State::Ready && !rawMode; }

private:
    State m_state = State::Unchecked;
};

// One report per run, from whichever worker finds the model missing first: the
// installer always carries the file, so the report means a broken installation.
// Returns true for the first caller only.
bool firstUnavailableReport() noexcept;
// For the tests: forget that the report was made.
void resetReportForTests() noexcept;

// The library's own load: init, then free. False for a missing or unreadable file.
bool libraryLoads(const std::string& path);

// Silero's answer for the whole of `pcm` (16 kHz mono), with decodeParams(): the
// number of speech segments, or -1 when the model cannot be loaded. For the
// tests, which check the model against noise and a voice.
int speechSegmentsIn(const std::string& path, const std::vector<float>& pcm);

} // namespace SileroVad
