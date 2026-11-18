#pragma once

#include "Injection/TypingTargetRules.h"

#include <QObject>
#include <QQueue>
#include <QString>

class QMimeData;
class QTimer;

class ExternalTextInjector : public QObject
{
    Q_OBJECT

public:
    explicit ExternalTextInjector(QObject* parent = nullptr);
    ~ExternalTextInjector() override;

    void setExternalDictationSessionActive(bool active);
    bool externalDictationSessionActive() const { return m_sessionActive; }

    void setKeystrokeFallbackEnabled(bool enabled) { m_keystrokeFallback = enabled; }
    bool keystrokeFallbackEnabled() const { return m_keystrokeFallback; }

    void enqueueFinalPhrase(const QString& text);

signals:
    void injectionFailed(const QString& userMessage);
    void phraseNeedsCapture(const QString& text);

private slots:
    void processNextPhrase();
    void restoreClipboardDeferred();

private:
    bool ensureClipboardBackedUp();
    bool injectViaClipboard(const QString& phrase);
    bool injectViaKeystrokes(const QString& phrase);
    void scheduleRestore();
    void fail(VaaniTypingTarget::BlockReason reason, const QString& detail = QString());
    void failMessage(const QString& message);
    bool offerPhraseForManualCopy(const QString& phrase, VaaniTypingTarget::BlockReason reason);

    bool m_sessionActive = false;
    bool m_keystrokeFallback = false;
    bool m_processing = false;
    bool m_hadNonTextFormats = false;

    QQueue<QString> m_pendingPhrases;
    QMimeData* m_clipboardBackup = nullptr;
    QTimer* m_restoreTimer = nullptr;

    static constexpr int kRestoreDelayMs = 220;
    static constexpr int kInterPhraseDelayMs = 90;
};