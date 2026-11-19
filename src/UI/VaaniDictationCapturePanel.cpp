#include "UI/VaaniDictationCapturePanel.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

VaaniDictationCapturePanel::VaaniDictationCapturePanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("VaaniDictationCapturePanel"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(480);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("CaptureCard"));
    m_card->setStyleSheet(QStringLiteral(
        "QWidget#CaptureCard {"
        "  background-color: #161920;"
        "  border: 1px solid rgba(255, 255, 255, 0.12);"
        "  border-radius: 14px;"
        "}"
        "QLabel#CaptureBadge {"
        "  background-color: rgba(245, 158, 11, 0.14);"
        "  color: #fbbf24;"
        "  border: 1px solid rgba(245, 158, 11, 0.32);"
        "  border-radius: 10px;"
        "  padding: 3px 10px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QLabel#CaptureTitle {"
        "  color: #f3f4f6;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "  margin-top: 2px;"
        "}"
        "QLabel#CaptureHint {"
        "  color: #9ca3af;"
        "  font-size: 12px;"
        "  line-height: 1.4;"
        "}"
        "QLabel#CaptureStatus {"
        "  color: #34d399;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QTextEdit#CaptureText {"
        "  background-color: #0d1117;"
        "  color: #f0f6fc;"
        "  border: 1px solid #30363d;"
        "  border-radius: 8px;"
        "  padding: 10px 12px;"
        "  font-size: 13px;"
        "  selection-background-color: #1f6feb;"
        "  selection-color: #ffffff;"
        "}"
        "QTextEdit#CaptureText QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 6px;"
        "  margin: 4px 2px 4px 0px;"
        "}"
        "QTextEdit#CaptureText QScrollBar::handle:vertical {"
        "  background: #374151;"
        "  border-radius: 3px;"
        "  min-height: 20px;"
        "}"
        "QTextEdit#CaptureText QScrollBar::handle:vertical:hover {"
        "  background: #4b5563;"
        "}"
        "QTextEdit#CaptureText QScrollBar::add-line:vertical, QTextEdit#CaptureText QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QPushButton#HeaderCloseBtn {"
        "  background: transparent;"
        "  color: #9ca3af;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton#HeaderCloseBtn:hover {"
        "  background: rgba(255, 255, 255, 0.08);"
        "  color: #f3f4f6;"
        "}"
        "QPushButton#CloseBtn {"
        "  background: transparent;"
        "  color: #d1d5db;"
        "  border: 1px solid #374151;"
        "  border-radius: 7px;"
        "  padding: 7px 16px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#CloseBtn:hover {"
        "  background: rgba(255, 255, 255, 0.06);"
        "  border-color: #4b5563;"
        "  color: #f3f4f6;"
        "}"
        "QPushButton#CopyBtn {"
        "  background-color: #2563eb;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 7px;"
        "  padding: 7px 18px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#CopyBtn:hover {"
        "  background-color: #1d4ed8;"
        "}"
        "QPushButton#CopyBtn:pressed {"
        "  background-color: #1e40af;"
        "}"
    ));

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 6);
    m_card->setGraphicsEffect(shadow);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(10);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);

    m_badge = new QLabel(tr("⚠️ Input Not Focused"), m_card);
    m_badge->setObjectName(QStringLiteral("CaptureBadge"));

    m_headerCloseButton = new QPushButton(QStringLiteral("✕"), m_card);
    m_headerCloseButton->setObjectName(QStringLiteral("HeaderCloseBtn"));
    m_headerCloseButton->setCursor(Qt::PointingHandCursor);
    connect(m_headerCloseButton, &QPushButton::clicked, this, &VaaniDictationCapturePanel::dismiss);

    topRow->addWidget(m_badge);
    topRow->addStretch();
    topRow->addWidget(m_headerCloseButton);

    m_title = new QLabel(tr("Dictation Result"), m_card);
    m_title->setObjectName(QStringLiteral("CaptureTitle"));

    m_hint = new QLabel(
        tr("No text input field is focused. Click where you want to type, or copy the transcribed text below."),
        m_card);
    m_hint->setObjectName(QStringLiteral("CaptureHint"));
    m_hint->setWordWrap(true);

    m_text = new QTextEdit(m_card);
    m_text->setObjectName(QStringLiteral("CaptureText"));
    m_text->setReadOnly(true);
    m_text->setMinimumHeight(76);
    m_text->setMaximumHeight(200);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 4, 0, 0);
    bottomRow->setSpacing(10);

    m_statusLabel = new QLabel(m_card);
    m_statusLabel->setObjectName(QStringLiteral("CaptureStatus"));

    m_closeButton = new QPushButton(tr("Close"), m_card);
    m_closeButton->setObjectName(QStringLiteral("CloseBtn"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QPushButton::clicked, this, &VaaniDictationCapturePanel::dismiss);

    m_copyButton = new QPushButton(tr("Copy Text"), m_card);
    m_copyButton->setObjectName(QStringLiteral("CopyBtn"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    connect(m_copyButton, &QPushButton::clicked, this, &VaaniDictationCapturePanel::copyToClipboard);

    bottomRow->addWidget(m_statusLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_closeButton);
    bottomRow->addWidget(m_copyButton);

    cardLayout->addLayout(topRow);
    cardLayout->addWidget(m_title);
    cardLayout->addWidget(m_hint);
    cardLayout->addWidget(m_text);
    cardLayout->addLayout(bottomRow);

    outerLayout->addWidget(m_card);

    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, &VaaniDictationCapturePanel::resetCopyButton);

    hide();
}

VaaniDictationCapturePanel::~VaaniDictationCapturePanel() = default;

void VaaniDictationCapturePanel::showPhrase(const QString& text, const QString& reason)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    m_text->setPlainText(trimmed);

    if (!reason.isEmpty()) {
        m_hint->setText(reason);
    } else {
        m_hint->setText(
            tr("No text input field is focused. Click where you want to type, or copy the transcribed text below."));
    }

    m_statusLabel->setText(tr("✓ Copied to clipboard"));
    resetCopyButton();
    updateHeightForContent();
    reposition();
    show();
    raise();
    activateWindow();
}

void VaaniDictationCapturePanel::updateHeightForContent()
{
    const int docH = static_cast<int>(m_text->document()->size().height()) + 24;
    const int targetH = qBound(76, docH, 200);
    m_text->setFixedHeight(targetH);
    adjustSize();
}

void VaaniDictationCapturePanel::reposition()
{
    const QRect screen = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1024, 768);

    adjustSize();
    const int x = screen.center().x() - width() / 2;
    const int y = screen.bottom() - height() - 56;
    move(qMax(screen.left() + 16, x), qMax(screen.top() + 16, y));
}

void VaaniDictationCapturePanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    reposition();
}

void VaaniDictationCapturePanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        copyToClipboard();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void VaaniDictationCapturePanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void VaaniDictationCapturePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void VaaniDictationCapturePanel::copyToClipboard()
{
    const QString text = m_text->toPlainText();
    if (text.isEmpty())
        return;

    if (QClipboard* clip = QGuiApplication::clipboard())
        clip->setText(text, QClipboard::Clipboard);

    m_statusLabel->setText(tr("✓ Copied to clipboard"));
    m_copyButton->setText(tr("✓ Copied!"));
    m_copyButton->setStyleSheet(QStringLiteral(
        "QPushButton#CopyBtn {"
        "  background-color: #10b981;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 7px;"
        "  padding: 7px 18px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#CopyBtn:hover {"
        "  background-color: #059669;"
        "}"));
    m_copyFeedbackTimer->start(1800);
}

void VaaniDictationCapturePanel::resetCopyButton()
{
    m_copyButton->setText(tr("Copy Text"));
    m_copyButton->setStyleSheet(QString());
}

void VaaniDictationCapturePanel::dismiss()
{
    hide();
}
