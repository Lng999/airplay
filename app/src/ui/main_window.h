// main_window.h - the single top-level window of airplay-gui.
//
// Threading: UxplayHost invokes its callback on the reader thread. We never touch a HWND
// from there; the callback allocates a copy of the HostEvent and PostMessageW's it as
// WM_HOST_EVENT, and the window procedure takes ownership and deletes it.
#pragma once

#include <string>

#include "config_store.h"
#include "tray.h"
#include "ui_log.h"
#include "video_window.h"

namespace ui {

class MainWindow {
public:
    static constexpr UINT WM_HOST_EVENT = WM_APP + 1;   // lParam = airplay::HostEvent* (owned)
    static constexpr UINT WM_TRAY       = WM_APP + 2;   // shell notification-area callback
    // Both are posted by the updater worker thread and carry an owned heap object.
    static constexpr UINT WM_UPDATE_CHECKED    = WM_APP + 3;   // lParam = UpdateResult* (owned)
    static constexpr UINT WM_UPDATE_DOWNLOADED = WM_APP + 4;   // lParam = UpdateResult* (owned)

    MainWindow(HINSTANCE hinst, ConfigStore& store, AppConfig& cfg);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create();
    void show(bool minimizedToTray);
    HWND hwnd() const { return hwnd_; }

    // Same effect as pressing Start (used by [app] autostart_receiver and -autostart).
    void startReceiver();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp);

    void createControls();
    void createFonts();
    void layout();
    void applySectionVisibility();
    void resizeToContent();
    // Update flow. Each step runs on a throwaway worker thread and posts its result back;
    // updateBusy_ is the only guard, so a second request while one is in flight is ignored.
    void startUpdateCheck(bool manual);
    void startUpdateDownload();
    void onUpdateChecked(struct UpdateResult* r);
    void onUpdateDownloaded(struct UpdateResult* r);
    int  chromeHeight() const;
    void controlsFromConfig();
    void configFromControls();

    // --- the picture (docs/PHASE2-M2-SPEC.md) --------------------------------
    // The receiver only creates its video window once frames arrive, and destroys it when
    // they stop, so adoption is a poll rather than an event.
    void pollVideoWindow();
    void applyEmbedSetting();     // the checkbox changed while the receiver may be running
    void releaseVideo();          // hand the picture back; always before stopping the child
    void updateVideoTitle();

    void onHostEvent(const airplay::HostEvent& ev);
    void onCommand(int id, int code);

    void doStart();
    void doStop();
    void doCopyCmdline();
    void doExit();

    void updateStatus();
    void updateButtons();
    void applyAlwaysOnTop();
    void saveWindowRect();
    void logUi(const std::wstring& line);

    int  s(int logical) const { return MulDiv(logical, dpi_, 96); }

    HINSTANCE    hinst_ = nullptr;
    ConfigStore& store_;
    AppConfig&   cfg_;

    HWND hwnd_ = nullptr;
    HWND status_ = nullptr, hint_ = nullptr;
    HWND lblName_ = nullptr, editName_ = nullptr;
    HWND lblPort_ = nullptr, editPort_ = nullptr;
    HWND lblVideo_ = nullptr, cmbVideo_ = nullptr;
    HWND lblAudio_ = nullptr, cmbAudio_ = nullptr;
    HWND lblFps_ = nullptr, cmbFps_ = nullptr;
    HWND lblDecoder_ = nullptr, cmbDecoder_ = nullptr;
    HWND lblReset_ = nullptr, cmbReset_ = nullptr;
    HWND chkFullscreen_ = nullptr, chkH265_ = nullptr, chkDebug_ = nullptr;
    HWND chkAlwaysOnTop_ = nullptr, chkAutostart_ = nullptr;
    HWND chkEmbed_ = nullptr, chkLogon_ = nullptr, chkFrame_ = nullptr;
    HWND btnToggle_ = nullptr, btnCopy_ = nullptr;   // one button: Start <-> Stop
    HWND secAdvanced_ = nullptr, secDetails_ = nullptr;   // collapsible section headers
    HWND listLog_ = nullptr;

    HFONT fontUi_ = nullptr, fontStatus_ = nullptr;
    int   dpi_ = 96;

    // Collapsed by default: the plain window is status + name + one button.
    bool showAdvanced_ = false, showDetails_ = false;
    int  logTop_ = 0;   // y of the log area, set by layout(); drives resizeToContent()

    airplay::UxplayHost host_;
    UiLog log_;
    Tray  tray_;
    VideoWindow video_;

    static constexpr UINT_PTR kUpdateTimer = 1;   // one shot, 4 s after the window is up
    static constexpr UINT_PTR kEmbedTimer  = 2;   // 300 ms, while the receiver runs

    airplay::HostState state_ = airplay::HostState::Stopped;
    int   currentFps_     = 0;   // last non-zero frame rate, shown in the status line
    int   currentKbps_    = 0;   // last video bitrate from patches/0005
    bool  updateBusy_     = false;   // a check or a download is in flight
    std::wstring updateVersion_, updateUrl_, updatePageUrl_;
    std::wstring clientName_, clientModel_, lastError_, ipv4_;
    std::wstring resolutionText_;   // "1920x1080", only ever filled when debug=true
    bool  exiting_        = false;
    // adoptWindow() refused to move the receiver's window; stop trying for this session.
    bool  embedSuspended_ = false;
    UINT  taskbarCreated_ = 0;
};

// First "up", non-loopback IPv4 unicast address (GetAdaptersAddresses). Empty if none.
std::wstring firstLocalIPv4();

} // namespace ui
