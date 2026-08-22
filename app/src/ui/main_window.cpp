#include "main_window.h"

#include <commctrl.h>
#include <ipifcons.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <vector>

#include "../res/resource.h"
#include "autostart.h"
#include "single_instance.h"
#include "stale_receivers.h"
#include "strings.h"
#include "updater.h"
#include "version.h"
#include "video_embed.h"

namespace ui {

// What an updater worker thread hands back to the window. Allocated by the thread, deleted
// by the window procedure - the same ownership rule as WM_HOST_EVENT.
struct UpdateResult {
    UpdateInfo   info;
    std::string  err;          // UTF-8, empty on success
    bool         found = false;   // a newer release exists
    bool         manual = false;  // the user asked, so "up to date" is worth saying out loud
    std::wstring localPath;    // WM_UPDATE_DOWNLOADED: the installer on disk
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
    IDC_BTN_TOGGLE,
    IDC_BTN_COPY,
    IDC_SEC_ADVANCED,
    IDC_SEC_DETAILS,
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

UINT dpiForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
        auto fn = reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(u32, "GetDpiForWindow")));
        if (fn) {
            UINT d = fn(hwnd);
            if (d >= 72) return d;
        }
    }
    HDC dc = GetDC(nullptr);
    UINT d = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
    if (dc) ReleaseDC(nullptr, dc);
    return d ? d : 96u;
}

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
    if (fontUi_) DeleteObject(fontUi_);
    if (fontStatus_) DeleteObject(fontStatus_);
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    int x = cfg_.x, y = cfg_.y, w = cfg_.w, h = cfg_.h;
    if (w < 200 || h < 200) {
        x = y = CW_USEDEFAULT;
        w = h = CW_USEDEFAULT;
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
    if (fontUi_) DeleteObject(fontUi_);
    if (fontStatus_) DeleteObject(fontStatus_);

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

    LOGFONTW big = lf;
    big.lfHeight = -MulDiv(14, dpi_, 72);
    big.lfWeight = FW_BOLD;
    fontStatus_ = CreateFontIndirectW(&big);
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
    hint_   = mk(L"STATIC", str::kHintStopped, SS_LEFT | SS_ENDELLIPSIS, IDC_HINT);

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

    chkFullscreen_  = mk(L"BUTTON", str::kChkFullscreen, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_FULLSCREEN);
    chkH265_        = mk(L"BUTTON", str::kChkH265, BS_AUTOCHECKBOX | WS_TABSTOP, IDC_CHK_H265);
    chkDebug_       = mk(L"BUTTON", str::kChkDebug, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_DEBUG);
    chkAlwaysOnTop_ = mk(L"BUTTON", str::kChkAlwaysOnTop, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_ALWAYSONTOP);
    chkAutostart_   = mk(L"BUTTON", str::kChkAutostart, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_AUTOSTART);
    chkEmbed_       = mk(L"BUTTON", str::kChkEmbed, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_EMBED);
    chkLogon_       = mk(L"BUTTON", str::kChkLogon, BS_AUTOCHECKBOX | WS_TABSTOP,
                         IDC_CHK_LOGON);

    btnToggle_ = mk(L"BUTTON", str::kBtnStart, BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_BTN_TOGGLE);
    btnCopy_   = mk(L"BUTTON", str::kBtnCopyCmd, BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_COPY);

    // Flat, left-aligned buttons so they read as section headers rather than actions.
    secAdvanced_ = mk(L"BUTTON", str::kSecAdvanced, BS_PUSHBUTTON | BS_FLAT | BS_LEFT |
                                                        WS_TABSTOP,
                      IDC_SEC_ADVANCED);
    secDetails_  = mk(L"BUTTON", str::kSecDetails, BS_PUSHBUTTON | BS_FLAT | BS_LEFT |
                                                       WS_TABSTOP,
                      IDC_SEC_DETAILS);

    listLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               kChild | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_NOSEL |
                                   LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL,
                               0, 0, 10, 10, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_LOG)),
                               hinst_, nullptr);

    HWND all[] = {status_,     hint_,           lblName_,       editName_,      lblPort_,
                  editPort_,   lblVideo_,       cmbVideo_,      lblAudio_,      cmbAudio_,
                  chkFullscreen_, chkH265_,     chkDebug_,      chkAlwaysOnTop_, chkAutostart_,
                  chkEmbed_,   chkLogon_,
                  lblFps_,     cmbFps_,         lblDecoder_,    cmbDecoder_,
                  lblReset_,   cmbReset_,
                  btnToggle_,  btnCopy_,        secAdvanced_,   secDetails_,    listLog_};
    for (HWND h : all)
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    if (status_) SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(fontStatus_), TRUE);
}

void MainWindow::layout() {
    if (!hwnd_ || !listLog_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;
    const int m = s(14);
    const int contentW = W - 2 * m;

    auto place = [](HWND h, int x, int y, int w, int hh) {
        if (h) SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    // Single top-to-bottom flow: what the user needs first is highest up, and a collapsed
    // group simply does not advance y, so nothing above it moves.
    int y = s(12);
    const int textX = m + s(22);        // leaves room for the status dot
    const int textW = contentW - s(22);
    place(status_, textX, y, textW, s(28));
    y += s(30);
    place(hint_, textX, y, textW, s(18));
    y += s(32);

    place(lblName_, m, y + s(5), s(132), s(16));
    place(editName_, m + s(136), y, s(180), s(24));
    y += s(38);

    place(btnToggle_, m, y, s(160), s(34));
    y += s(48);

    // --- advanced (collapsible) ---
    place(secAdvanced_, m, y, s(160), s(22));
    y += s(26);
    if (showAdvanced_) {
        place(lblPort_, m, y + s(5), s(36), s(16));
        place(editPort_, m + s(40), y, s(70), s(24));
        place(lblFps_, m + s(126), y + s(5), s(60), s(16));
        place(cmbFps_, m + s(188), y, s(150), s(220));
        y += s(32);
        place(lblVideo_, m, y + s(5), s(88), s(16));
        place(cmbVideo_, m + s(92), y, s(170), s(220));
        y += s(30);
        place(lblDecoder_, m, y + s(5), s(88), s(16));
        place(cmbDecoder_, m + s(92), y, s(190), s(220));
        y += s(30);
        place(lblAudio_, m, y + s(5), s(88), s(16));
        place(cmbAudio_, m + s(92), y, s(170), s(220));
        y += s(30);
        place(lblReset_, m, y + s(5), s(120), s(16));
        place(cmbReset_, m + s(124), y, s(220), s(220));
        y += s(32);
        place(chkFullscreen_, m, y, s(88), s(20));
        place(chkH265_, m + s(92), y, s(62), s(20));
        place(chkDebug_, m + s(158), y, s(122), s(20));
        y += s(24);
        place(chkAlwaysOnTop_, m, y, s(118), s(20));
        place(chkAutostart_, m + s(124), y, s(170), s(20));
        y += s(24);
        place(chkEmbed_, m, y, s(210), s(20));
        place(chkLogon_, m + s(216), y, s(210), s(20));
        y += s(28);
        place(btnCopy_, m, y, s(140), s(28));
        y += s(36);
    }

    // --- details / log (collapsible) ---
    place(secDetails_, m, y, s(160), s(22));
    y += s(26);
    logTop_ = y;
    if (showDetails_) {
        int listH = H - y - m;
        if (listH < s(60)) listH = s(60);
        place(listLog_, m, y, contentW, listH);
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::applySectionVisibility() {
    const int adv = showAdvanced_ ? SW_SHOW : SW_HIDE;
    for (HWND h : {lblPort_, editPort_, lblFps_, cmbFps_, lblVideo_, cmbVideo_, lblDecoder_,
                   cmbDecoder_, lblAudio_, cmbAudio_, lblReset_, cmbReset_, chkFullscreen_,
                   chkH265_, chkDebug_, chkAlwaysOnTop_, chkAutostart_, chkEmbed_,
                   chkLogon_, btnCopy_})
        if (h) ShowWindow(h, adv);
    if (listLog_) ShowWindow(listLog_, showDetails_ ? SW_SHOW : SW_HIDE);

    if (secAdvanced_)
        SetWindowTextW(secAdvanced_,
                       (std::wstring(showAdvanced_ ? str::kSecOpen : str::kSecClosed) +
                        str::kSecAdvanced).c_str());
    if (secDetails_)
        SetWindowTextW(secDetails_,
                       (std::wstring(showDetails_ ? str::kSecOpen : str::kSecClosed) +
                        str::kSecDetails).c_str());
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
    r->found = downloadFile(job->url, dest, &r->err);
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
                == IDYES && !updatePageUrl_.empty()) {
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

    if (!r->found) {
        logUi(std::wstring(str::kUpdateFailPre) + widen(r->err));
        MessageBoxW(hwnd_, (std::wstring(str::kUpdateFailPre) + widen(r->err)).c_str(),
                    str::kAppName, MB_ICONWARNING | MB_OK);
        return;
    }
    logUi(L"update: downloaded " + r->localPath);

    // The installer replaces uxplay.exe as well, so the child has to go first - and it is
    // ours to stop, not the installer's to kill.
    doStop();

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

int MainWindow::chromeHeight() const {
    RECT r{0, 0, 100, 100};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return (r.bottom - r.top) - 100;
}

// Grow/shrink the window so the visible sections fit exactly. The user's width is kept:
// only the height follows the sections.
void MainWindow::resizeToContent() {
    if (!hwnd_ || IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    RECT wr{};
    if (!GetWindowRect(hwnd_, &wr)) return;
    const int client = logTop_ + (showDetails_ ? s(170) : 0) + s(14);
    // The hint line is the widest thing in the window; a narrower window would ellipsize
    // the one sentence that tells the user what to do.
    int width = wr.right - wr.left;
    if (width < s(500)) width = s(500);
    SetWindowPos(hwnd_, nullptr, 0, 0, width, client + chromeHeight(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
            text = str::kStateConnected;
            hint = clientName_.empty() ? std::wstring(str::kHintUnknownClient) : clientName_;
            if (!clientModel_.empty()) hint += L" (" + clientModel_ + L")";
            if (!resolutionText_.empty()) hint += L" · " + resolutionText_;
            if (currentKbps_ > 0) {
                wchar_t br[32];
                if (currentKbps_ >= 1000)
                    _snwprintf(br, 32, L" · %.1f Mbps", currentKbps_ / 1000.0);
                else
                    _snwprintf(br, 32, L" · %d kbps", currentKbps_);
                hint += br;
            }
            if (currentFps_ > 0) {
                wchar_t fps[32];
                _snwprintf(fps, 32, L" · %d fps", currentFps_);
                hint += fps;
            }
            break;
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
    RECT dot{0, 0, s(40), s(48)};
    InvalidateRect(hwnd_, &dot, TRUE);

    const std::wstring title = std::wstring(str::kAppName) + str::kTitleSep + text;
    tray_.setTip(title);
    SetWindowTextW(hwnd_, title.c_str());
    updateVideoTitle();
}

void MainWindow::updateButtons() {
    const bool running = state_ != airplay::HostState::Stopped &&
                         state_ != airplay::HostState::Error;
    SetWindowTextW(btnToggle_, running ? str::kBtnStop : str::kBtnStart);
    // Starting/Stopping are transient: the button would act on a state that is about to
    // change, so it stays disabled until the child has settled.
    EnableWindow(btnToggle_, state_ != airplay::HostState::Starting &&
                                 state_ != airplay::HostState::Stopping);
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
}

void MainWindow::applyEmbedSetting() {
    if (cfg_.embedVideo) {
        embedSuspended_ = false;
        if (host_.isRunning()) SetTimer(hwnd_, kEmbedTimer, 300, nullptr);
    } else {
        releaseVideo();
    }
}

void MainWindow::releaseVideo() {
    if (hwnd_) KillTimer(hwnd_, kEmbedTimer);
    video_.release();
}

// ---------------------------------------------------------------------------

void MainWindow::startReceiver() { doStart(); }

void MainWindow::doStart() {
    if (host_.isRunning()) return;

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
    if (cfg_.embedVideo) SetTimer(hwnd_, kEmbedTimer, 300, nullptr);

    state_ = host_.state();
    updateStatus();
    updateButtons();
}

void MainWindow::doStop() {
    if (!host_.isRunning()) return;
    logUi(L"stop requested");
    state_ = airplay::HostState::Stopping;
    updateStatus();
    updateButtons();
    // Before the child dies: its window is a child of ours, and Windows would destroy it
    // along with ours if the order were the other way round.
    releaseVideo();
    host_.stop();
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
            // The cause is a log line the user cannot see while Details is collapsed, and a
            // one-line hint always truncates it. Open the section for them - only for this
            // session, so it is not written back to config.ini as a preference.
            if (state_ == airplay::HostState::Error && !showDetails_) {
                showDetails_ = true;
                applySectionVisibility();
                layout();
                resizeToContent();
            }
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

        case K::ClientInfo:
            clientName_  = widen(ev.clientName);
            clientModel_ = widen(ev.clientModel);
            updateStatus();
            break;

        case K::Ports: {
            wchar_t b[160];
            _snwprintf(b, 160, L"ports: UDP %d %d %d / TCP %d %d %d", ev.udpPorts[0],
                       ev.udpPorts[1], ev.udpPorts[2], ev.tcpPorts[0], ev.tcpPorts[1],
                       ev.tcpPorts[2]);
            logUi(b);
            break;
        }

        case K::Resolution: {
            // Only ever seen with debug=true; it becomes a suffix on the hint line.
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
        case IDC_SEC_ADVANCED:
        case IDC_SEC_DETAILS: {
            bool& flag = (id == IDC_SEC_ADVANCED) ? showAdvanced_ : showDetails_;
            flag = !flag;
            cfg_.showAdvanced = showAdvanced_;
            cfg_.showDetails  = showDetails_;
            applySectionVisibility();
            layout();
            resizeToContent();
            store_.save(cfg_);
            break;
        }
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
            showAdvanced_ = cfg_.showAdvanced;
            showDetails_  = cfg_.showDetails;
            applySectionVisibility();

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
                embedSuspended_ = true;
                logUi(L"video: picture window closed, the receiver keeps its own");
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

        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: {
                    int cmd = tray_.trackMenu(hwnd_, host_.isRunning());
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

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd_, &ps);
            const int d = s(13);
            const int x = s(14);
            const int yy = s(12) + (s(28) - d) / 2;
            const COLORREF c = stateColor(state_);
            HBRUSH br = CreateSolidBrush(c);
            HPEN   pen = CreatePen(PS_SOLID, 1, c);
            HGDIOBJ oldBr = SelectObject(dc, br);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            Ellipse(dc, x, yy, x + d, yy + d);
            SelectObject(dc, oldBr);
            SelectObject(dc, oldPen);
            DeleteObject(br);
            DeleteObject(pen);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) layout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = s(500);
            // logTop_ is 0 until the first layout(); s(250) covers the collapsed window.
            mmi->ptMinTrackSize.y = (logTop_ > 0 ? logTop_ : s(250)) +
                                    (showDetails_ ? s(80) : 0) + s(14) + chromeHeight();
            return 0;
        }

        case WM_DPICHANGED: {
            dpi_ = static_cast<int>(HIWORD(wp));
            createFonts();
            for (HWND h : {status_, hint_, lblName_, editName_, lblPort_, editPort_,
                           lblVideo_, cmbVideo_, lblAudio_, cmbAudio_, lblFps_, cmbFps_,
                           lblDecoder_, cmbDecoder_, lblReset_, cmbReset_,
                           chkFullscreen_,
                           chkH265_, chkDebug_, chkAlwaysOnTop_, chkAutostart_, chkEmbed_,
                           chkLogon_, btnToggle_, btnCopy_, secAdvanced_, secDetails_,
                           listLog_})
                if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
            if (status_)
                SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(fontStatus_), TRUE);
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
                host_.setCallback(nullptr);
                releaseVideo();
                host_.stop();
            }
            return 0;

        case WM_DESTROY:
            host_.setCallback(nullptr);
            releaseVideo();
            host_.stop();
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
