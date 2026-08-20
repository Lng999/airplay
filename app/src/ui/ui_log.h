// ui_log.h - the GUI's log sink: a bounded listbox plus %LOCALAPPDATA%\airplay\logs\gui.log.
#pragma once

#include <string>

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

class UiLog {
public:
    static constexpr int      kMaxListLines = 500;
    static constexpr uint64_t kRotateBytes  = 5ull * 1024 * 1024;

    UiLog() = default;
    ~UiLog();
    UiLog(const UiLog&) = delete;
    UiLog& operator=(const UiLog&) = delete;

    // Creates %LOCALAPPDATA%\airplay\logs and opens gui.log for append.
    bool open();
    void close();

    // The listbox that mirrors the file. May be nullptr (file-only logging).
    void setListBox(HWND lb) { listBox_ = lb; }

    // line is UTF-8 (uxplay stdout) or plain ASCII (our own messages). No trailing newline.
    void append(const std::string& line);
    void appendW(const std::wstring& line);

    const std::wstring& path() const { return path_; }

private:
    void writeFile(const std::string& utf8Line);
    void rotateIfNeeded();

    HANDLE       file_    = INVALID_HANDLE_VALUE;
    HWND         listBox_ = nullptr;
    std::wstring path_;
    uint64_t     size_     = 0;
    int          hExtent_  = 0;   // widest line so far, for the horizontal scrollbar
};

} // namespace ui
