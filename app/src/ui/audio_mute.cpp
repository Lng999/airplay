#include "audio_mute.h"

// MinGW's libuuid does not carry the audio-session GUIDs, so this translation unit defines
// them itself: <initguid.h> turns the DEFINE_GUID declarations in the headers below into
// actual symbols. It must stay above them.
#include <initguid.h>

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

namespace ui {
namespace {

// Small RAII release, so the many early exits below cannot leak an interface.
template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    void** vpp() { return reinterpret_cast<void**>(&p); }
};

} // namespace

bool setProcessMuted(unsigned long pid, bool muted) {
    if (!pid) return false;

    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator, devices.vpp())))
        return false;

    ComPtr<IMMDevice> endpoint;
    if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &endpoint)) || !endpoint)
        return false;

    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(endpoint->Activate(IID_IAudioSessionManager2, CLSCTX_ALL, nullptr,
                                  manager.vpp())) ||
        !manager)
        return false;

    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(&sessions)) || !sessions) return false;

    int count = 0;
    if (FAILED(sessions->GetCount(&count))) return false;

    bool touched = false;
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(i, &control)) || !control) continue;

        ComPtr<IAudioSessionControl2> control2;
        if (FAILED(control->QueryInterface(IID_IAudioSessionControl2, control2.vpp())) ||
            !control2)
            continue;

        DWORD sessionPid = 0;
        if (FAILED(control2->GetProcessId(&sessionPid)) || sessionPid != pid) continue;

        // A process can hold more than one session; mute them all rather than the first.
        ComPtr<ISimpleAudioVolume> volume;
        if (FAILED(control2->QueryInterface(IID_ISimpleAudioVolume, volume.vpp())) || !volume)
            continue;
        if (SUCCEEDED(volume->SetMute(muted ? TRUE : FALSE, nullptr))) touched = true;
    }
    return touched;
}

} // namespace ui
