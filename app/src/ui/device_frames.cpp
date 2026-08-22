#include "device_frames.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

namespace ui {
namespace {

// Cut-out proportions, as fractions of the screen rect.
//   notch, 12 family : 209pt of a 390pt screen, 30pt of 844pt
//   notch, 13 and up : 162pt of 390pt - Apple narrowed it by about a fifth
//   island           : 125pt of 393pt, 37pt of 852pt
constexpr double kNotchWideW = 0.53, kNotchWideH = 0.036;
constexpr double kNotchW     = 0.42, kNotchH     = 0.038;
constexpr double kIslandW    = 0.32, kIslandH    = 0.043;

// The pill sits a little below the top edge; the notch hangs off it.
constexpr double kIslandTop = 0.013;

// Bezel and corner radii, as fractions of the screen's short side.
constexpr double kBezel        = 0.030;
constexpr double kRoundCorner  = 0.115;   // notched/island phones: very round
constexpr double kSquareCorner = 0.020;   // everything else

const DeviceProfile kDevices[] = {
    // --- notch, 12 family (the wide one) ---
    {L"iPhone13,1", L"iPhone 12 mini",    1080, 2340, Cutout::Notch,  kNotchWideW, kNotchWideH},
    {L"iPhone13,2", L"iPhone 12",         1170, 2532, Cutout::Notch,  kNotchWideW, kNotchWideH},
    {L"iPhone13,3", L"iPhone 12 Pro",     1170, 2532, Cutout::Notch,  kNotchWideW, kNotchWideH},
    {L"iPhone13,4", L"iPhone 12 Pro Max", 1284, 2778, Cutout::Notch,  kNotchWideW, kNotchWideH},
    // --- notch, 13/14 family (narrower) ---
    {L"iPhone14,4", L"iPhone 13 mini",    1080, 2340, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone14,5", L"iPhone 13",         1170, 2532, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone14,2", L"iPhone 13 Pro",     1170, 2532, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone14,3", L"iPhone 13 Pro Max", 1284, 2778, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone14,7", L"iPhone 14",         1170, 2532, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone14,8", L"iPhone 14 Plus",    1284, 2778, Cutout::Notch,  kNotchW, kNotchH},
    {L"iPhone17,5", L"iPhone 16e",        1170, 2532, Cutout::Notch,  kNotchW, kNotchH},
    // --- Dynamic Island ---
    {L"iPhone15,2", L"iPhone 14 Pro",     1179, 2556, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone15,3", L"iPhone 14 Pro Max", 1290, 2796, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone15,4", L"iPhone 15",         1179, 2556, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone15,5", L"iPhone 15 Plus",    1290, 2796, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone16,1", L"iPhone 15 Pro",     1179, 2556, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone16,2", L"iPhone 15 Pro Max", 1290, 2796, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone17,1", L"iPhone 16 Pro",     1206, 2622, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone17,2", L"iPhone 16 Pro Max", 1320, 2868, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone17,3", L"iPhone 16",         1179, 2556, Cutout::Island, kIslandW, kIslandH},
    {L"iPhone17,4", L"iPhone 16 Plus",    1290, 2796, Cutout::Island, kIslandW, kIslandH},
    // --- home button era, and the SE that kept it ---
    {L"iPhone12,8", L"iPhone SE (2. nesil)", 750, 1334, Cutout::None, 0.0, 0.0},
    {L"iPhone14,6", L"iPhone SE (3. nesil)", 750, 1334, Cutout::None, 0.0, 0.0},
};

// Family fallbacks. Apple has never gone back from a cut-out, so "newer than the island
// generation" means island; the notch generations are 10 (iPhone X) through 14.
const DeviceProfile kIslandGeneric{L"", L"", 1179, 2556, Cutout::Island, kIslandW, kIslandH};
const DeviceProfile kNotchGeneric {L"", L"", 1170, 2532, Cutout::Notch,  kNotchW,  kNotchH};
const DeviceProfile kPlainPhone   {L"", L"",  750, 1334, Cutout::None,   0.0,      0.0};
const DeviceProfile kTablet       {L"", L"", 1640, 2360, Cutout::None,   0.0,      0.0};

bool startsWith(const std::wstring& s, const wchar_t* p) {
    const size_t n = wcslen(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

// "iPhone14,5" -> 14. -1 when the shape is not <letters><number>,<number>.
int familyNumber(const std::wstring& model) {
    size_t i = 0;
    while (i < model.size() && !iswdigit(model[i])) ++i;
    if (i >= model.size()) return -1;
    return static_cast<int>(wcstol(model.c_str() + i, nullptr, 10));
}

int iround(double v) { return static_cast<int>(v < 0 ? v - 0.5 : v + 0.5); }

} // namespace

const DeviceProfile& lookupDevice(const std::wstring& model) {
    for (const DeviceProfile& d : kDevices)
        if (model == d.id) return d;

    if (startsWith(model, L"iPad")) return kTablet;
    if (startsWith(model, L"iPhone")) {
        const int fam = familyNumber(model);
        if (fam >= 15) return kIslandGeneric;   // 14 Pro onwards
        if (fam >= 10) return kNotchGeneric;    // iPhone X .. 14
        if (fam > 0)   return kPlainPhone;      // 8 and older
        return kNotchGeneric;
    }
    return kPlainPhone;
}

bool isKnownDevice(const std::wstring& model) {
    for (const DeviceProfile& d : kDevices)
        if (model == d.id) return true;
    return false;
}

bool deviceWantsFrame(const std::wstring& model) {
    return startsWith(model, L"iPhone") || startsWith(model, L"iPad");
}

double frameAspect(const DeviceProfile& profile, bool landscape) {
    if (profile.screenW <= 0 || profile.screenH <= 0) return 0.0;
    double sw = profile.screenW, sh = profile.screenH;
    if (landscape) std::swap(sw, sh);
    // Normalise so the short side is 1, then add the bezel on all four sides.
    const double shortSide = (std::min)(sw, sh);
    const double bezel = kBezel * shortSide;
    return (sw + 2 * bezel) / (sh + 2 * bezel);
}

FrameGeometry layoutDeviceFrame(SIZE client, SIZE stream, const DeviceProfile& profile,
                                bool landscapeHint) {
    FrameGeometry g;
    if (client.cx <= 0 || client.cy <= 0 || stream.cx <= 0 || stream.cy <= 0) return g;
    if (profile.screenW <= 0 || profile.screenH <= 0) return g;

    const double portrait = static_cast<double>(profile.screenW) / profile.screenH;  // < 1
    const double streamAspect = static_cast<double>(stream.cx) / stream.cy;

    // Which way up is the phone? If the stream already has one of the device's two aspects,
    // it is telling us directly. Otherwise it is carrying bars - and bars look identical
    // whichever way the phone is held, so the caller's hint decides (and the user can flip
    // it). 4% covers rounding in the encoder's chosen dimensions.
    const double tol = 0.04;
    auto matches = [&](double a) { return std::fabs(streamAspect - a) <= tol * a; };
    if (matches(1.0 / portrait)) {
        g.landscape = true;
        g.cropped = false;
    } else if (matches(portrait)) {
        g.landscape = false;
        g.cropped = false;
    } else {
        g.landscape = landscapeHint;
        g.cropped = true;
    }

    const double contentAspect = g.landscape ? 1.0 / portrait : portrait;

    // --- body and screen, in client coordinates -------------------------------------------
    // Work in units where the screen's short side is 1.
    double screenW = contentAspect >= 1.0 ? contentAspect : 1.0;
    double screenH = contentAspect >= 1.0 ? 1.0 : 1.0 / contentAspect;
    // (screenW/screenH == contentAspect either way, with min(screenW,screenH) == 1)
    const double bezel = kBezel;
    const double bodyW = screenW + 2 * bezel, bodyH = screenH + 2 * bezel;

    const double scale = (std::min)(client.cx / bodyW, client.cy / bodyH);
    if (scale <= 0.0) return g;

    const int bw = iround(bodyW * scale), bh = iround(bodyH * scale);
    const int bx = (client.cx - bw) / 2, by = (client.cy - bh) / 2;
    g.body = RECT{bx, by, bx + bw, by + bh};

    const int inset = iround(bezel * scale);
    g.screen = RECT{bx + inset, by + inset, bx + bw - inset, by + bh - inset};

    const int screenPixW = g.screen.right - g.screen.left;
    const int screenPixH = g.screen.bottom - g.screen.top;
    if (screenPixW <= 0 || screenPixH <= 0) return g;

    const int shortPix = (std::min)(screenPixW, screenPixH);
    const double cornerFrac = profile.cutout == Cutout::None ? kSquareCorner : kRoundCorner;
    g.screenRadius = iround(cornerFrac * shortPix);
    g.bodyRadius = g.screenRadius + inset;

    // --- the cut-out ----------------------------------------------------------------------
    if (profile.cutout != Cutout::None) {
        // The fractions are written for the portrait screen; rotate them with the phone.
        const int cw = iround(profile.cutoutW * (g.landscape ? screenPixH : screenPixW));
        const int ch = iround(profile.cutoutH * (g.landscape ? screenPixW : screenPixH));
        g.cutoutAtEdge = profile.cutout == Cutout::Notch;
        if (!g.landscape) {
            const int x = g.screen.left + (screenPixW - cw) / 2;
            const int top = g.cutoutAtEdge ? g.screen.top
                                           : g.screen.top + iround(kIslandTop * screenPixH);
            g.cutout = RECT{x, top, x + cw, top + ch};
            g.cutoutRadius = g.cutoutAtEdge ? iround(ch * 0.55) : ch / 2;
        } else {
            // Rotated a quarter turn: the cut-out lives on the left edge and stands upright.
            const int y = g.screen.top + (screenPixH - cw) / 2;
            const int left = g.cutoutAtEdge ? g.screen.left
                                            : g.screen.left + iround(kIslandTop * screenPixW);
            g.cutout = RECT{left, y, left + ch, y + cw};
            g.cutoutRadius = g.cutoutAtEdge ? iround(ch * 0.55) : ch / 2;
        }
    }

    // --- where the guest window goes --------------------------------------------------------
    if (!g.cropped) {
        g.guest = g.screen;
        g.guestClip = RECT{0, 0, screenPixW, screenPixH};
    } else {
        // The phone screen is a centred sub-rectangle of the stream at contentAspect; the
        // rest is bars. Scale the whole stream so that sub-rectangle lands on g.screen, and
        // let the bars fall outside - guestClip cuts them off.
        double contentW = stream.cx, contentH = stream.cy;
        if (streamAspect > contentAspect) {
            contentW = stream.cy * contentAspect;      // bars left and right
        } else {
            contentH = stream.cx / contentAspect;      // bars top and bottom
        }
        const double zoom = screenPixW / contentW;
        const int gw = iround(stream.cx * zoom), gh = iround(stream.cy * zoom);
        const int offX = iround((stream.cx - contentW) / 2 * zoom);
        const int offY = iround((stream.cy - contentH) / 2 * zoom);
        g.guest = RECT{g.screen.left - offX, g.screen.top - offY,
                       g.screen.left - offX + gw, g.screen.top - offY + gh};
        g.guestClip = RECT{offX, offY, offX + screenPixW, offY + screenPixH};
    }

    g.valid = true;
    return g;
}

} // namespace ui
