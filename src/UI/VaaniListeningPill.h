#pragma once

#include <QTimer>
#include <QWidget>

enum class VaaniDictationHudPhase {
    Hidden,
    Listening,
    Transcribing,
};
Q_DECLARE_METATYPE(VaaniDictationHudPhase)

// Bottom-centre status pill (Dictation Pro–style): label + animated level indicator.
class VaaniListeningPill : public QWidget
{
    Q_OBJECT

public:
    explicit VaaniListeningPill(QWidget* parent = nullptr);
    ~VaaniListeningPill() override;

    void setPhase(VaaniDictationHudPhase phase);
    void setHandsFree(bool handsFree);
    void setInputLevel(float level);

    // Kept for API compatibility; pill is screen-centred, not tray-anchored.
    void setTrayIcon(class QSystemTrayIcon* tray);
    void repositionNearTray();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void tickAnimation();

private:
    void applyPhase();
    void repositionBottomCenter();
    QString labelText() const;
    QColor accentColor(qreal pulse) const;

    QSystemTrayIcon* m_tray = nullptr;
    QTimer m_animTimer;
    VaaniDictationHudPhase m_phase = VaaniDictationHudPhase::Hidden;
    bool m_handsFree = false;
    float m_targetLevel = 0.f;
    float m_displayLevel = 0.f;
    qreal m_phaseAnim = 0.0;
};