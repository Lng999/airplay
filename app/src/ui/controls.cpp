#include "config_store.h"   // Win32 headers in the right order

#include "controls.h"

#include <d2d1.h>

#include "theme.h"
#include "win_util.h"

namespace ui {

const wchar_t* const kToggleClass = L"AirplayToggle";
const wchar_t* const kPushClass   = L"AirplayPush";

namespace {

struct State {
    bool  toggle = false;    // which class this is
    bool  checked = false;   // toggle only
    bool  hot = false, pressed = false, focused = false, spaceDown = false;
    HFONT font = nullptr;
};

State* stateOf(HWND h) { return reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

ID2D1Factory* d2d() {
    static ID2D1Factory* factory = [] {
        ID2D1Factory* f = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
        return f;
    }();
    return factory;
}

D2D1_COLOR_F color(COLORREF c, float a = 1.0f) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, a);
}

COLORREF mix(COLORREF a, COLORREF b, double t) {
    auto ch = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)),
               ch(GetBValue(a), GetBValue(b)));
}

void notifyClick(HWND h) {
    HWND parent = GetParent(h);
    if (!parent) return;
    const int id = GetDlgCtrlID(h);
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(h));
}

void click(HWND h) {
    State* st = stateOf(h);
    if (!st || !IsWindowEnabled(h)) return;
    if (st->toggle) {
        st->checked = !st->checked;
        InvalidateRect(h, nullptr, FALSE);
    }
    notifyClick(h);
}

void paint(HWND h, State* st) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(h, &ps);
    RECT rc{};
    GetClientRect(h, &rc);
    const int w = rc.right, hgt = rc.bottom;
    if (w <= 0 || hgt <= 0) { EndPaint(h, &ps); return; }

    // Double-buffered: the background, the shape and the text land on screen together.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, hgt);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HWND parent = GetParent(h);
    auto* bg = reinterpret_cast<HBRUSH>(
        SendMessageW(parent, WM_CTLCOLORBTN, reinterpret_cast<WPARAM>(mem),
                     reinterpret_cast<LPARAM>(h)));
    FillRect(mem, &rc, bg ? bg : GetSysColorBrush(COLOR_WINDOW));

    const Palette& p = activePalette();
    const bool enabled = IsWindowEnabled(h) != FALSE;
    // Like every Windows control: the focus ring only once the keyboard is in use.
    const bool ring = st->focused && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    const float scale = static_cast<float>(dpiForWindow(h)) / 96.0f;
    auto px = [scale](float v) { return v * scale; };

    wchar_t text[256] = L"";
    GetWindowTextW(h, text, 256);
    RECT textRc = rc;
    UINT textFmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
    COLORREF textColor = enabled ? p.text : p.textMuted;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ID2D1DCRenderTarget* rt = nullptr;
    if (d2d() && SUCCEEDED(d2d()->CreateDCRenderTarget(&props, &rt))) {
        rt->BindDC(mem, &rc);
        rt->BeginDraw();
        ID2D1SolidColorBrush* brush = nullptr;
        rt->CreateSolidColorBrush(color(p.accent), &brush);
        if (brush) {
            if (st->toggle) {
                // The switch sits at the right edge, vertically centred; the label takes the rest.
                const float tw = px(36), th = px(20);
                const float tx = w - tw - px(2), ty = (hgt - th) / 2.0f;
                const D2D1_ROUNDED_RECT track{D2D1::RectF(tx, ty, tx + tw, ty + th), th / 2, th / 2};
                COLORREF trackC = st->checked ? (st->hot ? p.accentHot : p.accent) : p.toggleOff;
                if (!enabled) trackC = mix(trackC, p.card, 0.5);
                brush->SetColor(color(trackC));
                rt->FillRoundedRectangle(track, brush);
                const float kr = th / 2 - px(2.5f);
                const float kx = st->checked ? tx + tw - th / 2 : tx + th / 2;
                brush->SetColor(color(p.knob));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ty + th / 2), kr, kr), brush);
                if (ring) {
                    brush->SetColor(color(p.accent, 0.55f));
                    const D2D1_ROUNDED_RECT ring{
                        D2D1::RectF(tx - px(2.5f), ty - px(2.5f), tx + tw + px(2.5f), ty + th + px(2.5f)),
                        th / 2 + px(2.5f), th / 2 + px(2.5f)};
                    rt->DrawRoundedRectangle(ring, brush, px(1.5f));
                }
                textRc.right = static_cast<LONG>(tx - px(10));
                textFmt |= DT_LEFT;
            } else {
                const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
                const bool quiet = (style & kQuietButton) != 0;
                const bool flat = !quiet && (style & BS_FLAT) != 0;
                const float inset = px(1);
                const float r = px(8);
                const D2D1_ROUNDED_RECT shape{
                    D2D1::RectF(inset, inset, w - inset, hgt - inset), r, r};
                if (quiet) {
                    if (st->hot || st->pressed) {
                        const COLORREF under = p.dark ? RGB(0xff, 0xff, 0xff) : RGB(0, 0, 0);
                        brush->SetColor(color(under, st->pressed ? 0.12f : 0.06f));
                        rt->FillRoundedRectangle(shape, brush);
                    }
                    textColor = enabled ? p.accent : p.textMuted;
                } else if (flat) {
                    COLORREF fill = st->pressed ? mix(p.card, p.border, 0.6)
                                    : st->hot   ? mix(p.card, p.border, 0.35) : p.card;
                    brush->SetColor(color(fill));
                    rt->FillRoundedRectangle(shape, brush);
                    brush->SetColor(color(p.border));
                    rt->DrawRoundedRectangle(shape, brush, px(1));
                } else {
                    COLORREF fill = st->pressed ? mix(p.accent, RGB(0, 0, 0), 0.15)
                                    : st->hot   ? p.accentHot : p.accent;
                    if (!enabled) fill = mix(p.accent, p.card, 0.55);
                    brush->SetColor(color(fill));
                    rt->FillRoundedRectangle(shape, brush);
                    textColor = p.onAccent;
                }
                if (ring) {
                    const bool onFill = !quiet && !flat;
                    brush->SetColor(color(onFill ? p.onAccent : p.accent, onFill ? 0.6f : 0.7f));
                    const float f = px(3);
                    const D2D1_ROUNDED_RECT ring{D2D1::RectF(f, f, w - f, hgt - f), r - px(2), r - px(2)};
                    rt->DrawRoundedRectangle(ring, brush, px(1.5f));
                }
                textFmt |= DT_CENTER;
            }
            brush->Release();
        }
        rt->EndDraw();
        rt->Release();
    }

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, textColor);
    HGDIOBJ oldFont = st->font ? SelectObject(mem, st->font) : nullptr;
    DrawTextW(mem, text, -1, &textRc, textFmt);
    if (oldFont) SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, w, hgt, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK controlProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = stateOf(h);
    switch (msg) {
        case WM_NCCREATE: {
            auto* s = new State();
            wchar_t cls[32] = L"";
            GetClassNameW(h, cls, 32);
            s->toggle = lstrcmpW(cls, kToggleClass) == 0;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            break;
        }
        case WM_NCDESTROY:
            delete st;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_SETFONT:
            if (st) st->font = reinterpret_cast<HFONT>(wp);
            if (LOWORD(lp)) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_GETFONT:
            return st ? reinterpret_cast<LRESULT>(st->font) : 0;
        case WM_SETTEXT: {
            const LRESULT r = DefWindowProcW(h, msg, wp, lp);
            InvalidateRect(h, nullptr, FALSE);
            return r;
        }
        case BM_GETCHECK:
            return st && st->checked ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK:
            if (st && st->toggle) {
                st->checked = wp == BST_CHECKED;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case BM_CLICK:
            click(h);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_BUTTON | (st && !st->toggle ? DLGC_UNDEFPUSHBUTTON : 0);
        case WM_ENABLE:
        case WM_UPDATEUISTATE:
            if (msg == WM_UPDATEUISTATE) DefWindowProcW(h, msg, wp, lp);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (st) {
                st->focused = msg == WM_SETFOCUS;
                if (!st->focused) st->spaceDown = false;
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (st && !st->hot) {
                st->hot = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (st) st->hot = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (st) st->pressed = true;
            SetCapture(h);
            if (GetFocus() != h) SetFocus(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (st && st->pressed) {
                st->pressed = false;
                ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
                RECT rc{};
                GetClientRect(h, &rc);
                const POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                if (PtInRect(&rc, pt)) click(h);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (st && st->pressed) {
                st->pressed = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_SPACE && st) {
                st->spaceDown = true;
                st->pressed = true;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_KEYUP:
            if (wp == VK_SPACE && st && st->spaceDown) {
                st->spaceDown = false;
                st->pressed = false;
                click(h);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (st) {
                paint(h, st);
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// A DC render target bound to the whole of `dc`'s client-sized area; nullptr on failure.
ID2D1DCRenderTarget* beginOn(HDC dc, const RECT& bounds) {
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ID2D1DCRenderTarget* rt = nullptr;
    if (!d2d() || FAILED(d2d()->CreateDCRenderTarget(&props, &rt))) return nullptr;
    rt->BindDC(dc, &bounds);
    rt->BeginDraw();
    return rt;
}

} // namespace

void paintRoundRect(HDC dc, const RECT& r, float radius, COLORREF fill, COLORREF border,
                    float borderWidth) {
    ID2D1DCRenderTarget* rt = beginOn(dc, r);
    if (!rt) return;
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(color(fill), &brush);
    if (brush) {
        const float w = static_cast<float>(r.right - r.left), h = static_cast<float>(r.bottom - r.top);
        const float in = borderWidth / 2;
        const D2D1_ROUNDED_RECT shape{D2D1::RectF(in, in, w - in, h - in), radius, radius};
        rt->FillRoundedRectangle(shape, brush);
        if (borderWidth > 0) {
            brush->SetColor(color(border));
            rt->DrawRoundedRectangle(shape, brush, borderWidth);
        }
        brush->Release();
    }
    rt->EndDraw();
    rt->Release();
}

void paintCircle(HDC dc, float cx, float cy, float radius, COLORREF fill) {
    const RECT box{static_cast<LONG>(cx - radius - 1), static_cast<LONG>(cy - radius - 1),
                   static_cast<LONG>(cx + radius + 2), static_cast<LONG>(cy + radius + 2)};
    ID2D1DCRenderTarget* rt = beginOn(dc, box);
    if (!rt) return;
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(color(fill), &brush);
    if (brush) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - box.left, cy - box.top), radius, radius),
                        brush);
        brush->Release();
    }
    rt->EndDraw();
    rt->Release();
}

bool registerControls(HINSTANCE hinst) {
    for (const wchar_t* cls : {kToggleClass, kPushClass}) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = controlProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = cls;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    return true;
}

} // namespace ui
