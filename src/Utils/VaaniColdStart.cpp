#include "Utils/VaaniColdStart.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

namespace Vaani::ColdStart {

static QElapsedTimer& totalTimer()
{
    static QElapsedTimer t;
    return t;
}

void logBegin()
{
    totalTimer().start();
    qInfo().noquote() << QStringLiteral("[ColdStart] Vaani main begin");
}

void logPhase(const char* phaseName, qint64 elapsedMs)
{
    qInfo().noquote() << QStringLiteral("[ColdStart] phase=%1 elapsed_ms=%2")
                             .arg(QString::fromLatin1(phaseName))
                             .arg(elapsedMs);
}

void logEnd()
{
    qInfo().noquote() << QStringLiteral("[ColdStart] Vaani main ready total_elapsed_ms=%1")
                             .arg(totalTimer().elapsed());
}

PhaseTimer::PhaseTimer()
{
    restart();
}

void PhaseTimer::restart()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    m_started = clock.elapsed();
}

qint64 PhaseTimer::elapsedMs() const
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        return 0;
    return clock.elapsed() - m_started;
}

} // namespace Vaani::ColdStart