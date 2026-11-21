#include "UI/VaaniListeningPill.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cmath>

namespace {

constexpr int kHeight = 34;
constexpr int kPad = 14;
constexpr int kIndicatorWidth = 26;
constexpr int kGap = 10;
constexpr int kBottomMargin = 36;

QFont pillFont(const QFont& base)
{
    QFont font = base;
    font.setPointSizeF(9.5);
    font.setWeight(QFont::Medium);
    return font;
}

void drawListeningDot(QPainter& painter, const QRectF& box, const QColor& accent, qreal pulse,
                      float level)
{
    const qreal voice = static_cast<qreal>(qBound(0.f, level, 1.f));
    const qreal r = 4.0 + 2.5 * pulse * (0.4 + 0.6 * voice);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(box.center(), r, r);
    QColor ring = accent;
    ring.setAlphaF(static_cast<float>(0.22 + 0.28 * voice * (1.0 - pulse)));
    painter.setBrush(ring);
    painter.drawEllipse(box.center(), r + 4.0 + voice * 3.0, r + 4.0 + voice * 3.0);
}

void drawLevelBars(QPainter& painter, const QRectF& box, const QColor& accent, qreal phase,
                   int count, float level, bool voiceReactive)
{
    const qreal barW = 3.0;
    const qreal gap = 3.0;
    const qreal maxH = box.height() * 0.55;
    const qreal baseY = box.center().y() + maxH * 0.5;
    qreal x = box.left() + (box.width() - (count * barW + (count - 1) * gap)) * 0.5;
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);

    const qreal voice = static_cast<qreal>(qBound(0.f, level, 1.f));
    for (int i = 0; i < count; ++i) {
        const qreal wave = std::sin(phase + i * 0.92) * 0.5 + 0.5;
        qreal scale = 0.28 + 0.72 * wave;
        if (voiceReactive)
            scale = 0.2 + scale * (0.35 + 0.65 * voice);
        const qreal h = maxH * scale;
        painter.drawRoundedRect(QRectF(x, baseY - h, barW, h), 1.5, 1.5);
        x += barW + gap;
    }
}

} // namespace

VaaniListeningPill::VaaniListeningPill(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                         | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, &VaaniListeningPill::tickAnimation);
    hide();
}

VaaniListeningPill::~VaaniListeningPill() = default;

void VaaniListeningPill::setTrayIcon(QSystemTrayIcon* tray)
{
    m_tray = tray;
}

void VaaniListeningPill::setPhase(VaaniDictationHudPhase phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    applyPhase();
}

void VaaniListeningPill::setHandsFree(bool handsFree)
{
    if (m_handsFree == handsFree)
        return;
    m_handsFree = handsFree;
    if (m_phase != VaaniDictationHudPhase::Hidden)
        applyPhase();
}

void VaaniListeningPill::setInputLevel(float level)
{
    m_targetLevel = qBound(0.f, level, 1.f);
}

void VaaniListeningPill::applyPhase()
{
    if (m_phase == VaaniDictationHudPhase::Hidden) {
        m_animTimer.stop();
        m_targetLevel = 0.f;
        m_displayLevel = 0.f;
        hide();
        return;
    }

    const QString label = labelText();
    const int textW =
        static_cast<int>(std::ceil(QFontMetricsF(pillFont(font())).horizontalAdvance(label)));
    setFixedSize(kPad + kIndicatorWidth + kGap + textW + kPad, kHeight);

    if (!m_animTimer.isActive())
        m_animTimer.start();

    repositionBottomCenter();
    show();
    raise();
    update();
}

QString VaaniListeningPill::labelText() const
{
    if (m_phase == VaaniDictationHudPhase::Transcribing)
        return tr("Transcribing…");
    if (m_handsFree)
        return tr("Listening… press hotkey to stop");
    return tr("Listening…");
}

QColor VaaniListeningPill::accentColor(qreal pulse) const
{
    if (m_phase == VaaniDictationHudPhase::Transcribing) {
        return QColor::fromRgbF(static_cast<float>(0.92 + 0.05 * pulse), static_cast<float>(0.55 + 0.12 * pulse),
                               static_cast<float>(0.18 + 0.08 * pulse));
    }
    return QColor::fromRgbF(0.35f, static_cast<float>(0.85 + 0.1 * pulse), 0.45f);
}

void VaaniListeningPill::repositionNearTray()
{
    repositionBottomCenter();
}

void VaaniListeningPill::repositionBottomCenter()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (m_tray) {
        const QRect trayGeo = m_tray->geometry();
        if (trayGeo.isValid()) {
            const auto screens = QGuiApplication::screens();
            for (QScreen* candidate : screens) {
                if (candidate && candidate->geometry().contains(trayGeo.center())) {
                    screen = candidate;
                    break;
                }
            }
        }
    }
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    move(g.x() + (g.width() - width()) / 2, g.y() + g.height() - height() - kBottomMargin);
}

void VaaniListeningPill::tickAnimation()
{
    m_phaseAnim += 0.14;
    constexpr float smoothing = 0.28f;
    m_displayLevel += (m_targetLevel - m_displayLevel) * smoothing;
    if (m_phase == VaaniDictationHudPhase::Hidden)
        m_displayLevel *= 0.85f;
    update();
}

void VaaniListeningPill::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_phase == VaaniDictationHudPhase::Hidden)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal pulse = 0.5 + 0.5 * std::sin(m_phaseAnim);
    const QColor accent = accentColor(pulse);
    const QRectF pill(0.5, 0.5, width() - 1.0, height() - 1.0);

    painter.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
    painter.setBrush(QColor(22, 24, 30, 235));
    painter.drawRoundedRect(pill, pill.height() * 0.5, pill.height() * 0.5);

    const QRectF indicator(pill.left() + kPad - 5, pill.top(), kIndicatorWidth, pill.height());
    if (m_phase == VaaniDictationHudPhase::Transcribing)
        drawLevelBars(painter, indicator, accent, m_phaseAnim, 4, m_displayLevel, false);
    else
        drawLevelBars(painter, indicator, accent, m_phaseAnim * 1.15, 3, m_displayLevel, true);

    painter.setFont(pillFont(font()));
    painter.setPen(QColor(235, 238, 245));
    const qreal textLeft = pill.left() + kPad + kIndicatorWidth + kGap;
    const QRectF textRect(textLeft, pill.top(), pill.right() - textLeft - kPad, pill.height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, labelText());
}

void VaaniListeningPill::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    repositionBottomCenter();
}