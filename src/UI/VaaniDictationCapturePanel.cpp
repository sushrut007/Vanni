#include "UI/VaaniDictationCapturePanel.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTextEdit>
#include <QVBoxLayout>

VaaniDictationCapturePanel::VaaniDictationCapturePanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("VaaniDictationCapture"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMinimumWidth(340);
    setMaximumWidth(520);
    setStyleSheet(QStringLiteral(
        "VaaniDictationCapture {"
        "background-color: #1a1f27;"
        "border: 1px solid #4b5563;"
        "border-radius: 10px; }"
        "QLabel#VaaniCaptureTitle { color: #f3f4f6; font-size: 13px; font-weight: 600; }"
        "QLabel#VaaniCaptureHint { color: #9ca3af; font-size: 11px; }"
        "QTextEdit {"
        "background: #0f1318; color: #f3f4f6; border: 1px solid #374151;"
        "border-radius: 6px; padding: 8px; font-size: 13px; }"
        "QPushButton {"
        "background: #2563eb; color: #fff; border: none; border-radius: 6px;"
        "padding: 6px 14px; font-weight: 600; }"
        "QPushButton#VaaniCaptureClose { background: #374151; }"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton#VaaniCaptureClose:hover { background: #4b5563; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    auto* title = new QLabel(tr("Dictation"), this);
    title->setObjectName(QStringLiteral("VaaniCaptureTitle"));

    m_hint = new QLabel(
        tr("No text field is focused — copy the text below and paste where you need it."),
        this);
    m_hint->setObjectName(QStringLiteral("VaaniCaptureHint"));
    m_hint->setWordWrap(true);

    m_text = new QTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setMinimumHeight(72);
    m_text->setMaximumHeight(160);
    m_text->setFrameShape(QFrame::NoFrame);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();

    m_copyButton = new QPushButton(tr("Copy"), this);
    connect(m_copyButton, &QPushButton::clicked, this, &VaaniDictationCapturePanel::copyToClipboard);

    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setObjectName(QStringLiteral("VaaniCaptureClose"));
    connect(m_closeButton, &QPushButton::clicked, this, &VaaniDictationCapturePanel::dismiss);

    buttons->addWidget(m_copyButton);
    buttons->addWidget(m_closeButton);

    root->addWidget(title);
    root->addWidget(m_hint);
    root->addWidget(m_text);
    root->addLayout(buttons);
    hide();
}

VaaniDictationCapturePanel::~VaaniDictationCapturePanel() = default;

void VaaniDictationCapturePanel::showPhrase(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    m_text->setPlainText(trimmed);
    reposition();
    show();
    raise();
}

void VaaniDictationCapturePanel::reposition()
{
    const QRect screen = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 800, 600);

    adjustSize();
    const int x = screen.center().x() - width() / 2;
    const int y = screen.bottom() - height() - 48;
    move(qMax(screen.left() + 12, x), qMax(screen.top() + 12, y));
}

void VaaniDictationCapturePanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    reposition();
}

void VaaniDictationCapturePanel::copyToClipboard()
{
    const QString text = m_text->toPlainText();
    if (text.isEmpty())
        return;
    if (QClipboard* clip = QGuiApplication::clipboard())
        clip->setText(text, QClipboard::Clipboard);
}

void VaaniDictationCapturePanel::dismiss()
{
    hide();
}