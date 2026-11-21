#pragma once

#include <QString>
#include <vector>

// Session 596 review, B-9: which capture device dictation opens.
//
// Settings stores the Windows endpoint id of the microphone the customer chose
// (and the level meter opens that endpoint), but capture always opened
// PortAudio's default input device -- so a customer who picked their headset saw
// the meter move for it while dictation listened to the laptop microphone. The
// stored id is now resolved to the PortAudio device with the same endpoint id.
//
// Portable on purpose: AudioProcessor lists the Windows (WASAPI) capture devices
// with their endpoint ids, and this decides; the tests drive it with lists.
namespace MicrophoneResolver {

// One PortAudio capture device and the Windows endpoint id behind it.
struct Candidate {
    int deviceIndex = -1;
    QString endpointId;
};

// The PortAudio device index to open for `storedEndpointId`, or -1 for "use the
// Windows default device": an empty id (the "Default microphone" entry) or an id
// no connected device has. Endpoint ids are compared case-insensitively, as
// Windows treats them.
int resolve(const QString& storedEndpointId, const std::vector<Candidate>& candidates);

// True when a non-empty stored id was not found -- the customer's chosen
// microphone is not connected, and they should be told the default is used.
bool storedDeviceMissing(const QString& storedEndpointId, int resolvedIndex);

} // namespace MicrophoneResolver
