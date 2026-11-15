#include "main_window.h"

#include <commctrl.h>
#include <ipifcons.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <vector>

#include "../res/resource.h"
#include "autostart.h"
#include "controls.h"
#include "device_frames.h"
#include "single_instance.h"
#include "stale_receivers.h"
#include "strings.h"
#include "theme.h"
#include "updater.h"
#include "version.h"
#include "video_embed.h"
#include "win_util.h"

namespace ui {

// What an updater worker thread hands back to the window. Allocated by the thread, deleted
// by the window procedure - the same ownership rule as WM_HOST_EVENT.
struct UpdateResult {
    UpdateInfo   info;
    std::string  err;          // UTF-8, empty on success
    bool         found = false;   // a newer release exists
    bool         manual = false;  // the user asked, so "up to date" is worth saying out loud
    std::wstring localPath;    // WM_UPDATE_DOWNLOADED: the installer on disk
    bool         integrityFailed = false;   // WM_UPDATE_DOWNLOADED: SHA-256 did not match
};

namespace {

enum : int {
    IDC_STATUS = 1001,
    IDC_HINT,
    IDC_LBL_NAME,
    IDC_EDIT_NAME,
    IDC_LBL_PORT,
    IDC_EDIT_PORT,
    IDC_LBL_VIDEO,
    IDC_CMB_VIDEO,
    IDC_LBL_AUDIO,
    IDC_CMB_AUDIO,
    IDC_LBL_FPS,
    IDC_CMB_FPS,
    IDC_LBL_DECODER,
    IDC_CMB_DECODER,
    IDC_LBL_RESET,
    IDC_CMB_RESET,
    IDC_CHK_FULLSCREEN,
    IDC_CHK_H265,
    IDC_CHK_DEBUG,
    IDC_CHK_ALWAYSONTOP,
    IDC_CHK_AUTOSTART,
    IDC_CHK_EMBED,
    IDC_CHK_LOGON,
    IDC_CHK_FRAME,
    IDC_BTN_TOGGLE,
    IDC_BTN_COPY,
    IDC_BTN_SETTINGS,
    IDC_BTN_BACK,
    IDC_LIST_LOG
};

const wchar_t* const kVideoSinks[] = {L"d3d11videosink", L"d3d12videosink", L"autovideosink"};
const wchar_t* const kAudioSinks[] = {L"autoaudiosink", L"wasapi2sink", L"wasapisink",
                                      L"directsoundsink"};

// The frame-rate ceiling handed to the client as maxFPS. Index-matched pairs.
const wchar_t* const kFpsLabels[] = {str::kFps60, str::kFps30};
const int            kFpsValues[] = {60, 30};

// Decoder choices: label shown, value passed to -vd. "" = decodebin (UxPlay's default).
const wchar_t* const kDecoderLabels[] = {str::kDecAuto, str::kDecD3D11, str::kDecNv,
                                         str::kDecSw};
const wchar_t* const kDecoderValues[] = {L"", L"d3d11h264dec", L"nvh264dec", L"avdec_h264"};

// -reset n: how long the client may stay silent. Index-matched pairs.
const wchar_t* const kResetLabels[] = {str::kReset0, str::kReset15, str::kReset60};
const int            kResetValues[] = {0, 15, 60};

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(n) + 1, L'\0');
    int got = GetWindowTextW(h, &out[0], n + 1);
    out.resize(static_cast<size_t>(got < 0 ? 0 : got));
    return out;
}

// The status dot. Colours are picked for the light system theme the rest of the window
// already assumes (COLOR_BTNFACE background).
COLORREF stateColor(airplay::HostState st) {
    switch (st) {
        case airplay::HostState::Waiting:   return RGB(0x1c, 0x6c, 0xc8);   // blue: advertising
        case airplay::HostState::Connected: return RGB(0x1a, 0x9c, 0x4a);   // green: mirroring
        case airplay::HostState::Starting:
        case airplay::HostState::Stopping:  return RGB(0xd8, 0x8a, 0x16);   // amber: transient
        case airplay::HostState::Error:     return RGB(0xc4, 0x28, 0x28);   // red
        case airplay::HostState::Stopped:
        default:                            return RGB(0x96, 0x96, 0x96);   // grey: idle
    }
}

bool isChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void setChecked(HWND h, bool on) {
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') out.push_back(L'\\');
        out.push_back(c);
    }
    out.push_back(L'"');
    return out;
}

bool copyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------

// The marketing name for a model identifier, or "" when the table has never heard of it.
const std::wstring& deviceDisplayName(const std::wstring& model) {
    static const std::wstring empty;
    static std::wstring cached, cachedFor;
    if (model == cachedFor) return cached;
    cachedFor = model;
    cached = isKnownDevice(model) ? lookupDevice(model).name : L"";
    return cached.empty() ? empty : cached;
}

std::wstring firstLocalIPv4() {
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;

    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return std::wstring();

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const sockaddr* sa = u->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET) continue;
            const auto* sin = reinterpret_cast<const sockaddr_in*>(sa);
            const unsigned char* o = reinterpret_cast<const unsigned char*>(&sin->sin_addr);
            if (o[0] == 127 || (o[0] == 169 && o[1] == 254)) continue;  // loopback / APIPA
            wchar_t out[32];
            _snwprintf(out, 32, L"%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
            return std::wstring(out);
        }
    }
    return std::wstring();
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(HINSTANCE hinst, ConfigStore& store, AppConfig& cfg)
    : hinst_(hinst), store_(store), cfg_(cfg), video_(hinst, cfg) {}

MainWindow::~MainWindow() {
    host_.setCallback(nullptr);
    releaseVideo();
    host_.stop();
    joinStopThread();
    for (HGDIOBJ o : {static_cast<HGDIOBJ>(fontUi_),
                      static_cast<HGDIOBJ>(fontHead_), static_cast<HGDIOBJ>(fontTitle_),
                      static_cast<HGDIOBJ>(fontMono_), static_cast<HGDIOBJ>(brWindow_),
                      static_cast<HGDIOBJ>(brCard_)})
        if (o) DeleteObject(o);
}

bool MainWindow::create() {
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWindow::wndProcThunk;
    wc.hInstance     = hinst_;
    // LR_SHARED: the system owns these, so they must not be destroyed by us.
    wc.hIcon   = static_cast<HICON>(LoadImageW(hinst_, MAKEINTRESOURCEW(IDI_APPICON),
                                               IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hinst_, MAKEINTRESOURCEW(IDI_APPICON),
                                               IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // paintWindow() paints all of it, in the theme's colours
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    if (!registerControls(hinst_)) return false;

    int x = cfg_.x, y = cfg_.y, w = cfg_.w, h = cfg_.h;
    if (w < 200 || h < 200) {
        x = y = CW_USEDEFAULT;
        w = h = CW_USEDEFAULT;
    } else if (!rectOnSomeMonitor(RECT{x, y, x + w, y + h})) {
        // Saved on a monitor that is gone: keep the size, let Windows place it. y must be
        // CW_USEDEFAULT too, or CreateWindowEx would take it as a ShowWindow command.
        x = y = CW_USEDEFAULT;
    }

    hwnd_ = CreateWindowExW(0, kWindowClass, L"airplay - AirPlay Receiver",
                            WS_OVERLAPPEDWINDOW, x, y, w, h,
                            nullptr, nullptr, hinst_, this);
    return hwnd_ != nullptr;
}

void MainWindow::show(bool minimizedToTray) {
    if (!hwnd_) return;
    if (minimizedToTray) {
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK MainWindow::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wndProc(msg, wp, lp);
}

void MainWindow::createFonts() {
    for (HFONT f : {fontUi_, fontHead_, fontTitle_, fontMono_})
        if (f) DeleteObject(f);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    LOGFONTW lf{};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        lf = ncm.lfMessageFont;
    } else {
        lf.lfHeight = -12;
        lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    }
    LOGFONTW base = lf;
    base.lfHeight = -MulDiv(9, dpi_, 72);
    fontUi_ = CreateFontIndirectW(&base);

    LOGFONTW head = base;
    head.lfWeight = FW_SEMIBOLD;
    fontHead_ = CreateFontIndirectW(&head);

    LOGFONTW title = lf;
    title.lfHeight = -MulDiv(13, dpi_, 72);
    title.lfWeight = FW_SEMIBOLD;
    fontTitle_ = CreateFontIndirectW(&title);

    LOGFONTW mono = base;
    lstrcpynW(mono.lfFaceName, L"Consolas", LF_FACESIZE);
    fontMono_ = CreateFontIndirectW(&mono);
}

void MainWindow::createControls() {
    const DWORD kChild = WS_CHILD | WS_VISIBLE;
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id,
                  DWORD exStyle = 0) {
        return CreateWindowExW(exStyle, cls, text, kChild | style, 0, 0, 10, 10, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hinst_,
                               nullptr);
    };

    status_ = mk(L"STATIC", str::kStateStopped, SS_LEFT | SS_ENDELLIPSIS, IDC_STATUS);
    hint_   = mk(L"STATIC", str::kHintStopped, SS_LEFT, IDC_HINT);   // wraps to two lines

    lblName_  = mk(L"STATIC", str::kLabelName, SS_LEFT, IDC_LBL_NAME);
    editName_ = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_EDIT_NAME, WS_EX_CLIENTEDGE);
    lblPort_  = mk(L"STATIC", str::kLabelPort, SS_LEFT, IDC_LBL_PORT);
    editPort_ = mk(L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, IDC_EDIT_PORT,
                   WS_EX_CLIENTEDGE);

    lblVideo_ = mk(L"STATIC", str::kLabelVideo, SS_LEFT, IDC_LBL_VIDEO);
    cmbVideo_ = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_CMB_VIDEO);
    lblAudio_ = mk(L"STATIC", str::kLabelAudio, SS_LEFT, IDC_LBL_AUDIO);
    cmbAudio_ = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_CMB_AUDIO);

    lblFps_     = mk(L"STATIC", str::kLabelFps, SS_LEFT, IDC_LBL_FPS);
    cmbFps_     = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_CMB_FPS);
    lblReset_   = mk(L"STATIC", str::kLabelReset, SS_LEFT, IDC_LBL_RESET);
    cmbReset_   = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                     IDC_CMB_RESET);
    lblDecoder_ = mk(L"STATIC", str::kLabelDecoder, SS_LEFT, IDC_LBL_DECODER);
    cmbDecoder_ = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                     IDC_CMB_DECODER);

    for (const wchar_t* f : kFpsLabels)
        SendMessageW(cmbFps_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f));
    for (const wchar_t* d : kDecoderLabels)
        SendMessageW(cmbDecoder_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(d));
    for (const wchar_t* r : kResetLabels)
        SendMessageW(cmbReset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r));

    for (const wchar_t* v : kVideoSinks)
        SendMessageW(cmbVideo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    for (const wchar_t* a : kAudioSinks)
        SendMessageW(cmbAudio_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(a));

    // Switches and rounded buttons (controls.h); to this code they are checkboxes and buttons.
    auto toggle = [&](const wchar_t* text, int id) { return mk(kToggleClass, text, WS_TABSTOP, id); };
    chkFullscreen_  = toggle(str::kChkFullscreen, IDC_CHK_FULLSCREEN);
    chkH265_        = toggle(str::kChkH265, IDC_CHK_H265);
    chkDebug_       = toggle(str::kChkDebug, IDC_CHK_DEBUG);
    chkAlwaysOnTop_ = toggle(str::kChkAlwaysOnTop, IDC_CHK_ALWAYSONTOP);
    chkAutostart_   = toggle(str::kChkAutostart, IDC_CHK_AUTOSTART);
    chkEmbed_       = toggle(str::kChkEmbed, IDC_CHK_EMBED);
    chkLogon_       = toggle(str::kChkLogon, IDC_CHK_LOGON);
    chkFrame_       = toggle(str::kChkFrame, IDC_CHK_FRAME);

    // A normal-sized button on the receiver card: accent "Başlat", or a plain "Durdur" while
    // it runs (updateButtons). Not a full-width bar.
    btnToggle_   = mk(kPushClass, str::kBtnStart, WS_TABSTOP, IDC_BTN_TOGGLE);
    hdrReceiver_ = mk(L"STATIC", str::kReceiverTitle, SS_LEFT, -1);
    btnCopy_     = mk(kPushClass, str::kBtnCopyCmd, BS_FLAT | WS_TABSTOP, IDC_BTN_COPY);
    btnSettings_ = mk(kPushClass, str::kBtnSettings, kQuietButton | WS_TABSTOP, IDC_BTN_SETTINGS);
    btnBack_     = mk(kPushClass, str::kBtnBack, kQuietButton | WS_TABSTOP, IDC_BTN_BACK);

    // Settings page title and group headings; -1 is the usual id of a static that sends nothing.
    hdrTitle_   = mk(L"STATIC", str::kSettingsTitle, SS_LEFT, -1);
    hdrGeneral_ = mk(L"STATIC", str::kGrpGeneral, SS_LEFT, -1);
    hdrConn_    = mk(L"STATIC", str::kGrpConnection, SS_LEFT, -1);
    hdrVideo_   = mk(L"STATIC", str::kGrpVideo, SS_LEFT, -1);
    hdrAudio_   = mk(L"STATIC", str::kGrpAudio, SS_LEFT, -1);
    hdrApp_     = mk(L"STATIC", str::kGrpApp, SS_LEFT, -1);
    hdrLog_     = mk(L"STATIC", str::kGrpLog, SS_LEFT, -1);

    listLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               kChild | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_NOSEL |
                                   LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL,
                               0, 0, 10, 10, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_LOG)),
                               hinst_, nullptr);

    applyFonts();
}

void MainWindow::applyFonts() {
    HWND all[] = {status_,     hint_,           lblName_,       editName_,      lblPort_,
                  editPort_,   lblVideo_,       cmbVideo_,      lblAudio_,      cmbAudio_,
                  chkFullscreen_, chkH265_,     chkDebug_,      chkAlwaysOnTop_, chkAutostart_,
                  chkEmbed_,   chkLogon_,      chkFrame_,
                  lblFps_,     cmbFps_,         lblDecoder_,    cmbDecoder_,
                  lblReset_,   cmbReset_,
                  btnToggle_,  btnCopy_,        btnSettings_,   btnBack_};
    for (HWND h : all)
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    for (HWND h : {hdrGeneral_, hdrConn_, hdrVideo_, hdrAudio_, hdrApp_, hdrLog_})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontHead_), TRUE);
    for (HWND h : {hdrTitle_, hdrReceiver_})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontTitle_), TRUE);
    if (btnToggle_) SendMessageW(btnToggle_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHead_), TRUE);
    if (listLog_) SendMessageW(listLog_, WM_SETFONT, reinterpret_cast<WPARAM>(fontMono_), TRUE);
}

void MainWindow::layout() {
    if (!hwnd_ || !listLog_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;
    const int m = s(16);
    const int contentW = W - 2 * m;

    auto place = [](HWND h, int x, int y, int w, int hh) {
        if (h) SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    int y = s(16);
    if (!settingsPage_) {
        // --- the receiver card: "AirPlay alıcısı" with Başlat/Durdur at the right, the state
        //     with its dot, what to do next (or who is connected) in up to two quieter lines ---
        const int pad = s(16);
        const int top = y;
        const int btnW = s(96);
        place(hdrReceiver_, m + pad, y + pad - s(1), contentW - 2 * pad - btnW - s(8), s(28));
        place(btnToggle_, W - m - pad - btnW, y + pad - s(2), btnW, s(32));
        const int stateY = y + pad + s(32);
        place(status_, m + pad + s(16), stateY, contentW - 2 * pad - s(16), s(18));
        place(hint_, m + pad, stateY + s(24), contentW - 2 * pad, s(36));
        statusCard_ = RECT{m, top, W - m, stateY + s(24) + s(36) + pad - s(4)};
        y = statusCard_.bottom + s(12);

        // --- three switches on one card, a hairline between rows (paintWindow) ---
        const int rowH = s(44);
        toggleCard_ = RECT{m, y, W - m, y + 3 * rowH};
        int ty = y;
        for (HWND h : {chkEmbed_, chkFrame_, chkAlwaysOnTop_}) {
            place(h, m + pad, ty + (rowH - s(30)) / 2, contentW - 2 * pad, s(30));
            ty += rowH;
        }
        y += 3 * rowH + s(8);

        // Settings is one quiet line under the cards, not a button on them.
        place(btnSettings_, W - m - s(96), y, s(96), s(28));
        y += s(28) + s(10);
        contentH_ = y;
    } else {
        statusCard_ = toggleCard_ = RECT{};
        place(btnBack_, m, y, s(90), s(30));
        place(hdrTitle_, m + s(104), y + s(3), contentW - s(104), s(26));
        y += s(30) + s(14);

        // Two columns so the page stays short enough for a laptop screen at 150 %.
        const int gap = s(28);
        const int colW = (contentW - gap) / 2;
        const int lblW = s(118);
        auto column = [&](int x, int top, auto&& body) {
            int cy = top;
            auto heading = [&](HWND h) {
                place(h, x, cy + s(6), colW, s(18));
                cy += s(30);
            };
            auto row = [&](HWND lbl, HWND ctl, int dropH) {
                place(lbl, x, cy + s(5), lblW, s(16));
                place(ctl, x + lblW + s(6), cy, colW - lblW - s(6), dropH);   // combos: + list
                cy += s(32);
            };
            auto sw = [&](HWND h) {
                place(h, x, cy, colW, s(28));
                cy += s(32);
            };
            body(heading, row, sw);
            return cy;
        };
        const int left = column(m, y, [&](auto heading, auto row, auto sw) {
            heading(hdrGeneral_);
            row(lblName_, editName_, s(24));
            heading(hdrConn_);
            row(lblPort_, editPort_, s(24));
            row(lblReset_, cmbReset_, s(220));
            heading(hdrAudio_);
            row(lblAudio_, cmbAudio_, s(220));
            (void) sw;
        });
        const int right = column(m + colW + gap, y, [&](auto heading, auto row, auto sw) {
            heading(hdrVideo_);
            row(lblFps_, cmbFps_, s(220));
            row(lblVideo_, cmbVideo_, s(220));
            row(lblDecoder_, cmbDecoder_, s(220));
            sw(chkFullscreen_);
            sw(chkH265_);
            heading(hdrApp_);
            sw(chkAutostart_);
            sw(chkLogon_);
            sw(chkDebug_);
        });
        y = (left > right ? left : right) + s(10);
        place(btnCopy_, m, y, s(150), s(30));
        y += s(30) + s(8);

        place(hdrLog_, m, y + s(6), contentW, s(18));
        y += s(30);
        const int minLog = s(140);
        int listH = H - y - m;
        if (listH < minLog) listH = minLog;
        place(listLog_, m, y, contentW, listH);
        contentH_ = y + minLog + m;
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::applyPageVisibility() {
    const int onMain = settingsPage_ ? SW_HIDE : SW_SHOW;
    const int onSettings = settingsPage_ ? SW_SHOW : SW_HIDE;
    for (HWND h : {status_, hint_, hdrReceiver_, btnToggle_, btnSettings_, chkEmbed_, chkFrame_,
                   chkAlwaysOnTop_})
        if (h) ShowWindow(h, onMain);
    for (HWND h : {btnBack_, hdrTitle_, hdrGeneral_, hdrConn_, hdrVideo_, hdrAudio_, hdrApp_,
                   hdrLog_, lblName_, editName_, lblPort_, editPort_, lblFps_, cmbFps_,
                   lblVideo_, cmbVideo_, lblDecoder_, cmbDecoder_, lblAudio_, cmbAudio_,
                   lblReset_, cmbReset_, chkFullscreen_, chkH265_, chkDebug_, chkAutostart_,
                   chkLogon_, btnCopy_, listLog_})
        if (h) ShowWindow(h, onSettings);
}

void MainWindow::applyTheme() {
    const Palette p = paletteFor(systemPrefersDark(), systemAccent());
    activePalette() = p;
    if (brWindow_) DeleteObject(brWindow_);
    if (brCard_) DeleteObject(brCard_);
    brWindow_ = CreateSolidBrush(p.window);
    brCard_   = CreateSolidBrush(p.card);
    useDarkTitleBar(hwnd_, p.dark);
    // The native fields keep their themed borders and arrows; in dark mode Windows' own
    // dark variants of those themes. An unknown theme name falls back to the default.
    const wchar_t* fieldTheme = p.dark ? L"DarkMode_CFD" : nullptr;
    for (HWND h : {editName_, editPort_, cmbVideo_, cmbAudio_, cmbFps_, cmbDecoder_, cmbReset_})
        if (h) SetWindowTheme(h, fieldTheme, nullptr);
    if (listLog_) SetWindowTheme(listLog_, p.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void MainWindow::paintWindow(HDC dc, const RECT& client) {
    const Palette& p = activePalette();
    FillRect(dc, &client, brWindow_ ? brWindow_ : GetSysColorBrush(COLOR_WINDOW));
    if (settingsPage_) return;

    const float radius = static_cast<float>(s(12));
    paintRoundRect(dc, statusCard_, radius, p.card, p.border, 1.0f);
    paintRoundRect(dc, toggleCard_, radius, p.card, p.border, 1.0f);
    // Hairlines between the toggle rows.
    const int rows = 3, rowH = (toggleCard_.bottom - toggleCard_.top) / rows;
    HBRUSH line = CreateSolidBrush(p.border);
    for (int i = 1; i < rows; ++i) {
        const RECT r{toggleCard_.left + s(16), toggleCard_.top + i * rowH,
                     toggleCard_.right - s(16), toggleCard_.top + i * rowH + 1};
        FillRect(dc, &r, line);
    }
    DeleteObject(line);
    // The state's dot, at the start of the state line.
    const float d = static_cast<float>(s(8));
    paintCircle(dc, statusCard_.left + s(16) + d / 2, statusCard_.top + s(16) + s(32) + s(9),
                d / 2, stateColor(state_));
}

// --- updates ---------------------------------------------------------------------------------
// Both workers own their job struct and the UpdateResult they post; the window procedure takes
// the result over and deletes it. Nothing here touches a HWND except through PostMessageW.

namespace {

struct UpdateJob {
    HWND         hwnd    = nullptr;
    bool         manual  = false;
    std::wstring url;        // download job only
    std::wstring version;    // download job only
};

DWORD WINAPI updateCheckThread(LPVOID p) {
    UpdateJob* job = static_cast<UpdateJob*>(p);
    auto* r = new UpdateResult();
    r->manual = job->manual;
    r->found  = checkForUpdate(r->info, &r->err);
    if (!PostMessageW(job->hwnd, MainWindow::WM_UPDATE_CHECKED, 0,
                      reinterpret_cast<LPARAM>(r))) {
        delete r;
    }
    delete job;
    return 0;
}

DWORD WINAPI updateDownloadThread(LPVOID p) {
    UpdateJob* job = static_cast<UpdateJob*>(p);
    auto* r = new UpdateResult();
    r->info.version = job->version;
    const std::wstring dest = installerDownloadPath(job->version);
    r->found = downloadInstaller(job->url, dest, &r->err, &r->integrityFailed);
    if (r->found) r->localPath = dest;
    if (!PostMessageW(job->hwnd, MainWindow::WM_UPDATE_DOWNLOADED, 0,
                      reinterpret_cast<LPARAM>(r))) {
        delete r;
    }
    delete job;
    return 0;
}

} // namespace

void MainWindow::startUpdateCheck(bool manual) {
    if (updateBusy_) return;
    if (manual) logUi(str::kUpdateChecking);

    auto* job = new UpdateJob{hwnd_, manual, {}, {}};
    HANDLE t = CreateThread(nullptr, 0, updateCheckThread, job, 0, nullptr);
    if (!t) { delete job; return; }
    CloseHandle(t);
    updateBusy_ = true;
}

void MainWindow::startUpdateDownload() {
    if (updateBusy_ || updateUrl_.empty()) return;
    logUi(str::kUpdateDownloading);

    auto* job = new UpdateJob{hwnd_, false, updateUrl_, updateVersion_};
    HANDLE t = CreateThread(nullptr, 0, updateDownloadThread, job, 0, nullptr);
    if (!t) { delete job; return; }
    CloseHandle(t);
    updateBusy_ = true;
}

void MainWindow::onUpdateChecked(UpdateResult* r) {
    updateBusy_ = false;

    if (!r->err.empty()) {
        logUi(std::wstring(str::kUpdateFailPre) + widen(r->err));
        if (r->manual) {
            MessageBoxW(hwnd_, (std::wstring(str::kUpdateFailPre) + widen(r->err)).c_str(),
                        str::kAppName, MB_ICONWARNING | MB_OK);
        }
        return;
    }
    if (!r->found) {
        logUi(L"update: already on the latest release (" AIRPLAY_VERSION_WSTR L")");
        if (r->manual) {
            MessageBoxW(hwnd_, str::kUpdateNone, str::kAppName, MB_ICONINFORMATION | MB_OK);
        }
        return;
    }

    if (r->info.skip != UpdateSkip::None) {
        // Found but not offered: a log line, never a prompt (docs/SPRINT0-SPEC.md B1, B2).
        logUi(std::wstring(str::kUpdateSkippedPre) + r->info.version + L" — " +
              (r->info.skip == UpdateSkip::NoChecksum ? str::kUpdateSkipNoChecksum
                                                      : str::kUpdateSkipUntrusted));
        return;
    }

    updateVersion_ = r->info.version;
    updateUrl_     = r->info.downloadUrl;
    updatePageUrl_ = r->info.pageUrl;
    logUi(L"update: " + updateVersion_ + L" is available");

    // A background find is easy to miss behind other windows; say it in the tray too.
    if (!r->manual) {
        tray_.showBalloon(str::kUpdateTitle, std::wstring(str::kUpdateFoundPre) + updateVersion_);
    }

    std::wstring msg = std::wstring(str::kUpdateFoundPre) + updateVersion_;
    if (!r->info.notes.empty()) msg += L"\n\n" + r->info.notes;

    if (updateUrl_.empty()) {
        // A release with no installer attached. Nothing to run, so offer the page instead.
        msg += L"\n\n";
        msg += str::kUpdateNoAsset;
        if (MessageBoxW(hwnd_, msg.c_str(), str::kUpdateTitle, MB_ICONINFORMATION | MB_YESNO)
                == IDYES && isAllowedUpdateUrl(updatePageUrl_)) {
            ShellExecuteW(hwnd_, L"open", updatePageUrl_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }

    msg += str::kUpdateAskTail;
    if (MessageBoxW(hwnd_, msg.c_str(), str::kUpdateTitle, MB_ICONQUESTION | MB_YESNO) == IDYES) {
        startUpdateDownload();
    }
}

void MainWindow::onUpdateDownloaded(UpdateResult* r) {
    updateBusy_ = false;

    if (!r->found && r->integrityFailed) {
        // The .part is already gone (updater.cpp); the detail goes to the log only.
        logUi(L"update: " + widen(r->err));
        logUi(str::kUpdateBadHash);
        MessageBoxW(hwnd_, str::kUpdateBadHash, str::kAppName, MB_ICONERROR | MB_OK);
        return;
    }
    if (!r->found) {
        logUi(std::wstring(str::kUpdateFailPre) + widen(r->err));
        MessageBoxW(hwnd_, (std::wstring(str::kUpdateFailPre) + widen(r->err)).c_str(),
                    str::kAppName, MB_ICONWARNING | MB_OK);
        return;
    }
    logUi(L"update: downloaded " + r->localPath);

    // The installer replaces uxplay.exe as well, so the child has to go first - and it is
    // ours to stop, not the installer's to kill. Synchronous: the installer must not start
    // while the old receiver is still running.
    doStop(true);

    // /SILENT: the user already answered the question, a wizard would only ask it again.
    // installer/airplay.iss starts us back up when it is done.
    const HINSTANCE rc = ShellExecuteW(hwnd_, nullptr, r->localPath.c_str(),
                                       L"/SILENT /SUPPRESSMSGBOXES /NOCANCEL",
                                       nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(rc) <= 32) {
        logUi(L"update: could not start the installer");
        MessageBoxW(hwnd_, str::kUpdateFailPre, str::kAppName, MB_ICONWARNING | MB_OK);
        return;
    }
    doExit();
}

int MainWindow::chromeWidth() const {
    RECT r{0, 0, 100, 100};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return (r.right - r.left) - 100;
}

int MainWindow::chromeHeight() const {
    RECT r{0, 0, 100, 100};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return (r.bottom - r.top) - 100;
}

// Each page has its own size: the main page is a narrow card stack, the settings page two
// columns and the log. The window's top-left corner stays where it is.
void MainWindow::resizeToContent() {
    if (!hwnd_ || IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    const int width = settingsPage_ ? s(720) : s(400);
    SetWindowPos(hwnd_, nullptr, 0, 0, width + chromeWidth(), contentH_ + chromeHeight(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);   // WM_SIZE lays out again
}

void MainWindow::controlsFromConfig() {
    SetWindowTextW(editName_, cfg_.name.c_str());
    wchar_t p[16];
    _snwprintf(p, 16, L"%d", cfg_.port);
    SetWindowTextW(editPort_, p);

    int idx = static_cast<int>(SendMessageW(cmbVideo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                            reinterpret_cast<LPARAM>(cfg_.videoSink.c_str())));
    SendMessageW(cmbVideo_, CB_SETCURSEL, static_cast<WPARAM>(idx == CB_ERR ? 0 : idx), 0);
    idx = static_cast<int>(SendMessageW(cmbAudio_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                        reinterpret_cast<LPARAM>(cfg_.audioSink.c_str())));
    SendMessageW(cmbAudio_, CB_SETCURSEL, static_cast<WPARAM>(idx == CB_ERR ? 0 : idx), 0);

    int fpsIdx = 0;
    for (int i = 0; i < static_cast<int>(ARRAYSIZE(kFpsValues)); ++i)
        if (kFpsValues[i] == cfg_.maxFps) fpsIdx = i;
    SendMessageW(cmbFps_, CB_SETCURSEL, static_cast<WPARAM>(fpsIdx), 0);

    int decIdx = 0;
    for (int i = 0; i < static_cast<int>(ARRAYSIZE(kDecoderValues)); ++i)
        if (cfg_.videoDecoder == kDecoderValues[i]) decIdx = i;
    SendMessageW(cmbDecoder_, CB_SETCURSEL, static_cast<WPARAM>(decIdx), 0);

    int resetIdx = 0;
    for (int i = 0; i < static_cast<int>(ARRAYSIZE(kResetValues)); ++i)
        if (kResetValues[i] == cfg_.resetSeconds) resetIdx = i;
    SendMessageW(cmbReset_, CB_SETCURSEL, static_cast<WPARAM>(resetIdx), 0);

    setChecked(chkFullscreen_, cfg_.fullscreen);
    setChecked(chkH265_, cfg_.h265);
    setChecked(chkDebug_, cfg_.debug);
    setChecked(chkAlwaysOnTop_, cfg_.alwaysOnTop);
    setChecked(chkAutostart_, cfg_.autostartReceiver);
    setChecked(chkEmbed_, cfg_.embedVideo);
    setChecked(chkFrame_, cfg_.deviceFrame);
    // Not a config.ini value: the registry (and the installer's Startup shortcut) own it.
    setChecked(chkLogon_, isLaunchAtLogon());
}

void MainWindow::configFromControls() {
    std::wstring name = getText(editName_);
    if (!name.empty()) cfg_.name = name;

    std::wstring port = getText(editPort_);
    long v = port.empty() ? 7100 : wcstol(port.c_str(), nullptr, 10);
    if (v < 0 || v > 65533) v = 7100;
    cfg_.port = static_cast<int>(v);

    int i = static_cast<int>(SendMessageW(cmbVideo_, CB_GETCURSEL, 0, 0));
    if (i >= 0 && i < static_cast<int>(ARRAYSIZE(kVideoSinks))) cfg_.videoSink = kVideoSinks[i];
    i = static_cast<int>(SendMessageW(cmbAudio_, CB_GETCURSEL, 0, 0));
    if (i >= 0 && i < static_cast<int>(ARRAYSIZE(kAudioSinks))) cfg_.audioSink = kAudioSinks[i];

    i = static_cast<int>(SendMessageW(cmbFps_, CB_GETCURSEL, 0, 0));
    if (i >= 0 && i < static_cast<int>(ARRAYSIZE(kFpsValues))) cfg_.maxFps = kFpsValues[i];
    i = static_cast<int>(SendMessageW(cmbDecoder_, CB_GETCURSEL, 0, 0));
    if (i >= 0 && i < static_cast<int>(ARRAYSIZE(kDecoderValues)))
        cfg_.videoDecoder = kDecoderValues[i];

    i = static_cast<int>(SendMessageW(cmbReset_, CB_GETCURSEL, 0, 0));
    if (i >= 0 && i < static_cast<int>(ARRAYSIZE(kResetValues)))
        cfg_.resetSeconds = kResetValues[i];

    cfg_.fullscreen        = isChecked(chkFullscreen_);
    cfg_.h265              = isChecked(chkH265_);
    cfg_.debug             = isChecked(chkDebug_);
    cfg_.alwaysOnTop       = isChecked(chkAlwaysOnTop_);
    cfg_.autostartReceiver = isChecked(chkAutostart_);
    cfg_.embedVideo        = isChecked(chkEmbed_);
    cfg_.deviceFrame       = isChecked(chkFrame_);
}

// ---------------------------------------------------------------------------

void MainWindow::logUi(const std::wstring& line) { log_.appendW(L"[gui] " + line); }

void MainWindow::updateStatus() {
    std::wstring text, hint;
    switch (state_) {
        case airplay::HostState::Stopped:
            text = str::kStateStopped;
            hint = str::kHintStopped;
            break;
        case airplay::HostState::Starting:
            text = str::kStateStarting;
            break;
        case airplay::HostState::Waiting:
            text = str::kStateWaiting;
            hint = std::wstring(str::kHintWaitingPre) + cfg_.name;
            if (!ipv4_.empty()) hint += L" (" + ipv4_ + L")";
            break;
        case airplay::HostState::Connected:
        {
            text = str::kStateConnected;
            // Who, in words: "iPhone 17 Pro Max", and the phone's own name when it is not just
            // "iPhone". Size, bitrate and frame rate stay in the log and the picture's title.
            const std::wstring& named = clientModel_.empty() ? std::wstring()
                                                             : deviceDisplayName(clientModel_);
            hint = !named.empty() ? named
                 : !clientName_.empty() ? clientName_
                 : !clientModel_.empty() ? clientModel_ : std::wstring(str::kHintUnknownClient);
            if (!named.empty() && !clientName_.empty() && clientName_ != L"iPhone" &&
                clientName_ != L"iPad")
                hint += L" · " + clientName_;
            hint += L" → " + cfg_.name;
            break;
        }
        case airplay::HostState::Stopping:
            text = str::kStateStopping;
            break;
        case airplay::HostState::Error:
            text = str::kStateError;
            hint = lastError_.empty() ? std::wstring(str::kHintUnknownError)
                                      : std::wstring(str::kHintErrorPrefix) + lastError_;
            break;
    }
    SetWindowTextW(status_, text.c_str());
    SetWindowTextW(hint_, hint.c_str());
    InvalidateRect(hwnd_, &statusCard_, FALSE);   // the dot

    const std::wstring title = std::wstring(str::kAppName) + str::kTitleSep + text;
    tray_.setTip(title);
    SetWindowTextW(hwnd_, title.c_str());
    updateVideoTitle();
}

void MainWindow::updateButtons() {
    const bool running = state_ != airplay::HostState::Stopped &&
                         state_ != airplay::HostState::Error;
    SetWindowTextW(btnToggle_, running ? str::kBtnStop : str::kBtnStart);
    // Start is the one accent action; Stop is a plain bordered button.
    LONG_PTR style = GetWindowLongPtrW(btnToggle_, GWL_STYLE);
    style = running ? (style | BS_FLAT) : (style & ~static_cast<LONG_PTR>(BS_FLAT));
    SetWindowLongPtrW(btnToggle_, GWL_STYLE, style);
    InvalidateRect(btnToggle_, nullptr, FALSE);
    // Starting/Stopping are transient: the button would act on a state that is about to
    // change, so it stays disabled until the child has settled.
    // The stop worker also counts: its Stopped event arrives before WM_HOST_STOPPED does.
    EnableWindow(btnToggle_, state_ != airplay::HostState::Starting &&
                                 state_ != airplay::HostState::Stopping && !stopThread_);
    // Receiver settings only take effect on (re)start - see DESIGN 6.1 limitations.
    const BOOL editable = running ? FALSE : TRUE;
    for (HWND h : {editName_, editPort_, cmbVideo_, cmbAudio_, cmbFps_, cmbDecoder_,
                   cmbReset_, chkFullscreen_, chkH265_, chkDebug_})
        EnableWindow(h, editable);
}

void MainWindow::applyAlwaysOnTop() {
    SetWindowPos(hwnd_, cfg_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // M1 could only do this to the control panel; the picture is ours now (PHASE2-M2-SPEC).
    video_.setAlwaysOnTop(cfg_.alwaysOnTop);
}

void MainWindow::saveWindowRect() {
    if (!hwnd_ || IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    RECT r{};
    if (!GetWindowRect(hwnd_, &r)) return;
    cfg_.x = r.left;
    cfg_.y = r.top;
    cfg_.w = r.right - r.left;
    cfg_.h = r.bottom - r.top;
}

// --- the picture (docs/PHASE2-M2-SPEC.md) ----------------------------------------------

void MainWindow::updateVideoTitle() {
    if (!video_.hasGuest()) return;
    std::wstring t = str::kAppName;
    if (!clientName_.empty()) t += std::wstring(str::kTitleSep) + clientName_;
    if (!resolutionText_.empty()) t += L" · " + resolutionText_;
    video_.setTitle(t);
}

void MainWindow::pollVideoWindow() {
    if (video_.hasGuest()) {
        if (video_.guestAlive()) return;
        // The receiver tore its window down: mirroring stopped, though the session may well
        // still be up. Hide ours and keep looking - the next stream gets adopted too.
        video_.release();
        resolutionText_.clear();
        logUi(L"video: the receiver closed its window");
        updateStatus();
        armEmbedTimer(true);   // back to hunting: the next stream gets its own window
        return;
    }
    if (!cfg_.embedVideo || embedSuspended_ || !host_.isRunning()) return;

    HWND guest = findReceiverVideoWindow(host_.pid());
    if (!guest) return;
    if (!video_.adopt(guest)) {
        // Do not fight it every 300 ms; the picture stays in the receiver own window.
        embedSuspended_ = true;
        logUi(L"video: the receiver window would not move, leaving it on the desktop");
        return;
    }

    // The sink sizes its window to the video, so this is the source resolution - and it
    // arrives without -d, which in M1 was the only way to learn it at all.
    const SIZE src = video_.sourceSize();
    wchar_t b[64];
    _snwprintf(b, 64, L"%d×%d", static_cast<int>(src.cx), static_cast<int>(src.cy));
    resolutionText_ = b;
    wchar_t note[96];
    _snwprintf(note, 96, L"video: adopted the receiver window (%dx%d)",
               static_cast<int>(src.cx), static_cast<int>(src.cy));
    logUi(note);
    updateStatus();
    updateVideoTitle();
    armEmbedTimer(false);   // nothing left to hunt for; just keep an eye on it
}

void MainWindow::applyEmbedSetting() {
    if (cfg_.embedVideo) {
        embedSuspended_ = false;
        // Ticking the box is the second way back from a closed picture window.
        video_.showPicture();
        if (host_.isRunning()) armEmbedTimer(!video_.hasGuest());
    } else {
        // The one caller that means it: the user asked for the picture in the receiver's
        // own window, so that window has to actually come back.
        releaseVideo(true);
    }
}

void MainWindow::armEmbedTimer(bool fast) {
    if (!hwnd_) return;
    SetTimer(hwnd_, kEmbedTimer, fast ? kEmbedPollFastMs : kEmbedPollIdleMs, nullptr);
}

void MainWindow::releaseVideo(bool toDesktop) {
    if (hwnd_) KillTimer(hwnd_, kEmbedTimer);
    video_.release(toDesktop);
}

// ---------------------------------------------------------------------------

void MainWindow::startReceiver() { doStart(); }

void MainWindow::doStart() {
    // A stop still in flight: the button is grey for that reason, the tray may not know it.
    if (stopThread_ || host_.isRunning()) return;

    configFromControls();
    if (cfg_.uxplayPath.empty() || !fileExists(cfg_.uxplayPath))
        cfg_.uxplayPath = defaultUxplayPath();
    saveWindowRect();
    store_.save(cfg_);

    if (cfg_.uxplayPath.empty()) {
        lastError_ = str::kErrNoUxplay;
        state_ = airplay::HostState::Error;
        updateStatus();
        updateButtons();
        logUi(lastError_);
        MessageBoxW(hwnd_, lastError_.c_str(), str::kAppName, MB_ICONERROR | MB_OK);
        return;
    }

    // A receiver we do not own would keep listening on the same port (SO_REUSEADDR) and the
    // phone could connect to it instead of to our child, leaving the GUI blind.
    if (const int stale = killStaleReceivers(cfg_.uxplayPath)) {
        wchar_t note[96];
        _snwprintf(note, 96, L"terminated %d stale uxplay.exe before starting", stale);
        logUi(note);
    }

    airplay::HostConfig hc = ConfigStore::toHostConfig(cfg_);
    ensureDir(hc.homeDir);
    ensureDir(localAppDir());

    resolutionText_.clear();
    clientName_.clear();
    clientModel_.clear();
    lastError_.clear();
    ipv4_ = firstLocalIPv4();

    logUi(L"start: " + cfg_.uxplayPath);

    std::string err;
    if (!host_.start(hc, &err)) {
        lastError_ = err.empty() ? std::wstring(str::kErrStartFailed) : widen(err);
        state_ = airplay::HostState::Error;
        updateStatus();
        updateButtons();
        logUi(lastError_);
        MessageBoxW(hwnd_, lastError_.c_str(), str::kAppName, MB_ICONERROR | MB_OK);
        return;
    }

    embedSuspended_ = false;
    if (cfg_.embedVideo) armEmbedTimer(true);

    state_ = host_.state();
    updateStatus();
    updateButtons();
}

namespace {

struct StopJob {
    HWND                 hwnd = nullptr;
    airplay::UxplayHost* host = nullptr;
};

// stop() waits up to its grace period and then for the reader thread; none of that may
// happen on the UI thread. Touches no HWND except through PostMessageW.
DWORD WINAPI hostStopThread(LPVOID p) {
    StopJob* job = static_cast<StopJob*>(p);
    job->host->stop();
    PostMessageW(job->hwnd, MainWindow::WM_HOST_STOPPED, 0, 0);
    delete job;
    return 0;
}

} // namespace

void MainWindow::doStop(bool synchronous) {
    if (stopThread_) {
        // Already stopping. A caller that has to see the child gone waits for it: stop()
        // takes the host's lifecycle lock, so it returns only once the worker's stop() has.
        if (synchronous) {
            host_.stop();
            onStopFinished();
        }
        return;
    }
    if (!host_.isRunning()) return;
    logUi(L"stop requested");
    state_ = airplay::HostState::Stopping;
    updateStatus();
    updateButtons();
    // Before the child dies: its window is a child of ours, and Windows would destroy it
    // along with ours if the order were the other way round.
    releaseVideo();

    if (!synchronous) {
        auto* job = new StopJob{hwnd_, &host_};
        HANDLE t = CreateThread(nullptr, 0, hostStopThread, job, 0, nullptr);
        if (t) {
            stopThread_ = t;     // WM_HOST_STOPPED -> onStopFinished() takes it from here
            updateButtons();
            return;
        }
        delete job;              // no thread: stop right here rather than not at all
    }
    host_.stop();
    state_ = host_.state();
    updateStatus();
    updateButtons();
}

void MainWindow::joinStopThread() {
    if (!stopThread_) return;
    // The worker only has PostMessageW left to do once stop() has returned, so this is short.
    WaitForSingleObject(stopThread_, INFINITE);
    CloseHandle(stopThread_);
    stopThread_ = nullptr;
}

void MainWindow::onStopFinished() {
    joinStopThread();
    state_ = host_.state();
    updateStatus();
    updateButtons();
}

void MainWindow::doCopyCmdline() {
    configFromControls();
    airplay::HostConfig hc = ConfigStore::toHostConfig(cfg_);
    std::vector<std::wstring> args = airplay::UxplayHost::buildArgs(hc);

    std::wstring line;
    // buildArgs may or may not include argv[0]; only prepend the exe when it does not.
    if (args.empty() || args.front() != hc.uxplayExe) line = quoteArg(hc.uxplayExe);
    for (const std::wstring& a : args) {
        if (!line.empty()) line.push_back(L' ');
        line += quoteArg(a);
    }

    if (copyToClipboard(hwnd_, line))
        logUi(L"copied to clipboard: " + line);
    else
        logUi(L"clipboard copy failed: " + line);
}

void MainWindow::doExit() {
    exiting_ = true;
    configFromControls();
    saveWindowRect();
    releaseVideo();      // also writes the picture window position into the config
    store_.save(cfg_);
    host_.setCallback(nullptr);
    host_.stop();
    tray_.remove();
    DestroyWindow(hwnd_);
}

// ---------------------------------------------------------------------------

void MainWindow::onHostEvent(const airplay::HostEvent& ev) {
    using K = airplay::HostEventKind;
    switch (ev.kind) {
        case K::LogLine:
            log_.append(ev.message);
            break;

        case K::StateChanged: {
            state_ = ev.state;
            if (state_ == airplay::HostState::Error && !ev.message.empty())
                lastError_ = widen(ev.message);
            // The cause is in the hint (two lines on the status card) and in the log on the
            // settings page; the page is not switched under the user.
            if (state_ != airplay::HostState::Connected) {
                currentFps_  = 0;
                currentKbps_ = 0;
            }
            if (state_ == airplay::HostState::Waiting) {
                ipv4_ = firstLocalIPv4();
                clientName_.clear();
                clientModel_.clear();
            }
            if (state_ == airplay::HostState::Stopped ||
                state_ == airplay::HostState::Error) {
                clientName_.clear();
                clientModel_.clear();
                resolutionText_.clear();
                releaseVideo();
            }
            updateStatus();
            updateButtons();
            break;
        }

        case K::ClientInfo: {
            clientName_  = widen(ev.clientName);
            clientModel_ = widen(ev.clientModel);
            // The model identifier is what picks the frame - and its notch. It arrives
            // before the video window exists, which is the right order: by the time there
            // is a picture to frame, the phone is known.
            video_.setDevice(clientModel_);
            const std::wstring& named = deviceDisplayName(clientModel_);
            logUi(named.empty() ? L"client model: " + clientModel_ + L" (not in the frame table)"
                                : L"client model: " + clientModel_ + L" = " + named);
            updateStatus();
            break;
        }

        case K::Ports: {
            wchar_t b[160];
            _snwprintf(b, 160, L"ports: UDP %d %d %d / TCP %d %d %d", ev.udpPorts[0],
                       ev.udpPorts[1], ev.udpPorts[2], ev.tcpPorts[0], ev.tcpPorts[1],
                       ev.tcpPorts[2]);
            logUi(b);
            break;
        }

        case K::Resolution: {
            // Sent at the start of a stream and whenever the client changes its format
            // (patches/0007); the picture is re-laid out to the new aspect. Also a suffix on
            // the hint line.
            video_.setSourceSize(SIZE{ev.width, ev.height});
            wchar_t b[64];
            _snwprintf(b, 64, L"%d×%d", ev.width, ev.height);
            resolutionText_ = b;
            wchar_t note[160];
            _snwprintf(note, 160, L"resolution: %dx%d (source) -> %dx%d", ev.srcWidth,
                       ev.srcHeight, ev.width, ev.height);
            logUi(note);
            updateStatus();
            break;
        }

        case K::Pin:
            logUi(L"pairing PIN: " + widen(ev.message));
            MessageBoxW(hwnd_, (std::wstring(str::kPinTitle) + L": " + widen(ev.message)).c_str(),
                        str::kAppName, MB_ICONINFORMATION | MB_OK);
            break;

        // The receiver reports the video stream going quiet and coming back (patches/0004).
        // Nothing acts on it: a phone with its screen off is still a live session, and the
        // window stays exactly as it is until the user stops the receiver.
        case K::MirrorActivity:
            break;

        // The client's own once-a-second -FPSdata report, used only for the fps readout.
        // It reads 0 while the screen is off; keep the last real figure rather than blinking
        // the status line between "60 fps" and nothing.
        // What the receiver itself measured (patches/0005): the bitrate, and a frame rate
        // that does not depend on the client having been asked for one.
        case K::MirrorStats:
            currentKbps_ = ev.kbps;
            if (ev.fps > 0) currentFps_ = ev.fps;
            updateStatus();
            break;

        case K::MirrorFps:
            if (ev.srcWidth > 0) {
                currentFps_ = ev.srcWidth;
                updateStatus();
            }
            break;

        case K::Warning:
            logUi(L"WARNING: " + widen(ev.message));
            break;

        case K::Error:
            lastError_ = widen(ev.message);
            logUi(L"ERROR: " + lastError_);
            if (state_ == airplay::HostState::Error) updateStatus();
            break;
    }
}

void MainWindow::onCommand(int id, int code) {
    switch (id) {
        case IDC_BTN_TOGGLE:
            if (host_.isRunning())
                doStop();
            else
                doStart();
            break;
        case IDC_BTN_COPY:
            doCopyCmdline();
            break;
        case IDC_BTN_SETTINGS:
        case IDC_BTN_BACK:
            settingsPage_ = id == IDC_BTN_SETTINGS;
            applyPageVisibility();
            layout();
            resizeToContent();
            SetFocus(settingsPage_ ? btnBack_ : btnSettings_);
            break;
        case IDC_CHK_ALWAYSONTOP:
            cfg_.alwaysOnTop = isChecked(chkAlwaysOnTop_);
            applyAlwaysOnTop();
            store_.save(cfg_);
            break;
        case IDC_CHK_AUTOSTART:
            cfg_.autostartReceiver = isChecked(chkAutostart_);
            store_.save(cfg_);
            break;
        case IDC_CHK_EMBED:
            cfg_.embedVideo = isChecked(chkEmbed_);
            applyEmbedSetting();
            store_.save(cfg_);
            break;
        case IDC_CHK_FRAME:
            // setFrameEnabled owns cfg_.deviceFrame and re-lays out the picture at once.
            video_.setFrameEnabled(isChecked(chkFrame_));
            store_.save(cfg_);
            break;
        case IDC_CHK_LOGON: {
            const bool want = isChecked(chkLogon_);
            const bool ok   = setLaunchAtLogon(want);
            // Show what actually happened, not what was asked for.
            setChecked(chkLogon_, isLaunchAtLogon());
            logUi(ok ? (want ? L"logon: enabled" : L"logon: disabled")
                     : L"logon: could not write the HKCU Run value");
            break;
        }
        case kTrayShow:
            ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(hwnd_);
            break;
        case kTrayPicture:
            video_.showPicture();
            updateStatus();
            break;
        case kTrayStart:
            doStart();
            break;
        case kTrayStop:
            doStop();
            break;
        case kTrayExit:
            doExit();
            break;
        case kTrayUpdate:
            startUpdateCheck(true);
            break;
        default:
            (void)code;
            break;
    }
}

// ---------------------------------------------------------------------------

LRESULT MainWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated_ && taskbarCreated_ != 0) {
        tray_.readd();
        updateStatus();
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            dpi_ = static_cast<int>(dpiForWindow(hwnd_));
            createFonts();
            createControls();

            log_.open();
            log_.setListBox(listLog_);
            logUi(L"airplay " AIRPLAY_VERSION_WSTR);
            logUi(L"config: " + store_.path());
            logUi(L"log: " + log_.path());
            logUi(cfg_.uxplayPath.empty() ? std::wstring(L"uxplay.exe: NOT FOUND")
                                          : L"uxplay.exe: " + cfg_.uxplayPath);
            // Which ucrt64\ tree the child will load its DLLs and GStreamer plugins from -
            // the first thing to look at when a copied-to-another-machine install misbehaves.
            logUi(cfg_.msysRoot.empty() ? std::wstring(L"runtime: NOT FOUND")
                                        : L"runtime: " + cfg_.msysRoot);

            controlsFromConfig();
            applyTheme();
            applyPageVisibility();

            if (cfg_.w < 200 || cfg_.h < 200)
                SetWindowPos(hwnd_, nullptr, 0, 0, s(500), s(300),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            resizeToContent();

            tray_.add(hwnd_, WM_TRAY);

            // Not from here: the check can come back before the window is even on screen, and
            // its answer is a message box. A few seconds in, the user has seen the app first.
            if (cfg_.autoUpdate) SetTimer(hwnd_, kUpdateTimer, 4000, nullptr);

            // The reader thread is not allowed to touch HWNDs; hand the event over by post.
            HWND target = hwnd_;
            host_.setCallback([target](const airplay::HostEvent& e) {
                auto* copy = new airplay::HostEvent(e);
                if (!PostMessageW(target, WM_HOST_EVENT, 0, reinterpret_cast<LPARAM>(copy)))
                    delete copy;
            });

            video_.setOnClosed([this]() {
                // The guest stays adopted and hidden with us, so nothing reappears on the
                // desktop and pollVideoWindow() has no loose window to adopt again. Say
                // where it went - once - the session is up and still making sound.
                logUi(L"video: picture hidden, the session keeps running");
                if (!cfg_.pictureHintShown) {
                    tray_.showBalloon(str::kPictureHintTitle, str::kPictureHintText);
                    cfg_.pictureHintShown = true;
                }
                updateStatus();
            });

            applyAlwaysOnTop();
            updateStatus();
            updateButtons();
            return 0;
        }

        case WM_TIMER:
            // One shot: the startup update check, once the window is up.
            if (wp == kUpdateTimer) {
                KillTimer(hwnd_, kUpdateTimer);
                startUpdateCheck(false);
            } else if (wp == kEmbedTimer) {
                pollVideoWindow();
            }
            return 0;

        case WM_UPDATE_CHECKED: {
            auto* r = reinterpret_cast<UpdateResult*>(lp);
            if (r) { onUpdateChecked(r); delete r; }
            return 0;
        }

        case WM_UPDATE_DOWNLOADED: {
            auto* r = reinterpret_cast<UpdateResult*>(lp);
            if (r) { onUpdateDownloaded(r); delete r; }
            return 0;
        }

        case WM_HOST_EVENT: {
            auto* ev = reinterpret_cast<airplay::HostEvent*>(lp);
            if (ev) {
                onHostEvent(*ev);
                delete ev;
            }
            return 0;
        }

        case WM_HOST_STOPPED:
            // Posted after every event of that stop, so the state read here is final.
            onStopFinished();
            return 0;

        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: {
                    int cmd = tray_.trackMenu(hwnd_, host_.isRunning(), video_.isHidden());
                    if (cmd != kTrayNone) onCommand(cmd, 0);
                    break;
                }
                case WM_LBUTTONDBLCLK:
                    onCommand(kTrayShow, 0);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp));
            return 0;

        // Double-buffered so a resize or a page switch does not flicker through white.
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            paintWindow(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        // Labels and the status lines: on the card (main page) or on the window (settings).
        case WM_CTLCOLORSTATIC: {
            const Palette& p = activePalette();
            auto dc = reinterpret_cast<HDC>(wp);
            auto ctl = reinterpret_cast<HWND>(lp);
            SetBkMode(dc, TRANSPARENT);
            const bool onCard = ctl == status_ || ctl == hint_ || ctl == hdrReceiver_;
            SetTextColor(dc, ctl == hint_ ? p.textMuted : p.text);
            // Read-only / disabled edits come here too: give them the field colour.
            if (ctl == editName_ || ctl == editPort_) {
                SetBkColor(dc, p.card);
                SetTextColor(dc, p.textMuted);
                return reinterpret_cast<LRESULT>(brCard_);
            }
            return reinterpret_cast<LRESULT>(onCard ? brCard_ : brWindow_);
        }

        // The custom controls ask for the surface they sit on.
        case WM_CTLCOLORBTN: {
            auto ctl = reinterpret_cast<HWND>(lp);
            const bool onCard = !settingsPage_ && (ctl == chkEmbed_ || ctl == chkFrame_ ||
                                                   ctl == chkAlwaysOnTop_ || ctl == btnToggle_);
            return reinterpret_cast<LRESULT>(onCard ? brCard_ : brWindow_);
        }

        // Fields and the log: the card colour, readable in both themes.
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const Palette& p = activePalette();
            auto dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, p.text);
            SetBkColor(dc, p.card);
            return reinterpret_cast<LRESULT>(brCard_);
        }

        // Windows switched between light and dark app mode.
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
                applyTheme();
            break;

        // ...or the user picked another accent colour.
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            applyTheme();
            break;

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) layout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = (settingsPage_ ? s(680) : s(360)) + chromeWidth();
            // contentH_ is 0 until the first layout(); s(300) covers the main page.
            mmi->ptMinTrackSize.y = (contentH_ > 0 ? contentH_ : s(300)) + chromeHeight();
            return 0;
        }

        case WM_DPICHANGED: {
            dpi_ = static_cast<int>(HIWORD(wp));
            createFonts();
            applyFonts();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }

        case WM_CLOSE:
            // The close button hides to the tray; Exit in the tray menu really quits.
            if (!exiting_) {
                saveWindowRect();
                // Hiding a window the user just closed looks like a crash unless we say
                // where it went. Once is enough.
                if (!cfg_.trayHintShown) {
                    tray_.showBalloon(str::kTrayHintTitle, str::kTrayHintText);
                    cfg_.trayHintShown = true;
                }
                store_.save(cfg_);
                ShowWindow(hwnd_, SW_HIDE);
                return 0;
            }
            DestroyWindow(hwnd_);
            return 0;

        case WM_ENDSESSION:
            if (wp) {
                // The session ends without doExit() ever running (typically from the tray), so
                // save what it would have: the window rects, the picture position and the
                // once-only hint flags. Before stop(), which may be slow, and the OS may not wait.
                host_.setCallback(nullptr);
                configFromControls();
                saveWindowRect();
                releaseVideo();      // also writes the picture window position into the config
                store_.save(cfg_);
                host_.stop();
                joinStopThread();
            }
            return 0;

        case WM_DESTROY:
            host_.setCallback(nullptr);
            releaseVideo();
            host_.stop();          // waits for a stop worker still inside stop()
            joinStopThread();
            tray_.remove();
            log_.setListBox(nullptr);
            log_.close();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace ui
