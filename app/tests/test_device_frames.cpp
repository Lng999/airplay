// test_device_frames.cpp - the device-frame table and geometry (src/ui/device_frames.*).
//
// Pure arithmetic over RECT/SIZE, so all of it runs in ctest with no window and no receiver.
// The reference model is the one this project actually connects with: "iPhone14,5", measured
// in %LOCALAPPDATA%\airplay\logs\gui.log.
#include "device_frames.h"

#include <cstdio>
#include <string>

namespace {

int g_tests = 0, g_checks = 0, g_failed = 0;
const char* g_name = "";

void beginTest(const char* name) {
    g_name = name;
    ++g_tests;
}

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL  [%s] line %d: %s\n", g_name, line, expr);
    }
}

void checkInt(long long got, long long want, const char* expr, int line) {
    ++g_checks;
    if (got != want) {
        ++g_failed;
        std::printf("FAIL  [%s] line %d: %s  got=%lld want=%lld\n", g_name, line, expr, got, want);
    }
}

#define CHECK(e)        check((e), #e, __LINE__)
#define CHECK_INT(a, b) checkInt((a), (b), #a, __LINE__)

int width(const RECT& r)  { return r.right - r.left; }
int height(const RECT& r) { return r.bottom - r.top; }

bool contains(const RECT& outer, const RECT& inner) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

bool approx(double a, double b, double tol) { return a > b - tol && a < b + tol; }

// ---------------------------------------------------------------------------

void testLookupExact() {
    beginTest("lookup, exact");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");
    CHECK(std::wstring(p.name) == L"iPhone 13");
    CHECK(p.cutout == ui::Cutout::Notch);
    CHECK(ui::isKnownDevice(L"iPhone14,5"));

    const ui::DeviceProfile q = ui::lookupDevice(L"iPhone15,4");
    CHECK(std::wstring(q.name) == L"iPhone 15");
    CHECK(q.cutout == ui::Cutout::Island);
}

void testLookupFallback() {
    beginTest("lookup, family fallback");
    // A phone released after this table was written still gets the right shape.
    const ui::DeviceProfile future = ui::lookupDevice(L"iPhone19,7");
    CHECK(future.cutout == ui::Cutout::Island);
    CHECK(std::wstring(future.name).empty());        // unnamed: we will not invent one
    CHECK(!ui::isKnownDevice(L"iPhone19,7"));

    CHECK(ui::lookupDevice(L"iPhone11,8").cutout == ui::Cutout::Notch);   // XR
    CHECK(ui::lookupDevice(L"iPhone9,3").cutout == ui::Cutout::None);     // 7, home button
    CHECK(ui::lookupDevice(L"iPad13,1").cutout == ui::Cutout::None);

    CHECK(ui::deviceWantsFrame(L"iPhone14,5"));
    CHECK(ui::deviceWantsFrame(L"iPad13,1"));
    CHECK(!ui::deviceWantsFrame(L"MacBookPro18,1"));   // a Mac is not a phone
    CHECK(!ui::deviceWantsFrame(L""));
}

// The phone sends its screen at its own aspect: no bars, guest sits exactly on the screen.
void testNativePortrait() {
    beginTest("native portrait stream");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");   // 1170x2532
    ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{600, 1000}, SIZE{1170, 2532}, p, false);

    CHECK(g.valid);
    CHECK(!g.cropped);
    CHECK(!g.landscape);
    CHECK(contains(g.body, g.screen));
    CHECK(g.screen.left > g.body.left && g.screen.bottom < g.body.bottom);   // a real bezel
    CHECK(width(g.guest) == width(g.screen) && height(g.guest) == height(g.screen));
    CHECK_INT(g.guestClip.left, 0);
    CHECK_INT(g.guestClip.top, 0);
    CHECK_INT(width(g.guestClip), width(g.screen));

    // the screen keeps the device aspect
    CHECK(approx(static_cast<double>(width(g.screen)) / height(g.screen), 1170.0 / 2532.0, 0.02));
    // and the whole thing fits in the window
    CHECK(g.body.left >= 0 && g.body.top >= 0 && g.body.right <= 600 && g.body.bottom <= 1000);
}

void testCorners() {
    beginTest("corners and bezel");
    ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{600, 1000}, SIZE{1170, 2532},
                                                ui::lookupDevice(L"iPhone14,5"), false);
    // A home-button phone has squarer corners than a notch/island one.
    ui::FrameGeometry s = ui::layoutDeviceFrame(SIZE{600, 1000}, SIZE{750, 1334},
                                                ui::lookupDevice(L"iPhone14,6"), false);
    CHECK(s.screenRadius < g.screenRadius);
    // The body hugs the screen: its corner is the screen's plus the bezel, and the bezel is
    // slim - a few percent of the screen width, not a phone body.
    const int bezel = g.screen.left - g.body.left;
    CHECK_INT(g.bodyRadius, g.screenRadius + bezel);
    CHECK(bezel > 0 && bezel * 25 < width(g.screen));
}

void testLandscapeStream() {
    beginTest("landscape stream, rotated frame");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");
    // The phone is on its side: the stream arrives 2532x1170.
    ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{1000, 600}, SIZE{2532, 1170}, p, false);
    CHECK(g.valid);
    CHECK(g.landscape);
    CHECK(!g.cropped);
    CHECK(width(g.screen) > height(g.screen));
    CHECK(contains(g.body, g.screen));
}

// The interesting case: a 16:9 stream carrying a portrait screen with bars either side.
void testPillarboxedStream() {
    beginTest("pillarboxed 16:9 stream");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");
    ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{600, 1000}, SIZE{1920, 1080}, p, false);

    CHECK(g.valid);
    CHECK(g.cropped);
    CHECK(!g.landscape);                          // the hint decided
    // The guest is blown up and pushed left so the bars miss the screen rect.
    CHECK(width(g.guest) > width(g.screen));
    CHECK(g.guest.left < g.screen.left);
    // What survives the clip is exactly the screen rect...
    CHECK_INT(width(g.guestClip), width(g.screen));
    CHECK(approx(static_cast<double>(height(g.guestClip)), height(g.screen), 2.0));
    // ...and it is taken from the middle of the stream.
    const int leftBar = g.guestClip.left;
    const int rightBar = width(g.guest) - g.guestClip.right;
    CHECK(approx(leftBar, rightBar, 2.0));
    CHECK(leftBar > 0);
    // guestClip is expressed in the guest's own coordinates, so it lands on the screen rect
    CHECK_INT(g.guest.left + g.guestClip.left, g.screen.left);

    // Same stream, but the user says the phone is on its side: bars top and bottom instead.
    ui::FrameGeometry h = ui::layoutDeviceFrame(SIZE{1000, 600}, SIZE{1920, 1080}, p, true);
    CHECK(h.landscape);
    CHECK(h.cropped);
    CHECK(h.guest.top < h.screen.top);
    CHECK(h.guestClip.top > 0);
}

void testDegenerate() {
    beginTest("degenerate input");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");
    CHECK(!ui::layoutDeviceFrame(SIZE{0, 0}, SIZE{1170, 2532}, p, false).valid);
    CHECK(!ui::layoutDeviceFrame(SIZE{600, 1000}, SIZE{0, 0}, p, false).valid);
    // A window far too small still produces something sane rather than negative rectangles.
    ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{40, 40}, SIZE{1170, 2532}, p, false);
    if (g.valid) {
        CHECK(width(g.body) >= 0 && height(g.body) >= 0);
        CHECK(contains(g.body, g.screen));
    }
}

void testFrameAspect() {
    beginTest("frameAspect");
    const ui::DeviceProfile p = ui::lookupDevice(L"iPhone14,5");
    const double portrait = ui::frameAspect(p, false);
    const double landscape = ui::frameAspect(p, true);
    CHECK(portrait < 1.0 && landscape > 1.0);
    CHECK(approx(portrait * landscape, 1.0, 0.001));   // exact inverses
    // the body is a little squarer than the screen, because of the bezel
    CHECK(portrait > 1170.0 / 2532.0);
}

void testRoundRectDistance() {
    beginTest("roundRectDistance");
    const RECT r{0, 0, 100, 200};
    CHECK(approx(ui::roundRectDistance(50, 100, r, 20), -50.0, 1e-9));   // centre: half width in
    CHECK(approx(ui::roundRectDistance(50, 0, r, 20), 0.0, 1e-9));       // on a straight edge
    CHECK(approx(ui::roundRectDistance(50, -3, r, 20), 3.0, 1e-9));      // 3 px outside it
    // The corner is a quarter circle around (20,20): the rect's own corner is outside it.
    CHECK(approx(ui::roundRectDistance(0, 0, r, 20), 20.0 * 1.41421356 - 20.0, 1e-6));
    CHECK(ui::roundRectDistance(3, 3, r, 0) < 0.0);                      // square corner: inside
}

unsigned alphaOf(unsigned px) { return px >> 24; }

void testFrameEdge() {
    beginTest("anti-aliased bezel overlay");
    const ui::FrameGeometry g = ui::layoutDeviceFrame(SIZE{400, 860}, SIZE{1170, 2532},
                                                      ui::lookupDevice(L"iPhone14,5"), false);
    CHECK(g.valid);
    const int w = 400, h = 860;
    std::string buf(static_cast<size_t>(w) * h * 4, '\0');
    auto* px = reinterpret_cast<unsigned*>(&buf[0]);
    ui::paintFrameEdge(px, w, h, POINT{0, 0}, g, RGB(5, 5, 6), RGB(0x48, 0x48, 0x50));
    auto at = [&](int x, int y) { return px[static_cast<size_t>(y) * w + x]; };

    const int midY = (g.body.top + g.body.bottom) / 2;
    const int bezelX = (g.body.left + g.screen.left) / 2;
    CHECK_INT(alphaOf(at(bezelX, midY)), 255);                           // the bezel is opaque
    CHECK_INT(alphaOf(at((g.screen.left + g.screen.right) / 2, midY)), 0);  // the picture shows
    CHECK_INT(alphaOf(at(g.body.left, g.body.top)), 0);                 // outside the round corner

    // The top-left corner is soft: a stepped (region) edge would have no partial pixels at
    // all; a one-pixel anti-aliased arc has roughly one per pixel of arc length.
    int partial = 0;
    for (int y = g.body.top; y < g.body.top + g.bodyRadius; ++y)
        for (int x = g.body.left; x < g.body.left + g.bodyRadius; ++x) {
            const unsigned a = alphaOf(at(x, y));
            if (a > 0 && a < 255) ++partial;
        }
    CHECK(partial >= g.bodyRadius);
    // and along its diagonal alpha only climbs, from 0 to fully opaque
    unsigned last = 0;
    bool monotonic = true;
    for (int i = 0; i < g.bodyRadius && last < 255; ++i) {
        const unsigned a = alphaOf(at(g.body.left + i, g.body.top + i));
        if (a < last) monotonic = false;
        last = a;
    }
    CHECK(monotonic);
    CHECK_INT(last, 255);

    // Premultiplied: no colour channel exceeds alpha.
    bool premul = true;
    for (int i = 0; i < w * h; ++i) {
        const unsigned p = px[i], a = p >> 24;
        if (((p >> 16) & 0xff) > a || ((p >> 8) & 0xff) > a || (p & 0xff) > a) premul = false;
    }
    CHECK(premul);
}

} // namespace

int main() {
    testLookupExact();
    testLookupFallback();
    testNativePortrait();
    testCorners();
    testLandscapeStream();
    testPillarboxedStream();
    testDegenerate();
    testFrameAspect();
    testRoundRectDistance();
    testFrameEdge();

    std::printf("%s: %d tests, %d checks, %d failed\n", g_failed ? "FAILED" : "OK", g_tests,
                g_checks, g_failed);
    return g_failed ? 1 : 0;
}
