#pragma once

#include <QString>

// Forward declaration: ProcessingConfig is defined in the adaptive-processing layer.
struct ProcessingConfig;

// D-6: Engine-agnostic recognizer contract.
//
// SpeechRecognitionManager holds a single IRecognizer* for the active engine
// and dispatches through this interface instead of switching on a recognizer
// type with qobject_cast. The on-device engine (WhisperRecognizer) implements
// it today; a future engine -- a cloud engine, for example -- plugs in the same
// way, taking audio from the product's own capture and returning text through
// the manager's single result path, with no per-type branch in the manager.
//
// This is a pure interface (not a QObject). Concrete recognizers inherit from
// QObject as well and declare their own signals; the manager connects those
// concrete signals when it constructs each engine. Keeping IRecognizer free of
// Q_OBJECT avoids the diamond-inheritance and moc constraints that a QObject
// interface would impose.
class IRecognizer
{
public:
    virtual ~IRecognizer() = default;

    // Lifecycle. start() returns true when the engine has begun (or initiated)
    // recognition; stop() returns any final transcribed text the engine was
    // still holding.
    virtual bool start() = 0;
    virtual QString stop() = 0;
    virtual bool isRunning() const = 0;

    // Audio is pushed in as 16-bit PCM frames from the audio processor.
    virtual void processAudio(const short* data, int length) = 0;

    // Adaptive-processing configuration updates (noise profile, gain, etc.).
    virtual void updateProcessingConfig(const ProcessingConfig& config) = 0;

    // Clear any retained decoding context (e.g. on document switch).
    virtual void reset() = 0;

    // Set the dictation language using a short ISO code (e.g. "en", "de").
    virtual void setLanguage(const QString& lang) = 0;

    // Raw mode disables post-processing/formatting. Engines that do not support
    // it can rely on this default no-op.
    virtual void setRawMode(bool enabled) { Q_UNUSED(enabled); }
};
