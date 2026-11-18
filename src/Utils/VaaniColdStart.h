#pragma once

#include <QtGlobal>

namespace Vaani::ColdStart {

void logBegin();
void logPhase(const char* phaseName, qint64 elapsedMs);
void logEnd();

class PhaseTimer
{
public:
    PhaseTimer();
    qint64 elapsedMs() const;
    void restart();

private:
    qint64 m_started = 0;
};

} // namespace Vaani::ColdStart