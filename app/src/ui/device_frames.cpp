#include "device_frames.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

namespace ui {
namespace {

// Bezel and corner radii, as fractions of the screen's short side. The bezel is the slim
// macOS iPhone Mirroring one, not a full phone body.
constexpr double kBezel        = 0.018;
constexpr double kRoundCorner  = 0.115;   // notched/island phones: very round
constexpr double kSquareCorner = 0.020;   // everything else

const DeviceProfile kDevices[] = {
    // --- notch, 12 family (the wide one) ---
    {L"iPhone13,1", L"iPhone 12 mini",    1080, 2340, Cutout::Notch},
    {L"iPhone13,2", L"iPhone 12",         1170, 2532, Cutout::Notch},
    {L"iPhone13,3", L"iPhone 12 Pro",     1170, 2532, Cutout::Notch},
    {L"iPhone13,4", L"iPhone 12 Pro Max", 1284, 2778, Cutout::Notch},
    // --- notch, 13/14 family (narrower) ---
    {L"iPhone14,4", L"iPhone 13 mini",    1080, 2340, Cutout::Notch},
    {L"iPhone14,5", L"iPhone 13",         1170, 2532, Cutout::Notch},
    {L"iPhone14,2", L"iPhone 13 Pro",     1170, 2532, Cutout::Notch},
    {L"iPhone14,3", L"iPhone 13 Pro Max", 1284, 2778, Cutout::Notch},
    {L"iPhone14,7", L"iPhone 14",         1170, 2532, Cutout::Notch},
    {L"iPhone14,8", L"iPhone 14 Plus",    1284, 2778, Cutout::Notch},
    {L"iPhone17,5", L"iPhone 16e",        1170, 2532, Cutout::Notch},
    // --- Dynamic Island ---
    {L"iPhone15,2", L"iPhone 14 Pro",     1179, 2556, Cutout::Island},
    {L"iPhone15,3", L"iPhone 14 Pro Max", 1290, 2796, Cutout::Island},
    {L"iPhone15,4", L"iPhone 15",         1179, 2556, Cutout::Island},
    {L"iPhone15,5", L"iPhone 15 Plus",    1290, 2796, Cutout::Island},
    {L"iPhone16,1", L"iPhone 15 Pro",     1179, 2556, Cutout::Island},
    {L"iPhone16,2", L"iPhone 15 Pro Max", 1290, 2796, Cutout::Island},
    {L"iPhone17,1", L"iPhone 16 Pro",     1206, 2622, Cutout::Island},
    {L"iPhone17,2", L"iPhone 16 Pro Max", 1320, 2868, Cutout::Island},
    {L"iPhone17,3", L"iPhone 16",         1179, 2556, Cutout::Island},
    {L"iPhone17,4", L"iPhone 16 Plus",    1290, 2796, Cutout::Island},
    // --- home button era, and the SE that kept it ---
    {L"iPhone12,8", L"iPhone SE (2. nesil)", 750, 1334, Cutout::None},
    {L"iPhone14,6", L"iPhone SE (3. nesil)", 750, 1334, Cutout::None},
};

// Family fallbacks. Apple has never gone back from a cut-out, so "newer than the island
// generation" means island; the notch generations are 10 (iPhone X) through 14.
const DeviceProfile kIslandGeneric{L"", L"", 1179, 2556, Cutout::Island};
const DeviceProfile kNotchGeneric {L"", L"", 1170, 2532, Cutout::Notch};
const DeviceProfile kPlainPhone   {L"", L"",  750, 1334, Cutout::None};
const DeviceProfile kTablet       {L"", L"", 1640, 2360, Cutout::None};

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

double roundRectDistance(double x, double y, const RECT& r, double radius) {
    const double hx = (r.right - r.left) / 2.0, hy = (r.bottom - r.top) / 2.0;
    radius = (std::max)(0.0, (std::min)({radius, hx, hy}));
    const double px = std::fabs(x - (r.left + hx)) - (hx - radius);
    const double py = std::fabs(y - (r.top + hy)) - (hy - radius);
    const double outside = std::hypot((std::max)(px, 0.0), (std::max)(py, 0.0));
    const double inside = (std::min)((std::max)(px, py), 0.0);
    return outside + inside - radius;
}

void paintFrameEdge(unsigned int* pixels, int w, int h, POINT origin, const FrameGeometry& g,
                    COLORREF body, COLORREF hairline) {
    auto cover = [](double d) { return (std::min)(1.0, (std::max)(0.0, 0.5 - d)); };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned int& out = pixels[static_cast<size_t>(y) * w + x];
            out = 0;
            if (!g.valid) continue;
            const double cx = x - origin.x + 0.5, cy = y - origin.y + 0.5;
            const double dOuter = roundRectDistance(cx, cy, g.body, g.bodyRadius);
            if (dOuter >= 0.5) continue;                       // outside the phone
            const double dInner = roundRectDistance(cx, cy, g.screen, g.screenRadius);
            if (dInner <= -0.5) continue;                      // on the picture
            const double a = (std::min)(cover(dOuter), cover(-dInner));
            if (a <= 0.0) continue;
            // the hairline: roughly the outermost pixel of the body
            const double line = (std::min)(1.0, (cover(dOuter) - cover(dOuter + 1.0)) / a);
            auto mix = [&](int b, int e) { return b + (e - b) * line; };
            const double r = mix(GetRValue(body), GetRValue(hairline));
            const double gr = mix(GetGValue(body), GetGValue(hairline));
            const double b = mix(GetBValue(body), GetBValue(hairline));
            auto ch = [&](double c) { return static_cast<unsigned int>(c * a + 0.5); };
            out = (ch(255.0) << 24) | (ch(r) << 16) | (ch(gr) << 8) | ch(b);
        }
    }
}

} // namespace ui
