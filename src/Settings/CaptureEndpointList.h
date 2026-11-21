#pragma once

#include <QString>
#include <QVector>

namespace Vaani {

struct CaptureEndpoint {
    QString endpointId; // empty = Windows default
    QString displayName;
};

QVector<CaptureEndpoint> listActiveCaptureEndpoints();

} // namespace Vaani