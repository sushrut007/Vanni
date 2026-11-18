#include "AudioProcessing/MicrophoneResolver.h"

namespace MicrophoneResolver {

int resolve(const QString& storedEndpointId, const std::vector<Candidate>& candidates)
{
    const QString wanted = storedEndpointId.trimmed();
    if (wanted.isEmpty())
        return -1;
    for (const Candidate& c : candidates) {
        if (c.deviceIndex >= 0 && c.endpointId.compare(wanted, Qt::CaseInsensitive) == 0)
            return c.deviceIndex;
    }
    return -1;
}

bool storedDeviceMissing(const QString& storedEndpointId, int resolvedIndex)
{
    return !storedEndpointId.trimmed().isEmpty() && resolvedIndex < 0;
}

} // namespace MicrophoneResolver
