#pragma once

#include "AudioProcessing/AdaptiveProcessingManager.h"

// Vaani external-dictation latency profile.
namespace Vaani::SpeechTuning {

// Vaani defaults to Fast processing until a settings control exposes other modes.
constexpr ProcessingMode kDefaultProcessingMode = ProcessingMode::Fast;

// PortAudio frames-per-buffer stays 1024 in AudioProcessor (same as Dictation Pro).
// Partial text debounce before clipboard injection when enabled in settings.
constexpr int kPartialInjectDebounceMs = 400;

} // namespace Vaani::SpeechTuning