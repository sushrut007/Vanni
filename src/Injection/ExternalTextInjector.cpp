#include "Injection/ExternalTextInjector.h"

#include "Utils/ElevationRule.h"
#include "Utils/UnicodeKeystrokes.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>

#include <vector>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
bool processIsElevated(HANDLE process, bool* known)
{
    *known = false;
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    *known = ok != FALSE;
    return *known && elevation.TokenIsElevated != 0;
}

bool foregroundRunsAboveThisProcess(HWND hwnd)
{
    bool selfKnown = false;
    const bool selfElevated = processIsElevated(GetCurrentProcess(), &selfKnown);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0 || processId == GetCurrentProcessId())
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    bool targetQueried = false;
    bool targetElevated = false;
    if (process) {
        targetElevated = processIsElevated(process, &targetQueried);
        CloseHandle(process);
    }
    return ElevationRule::refuseTyping(selfElevated, targetQueried, targetElevated);
}

bool focusLooksLikePasswordField(HWND foreground)
{
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(GUITHREADINFO);
    const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    if (!GetGUIThreadInfo(threadId, &gui) || !gui.hwndFocus)
        return false;
    const LONG_PTR style = GetWindowLongPtrW(gui.hwndFocus, GWL_STYLE);
    return (style & ES_PASSWORD) != 0;
}

INPUT keyEvent(WORD vk, bool keyUp, bool extended = false)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    if (extended)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return input;
}

bool sendCtrlV()
{
    std::vector<INPUT> inputs;
    inputs.reserve(4);
    inputs.push_back(keyEvent(VK_CONTROL, false));
    inputs.push_back(keyEvent('V', false));
    inputs.push_back(keyEvent('V', true));
    inputs.push_back(keyEvent(VK_CONTROL, true));
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == static_cast<UINT>(inputs.size());
}

bool sendUnicodeKeystrokes(const QString& text)
{
    const std::vector<UnicodeKeystrokes::Key> keys = UnicodeKeystrokes::plan(text);
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size());
    for (const UnicodeKeystrokes::Key& key : keys) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        if (key.virtualKey != 0) {
            input.ki.wVk = key.virtualKey;
            input.ki.dwFlags = key.keyUp ? KEYEVENTF_KEYUP : 0;
        } else {
            input.ki.wVk = 0;
            input.ki.wScan = key.unit;
            input.ki.dwFlags = KEYEVENTF_UNICODE | (key.keyUp ? KEYEVENTF_KEYUP : 0);
        }
        inputs.push_back(input);
    }
    if (inputs.empty())
        return true;
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == static_cast<UINT>(inputs.size());
}
#endif // Q_OS_WIN

QString phraseForInjection(const QString& text)
{
    QString out = text;
    if (out.isEmpty())
        return out;
    if (!out.endsWith(QLatin1Char(' ')))
        out.append(QLatin1Char(' '));
    return out;
}

} // namespace

ExternalTextInjector::ExternalTextInjector(QObject* parent)
    : QObject(parent)
    , m_restoreTimer(new QTimer(this))
{
    m_restoreTimer->setSingleShot(true);
    m_restoreTimer->setInterval(kRestoreDelayMs);
    connect(m_restoreTimer, &QTimer::timeout,
            this, &ExternalTextInjector::restoreClipboardDeferred);
}

ExternalTextInjector::~ExternalTextInjector()
{
    delete m_clipboardBackup;
    m_clipboardBackup = nullptr;
}

void ExternalTextInjector::setExternalDictationSessionActive(bool active)
{
    m_sessionActive = active;
    if (!active) {
        m_restoreTimer->stop();
        restoreClipboardDeferred();
    }
}

void ExternalTextInjector::enqueueFinalPhrase(const QString& text)
{
    VaaniTypingTarget::BlockReason reason = VaaniTypingTarget::BlockReason::None;
    if (!VaaniTypingTarget::externalDictationSessionAllowsInjection(m_sessionActive, &reason)) {
        fail(reason, QString());
        return;
    }

    if (text.trimmed().isEmpty()) {
        fail(VaaniTypingTarget::BlockReason::EmptyPhrase, QString());
        return;
    }

    m_restoreTimer->stop();
    m_pendingPhrases.enqueue(text);

    if (!m_processing) {
        m_processing = true;
        if (!ensureClipboardBackedUp()) {
            m_processing = false;
            m_pendingPhrases.clear();
            failMessage(QStringLiteral("Could not read the clipboard. Another application may be using it."));
            return;
        }
        QMetaObject::invokeMethod(this, &ExternalTextInjector::processNextPhrase, Qt::QueuedConnection);
    }
}

void ExternalTextInjector::processNextPhrase()
{
    if (m_pendingPhrases.isEmpty()) {
        m_processing = false;
        scheduleRestore();
        return;
    }

    const QString phrase = m_pendingPhrases.dequeue();
    const QString injectText = phraseForInjection(phrase);

#ifdef Q_OS_WIN
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        offerPhraseForManualCopy(phrase, VaaniTypingTarget::BlockReason::NoForegroundWindow);
        m_pendingPhrases.clear();
        m_processing = false;
        scheduleRestore();
        return;
    }

    DWORD fgPid = 0;
    GetWindowThreadProcessId(foreground, &fgPid);
    if (fgPid == GetCurrentProcessId()) {
        offerPhraseForManualCopy(phrase, VaaniTypingTarget::BlockReason::OwnProcessForeground);
        m_pendingPhrases.clear();
        m_processing = false;
        scheduleRestore();
        return;
    }

    if (foregroundRunsAboveThisProcess(foreground)) {
        offerPhraseForManualCopy(phrase, VaaniTypingTarget::BlockReason::ElevatedTarget);
        m_pendingPhrases.clear();
        m_processing = false;
        scheduleRestore();
        return;
    }

    if (focusLooksLikePasswordField(foreground)) {
        offerPhraseForManualCopy(phrase, VaaniTypingTarget::BlockReason::LikelyPasswordField);
        m_pendingPhrases.clear();
        m_processing = false;
        scheduleRestore();
        return;
    }
#endif

    bool ok = injectViaClipboard(injectText);
    if (!ok && m_keystrokeFallback) {
        qWarning() << "[Vaani] Clipboard paste failed; trying Unicode keystroke fallback";
        ok = injectViaKeystrokes(injectText);
    }

    if (!ok) {
        offerPhraseForManualCopy(phrase, VaaniTypingTarget::BlockReason::None);
        m_pendingPhrases.clear();
        m_processing = false;
        scheduleRestore();
        failMessage(QStringLiteral("Text could not be pasted into the foreground application."));
        return;
    }

    if (m_pendingPhrases.isEmpty()) {
        m_processing = false;
        scheduleRestore();
    } else {
        QTimer::singleShot(kInterPhraseDelayMs, this, &ExternalTextInjector::processNextPhrase);
    }
}

bool ExternalTextInjector::ensureClipboardBackedUp()
{
    if (m_clipboardBackup)
        return true;

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;

    const QMimeData* mime = clipboard->mimeData(QClipboard::Clipboard);
    if (!mime)
        return true;

    m_clipboardBackup = new QMimeData();
    const QStringList formats = mime->formats();
    for (const QString& format : formats) {
        m_clipboardBackup->setData(format, mime->data(format));
    }

    m_hadNonTextFormats = false;
    for (const QString& format : formats) {
        const QString lower = format.toLower();
        if (lower.contains(QStringLiteral("image")) || lower.contains(QStringLiteral("bitmap"))) {
            m_hadNonTextFormats = true;
            break;
        }
    }

    return true;
}

bool ExternalTextInjector::injectViaClipboard(const QString& phrase)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;

    clipboard->setText(phrase, QClipboard::Clipboard);

#ifdef Q_OS_WIN
    if (!sendCtrlV())
        return false;
#endif
    return true;
}

bool ExternalTextInjector::injectViaKeystrokes(const QString& phrase)
{
#ifdef Q_OS_WIN
    return sendUnicodeKeystrokes(phrase);
#else
    Q_UNUSED(phrase);
    return false;
#endif
}

void ExternalTextInjector::scheduleRestore()
{
    if (!m_clipboardBackup)
        return;
    m_restoreTimer->start();
}

void ExternalTextInjector::restoreClipboardDeferred()
{
    if (!m_clipboardBackup || m_processing || !m_pendingPhrases.isEmpty())
        return;

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setMimeData(m_clipboardBackup, QClipboard::Clipboard);
        m_clipboardBackup = nullptr;
        if (m_hadNonTextFormats)
            qInfo() << "[Vaani] Clipboard restored (had non-text formats; verify rich content if needed).";
    } else {
        delete m_clipboardBackup;
        m_clipboardBackup = nullptr;
    }
    m_hadNonTextFormats = false;
}

void ExternalTextInjector::fail(VaaniTypingTarget::BlockReason reason, const QString& detail)
{
    QString message = VaaniTypingTarget::blockReasonMessage(reason);
    if (!detail.isEmpty())
        message = detail;
    failMessage(message);
}

void ExternalTextInjector::failMessage(const QString& message)
{
    qWarning() << "[Vaani] injection failed:" << message;
    emit injectionFailed(message);
}

bool ExternalTextInjector::offerPhraseForManualCopy(const QString& phrase,
                                                    VaaniTypingTarget::BlockReason reason)
{
    const QString trimmed = phrase.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(trimmed, QClipboard::Clipboard);

    emit phraseNeedsCapture(trimmed);
    qInfo() << "[Vaani] Paste blocked (" << VaaniTypingTarget::blockReasonMessage(reason)
            << ") — copied to clipboard and showing capture panel.";
    return true;
}