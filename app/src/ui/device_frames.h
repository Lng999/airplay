// device_frames.h - drawing the connected phone around the picture.
//
// The client tells us what it is: the SETUP plist carries a "model" key
// (third_party/UxPlay/lib/raop_handlers.h:749), UxPlay prints it, and it reaches us as
// HostEvent::clientModel. It is Apple's internal identifier - measured on this machine:
//
//     connection request from mustafanın iPhone (iPhone14,5) with deviceID = 46:F2:...
//
// iPhone14,5 is an iPhone 13. That is enough to know the screen aspect, the corner radius
// and - the point of the exercise - whether the top carries a notch or a Dynamic Island.
//
// Everything here is pure geometry over plain RECT/SIZE: no GDI, no HWND, no drawing. That
// keeps the hard part (where does the picture go, what has to be cut out of it) unit-testable
// without a window - see app/tests/test_device_frames.cpp.
#pragma once

#include "config_store.h"   // Win32 headers in the right order (RECT, SIZE)

#include <string>

namespace ui {

enum class Cutout {
    None,     // pre-X phones, iPads, anything we do not recognise as a notched phone
    Notch,    // attached to the top edge, rounded at the bottom two corners
    Island    // a floating pill a little below the top edge
};

struct DeviceProfile {
    const wchar_t* id;        // exact identifier, "" for the generated fallbacks
    const wchar_t* name;      // marketing name for the window title, "" when generic
    int            screenW;   // native portrait pixels; only the ratio is used
    int            screenH;
    Cutout         cutout;
    // Cut-out size as a fraction of the screen rect. The 12 family had a notably wider
    // notch than the 13 and later, and the Dynamic Island is smaller than both.
    double         cutoutW;
    double         cutoutH;
};

// Never fails: an unrecognised model falls back to a family rule (iPhoneNN,M -> notch or
// island by NN) and finally to a generic phone. `name` is empty for both fallbacks.
const DeviceProfile& lookupDevice(const std::wstring& model);

// True when the model matched an exact table entry, i.e. we can name it.
bool isKnownDevice(const std::wstring& model);

// Whether a device frame makes any sense for this client. A Mac mirroring its desktop is
// not a phone, and drawing a phone around it would be a lie rather than a decoration.
bool deviceWantsFrame(const std::wstring& model);

// What to draw and where to put the guest window. All rectangles are in the picture
// window's client coordinates, except guestClip which is in the guest's own coordinates.
struct FrameGeometry {
    bool valid = false;

    RECT body{};          // the phone body, a rounded rect we paint
    int  bodyRadius = 0;
    RECT screen{};        // the display area: where the mirrored picture must appear
    int  screenRadius = 0;

    RECT cutout{};        // notch or island; empty when Cutout::None
    int  cutoutRadius = 0;
    bool cutoutAtEdge = false;   // notch: merge with the top edge. island: floating.

    // Where the guest window goes. It may be larger than the client area: when the phone
    // pillarboxes its portrait screen inside a 16:9 stream, the bars are pushed outside the
    // visible screen rect and clipped away by guestClip.
    RECT guest{};
    RECT guestClip{};     // the part of the guest that is the phone screen, guest-local
    bool cropped = false; // true when the stream carried bars we had to cut off
    bool landscape = false;
};

// client  - our window's client size
// stream  - the video size the receiver is producing (the adopted window's client size)
// profile - the connected device
// landscapeHint - what to assume when the stream aspect matches neither orientation, which
//                 is exactly the pillarboxed case. false = assume the phone is upright.
FrameGeometry layoutDeviceFrame(SIZE client, SIZE stream, const DeviceProfile& profile,
                                bool landscapeHint);

// The aspect the picture window should keep while the frame is on: body width / body height
// for this device and orientation. 0 when unknown.
double frameAspect(const DeviceProfile& profile, bool landscape);

} // namespace ui
