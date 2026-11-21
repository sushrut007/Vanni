#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>

class VaaniToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal offset READ offset WRITE setOffset)

public:
    explicit VaaniToggleSwitch(QWidget* parent = nullptr);
    explicit VaaniToggleSwitch(const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;

    qreal offset() const { return m_offset; }
    void setOffset(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;

private:
    void setup();
    void animate(bool checked);

    qreal m_offset = 0.0;
    QVariantAnimation* m_animation = nullptr;
};
