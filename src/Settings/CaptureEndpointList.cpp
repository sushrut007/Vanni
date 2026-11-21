#include "Settings/CaptureEndpointList.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

namespace Vaani {

QVector<CaptureEndpoint> listActiveCaptureEndpoints()
{
    QVector<CaptureEndpoint> out;
    out.push_back({ QString(),
                    QCoreApplication::translate("VaaniSettings", "Default microphone") });

#ifdef Q_OS_WIN
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator)
        return out;

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr) || !collection)
        return out;

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        LPWSTR endpointId = nullptr;
        QString id;
        if (SUCCEEDED(device->GetId(&endpointId)) && endpointId) {
            id = QString::fromWCharArray(endpointId);
            CoTaskMemFree(endpointId);
        }

        QString friendly =
            QCoreApplication::translate("VaaniSettings", "Microphone %1").arg(i + 1);
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value))) {
                if (value.vt == VT_LPWSTR && value.pwszVal)
                    friendly = QString::fromWCharArray(value.pwszVal);
            }
            PropVariantClear(&value);
            store->Release();
        }

        if (!id.isEmpty())
            out.push_back({ id, friendly });
        device->Release();
    }
    collection->Release();
#endif
    return out;
}

} // namespace Vaani