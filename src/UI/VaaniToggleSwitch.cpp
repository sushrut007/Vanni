#include "UI/VaaniToggleSwitch.h"

#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>

VaaniToggleSwitch::VaaniToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setup();
}

VaaniToggleSwitch::VaaniToggleSwitch(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
{
    setText(text);
    setup();
}

void VaaniToggleSwitch::setup()
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(140);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        setOffset(value.toReal());
    });
}

void VaaniToggleSwitch::setOffset(qreal value)
{
    m_offset = value;
    update();
}

void VaaniToggleSwitch::animate(bool checked)
{
    m_animation->stop();
    m_animation->setStartValue(m_offset);
    m_animation->setEndValue(checked ? 1.0 : 0.0);
    m_animation->start();
}

void VaaniToggleSwitch::nextCheckState()
{
    QAbstractButton::nextCheckState();
    animate(isChecked());
}

QSize VaaniToggleSwitch::sizeHint() const
{
    const int toggleWidth = 42;
    const int toggleHeight = 22;

    if (text().isEmpty()) {
        return QSize(toggleWidth, toggleHeight);
    }

    const QFontMetrics fm(font());
    const int textWidth = fm.horizontalAdvance(text());
    const int textHeight = fm.height();
    return QSize(toggleWidth + 12 + textWidth, qMax(toggleHeight, textHeight));
}

void VaaniToggleSwitch::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int toggleW = 40;
    const int toggleH = 20;

    // Track coordinates on the right or left
    const qreal trackX = 0;
    const qreal trackY = (height() - toggleH) / 2.0;
    const QRectF trackRect(trackX, trackY, toggleW, toggleH);

    const bool on = isChecked();
    const bool dark = true; // Styled for modern dark theme with light fallback

    // Track colors
    QColor trackFill;
    QColor trackBorder;
    if (on) {
        trackFill = isDown() ? QColor(0x25, 0x63, 0xeb) : QColor(0x3b, 0x82, 0xf6);
        trackBorder = trackFill;
    } else {
        trackFill = dark ? QColor(0x22, 0x27, 0x31) : QColor(0xe5, 0xe7, 0xeb);
        trackBorder = dark ? QColor(0x5c, 0x64, 0x70) : QColor(0x9c, 0xa3, 0xaf);
        if (underMouse()) {
            trackBorder = dark ? QColor(0x8a, 0x94, 0xa6) : QColor(0x6b, 0x72, 0x80);
        }
    }

    // Draw track
    p.setPen(QPen(trackBorder, 1.5));
    p.setBrush(trackFill);
    p.drawRoundedRect(trackRect, 10.0, 10.0);

    // Thumb position & color
    const qreal thumbDiameter = 12.0;
    const qreal startX = trackRect.left() + 4.0;
    const qreal endX = trackRect.right() - thumbDiameter - 4.0;
    const qreal currentX = startX + m_offset * (endX - startX);
    const qreal thumbY = trackRect.top() + (toggleH - thumbDiameter) / 2.0;
    const QRectF thumbRect(currentX, thumbY, thumbDiameter, thumbDiameter);

    QColor thumbColor;
    if (on) {
        thumbColor = QColor(0xff, 0xff, 0xff);
    } else {
        thumbColor = dark ? QColor(0xa0, 0xaa, 0xb8) : QColor(0x4b, 0x55, 0x63);
        if (underMouse())
            thumbColor = dark ? QColor(0xda, 0xe2, 0xed) : QColor(0x11, 0x18, 0x27);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(thumbColor);
    p.drawEllipse(thumbRect);

    // Draw label text if present
    if (!text().isEmpty()) {
        const qreal textX = trackRect.right() + 10.0;
        const QRectF textRect(textX, 0, width() - textX, height());
        p.setPen(dark ? QColor(0xe8, 0xea, 0xed) : QColor(0x11, 0x18, 0x27));
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
    }
}
