#include "ui_log.h"

#include <windowsx.h>

#include <cstdint>

namespace ui {

UiLog::~UiLog() { close(); }

bool UiLog::open() {
    std::wstring dir = joinPath(localAppDir(), L"logs");
    if (dir.empty() || !ensureDir(dir)) return false;
    path_ = joinPath(dir, L"gui.log");

    file_ = CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (GetFileSizeEx(file_, &sz)) size_ = static_cast<uint64_t>(sz.QuadPart);
    return true;
}

void UiLog::close() {
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

void UiLog::rotateIfNeeded() {
    if (file_ == INVALID_HANDLE_VALUE || size_ < kRotateBytes) return;

    close();
    std::wstring old = path_ + L".1";
    MoveFileExW(path_.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    size_ = 0;
    file_ = CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void UiLog::writeFile(const std::string& utf8Line) {
    if (file_ == INVALID_HANDLE_VALUE) return;
    std::string rec = utf8Line;
    rec += "\r\n";
    DWORD written = 0;
    if (WriteFile(file_, rec.data(), static_cast<DWORD>(rec.size()), &written, nullptr))
        size_ += written;
    rotateIfNeeded();
}

void UiLog::append(const std::string& line) { appendW(widen(line)); }

void UiLog::appendW(const std::wstring& line) {
    writeFile(narrow(line));
    if (!listBox_) return;

    // Strip control characters that would confuse the listbox.
    std::wstring clean;
    clean.reserve(line.size());
    for (wchar_t c : line) clean.push_back(c == L'\r' || c == L'\n' || c == L'\t' ? L' ' : c);

    int idx = static_cast<int>(SendMessageW(listBox_, LB_ADDSTRING, 0,
                                            reinterpret_cast<LPARAM>(clean.c_str())));
    if (idx == LB_ERR || idx == LB_ERRSPACE) return;

    int count = static_cast<int>(SendMessageW(listBox_, LB_GETCOUNT, 0, 0));
    while (count > kMaxListLines) {
        SendMessageW(listBox_, LB_DELETESTRING, 0, 0);
        --count;
        --idx;
    }

    // Horizontal scrollbar: LB_SETHORIZONTALEXTENT wants pixels, so measure the string.
    if (HDC dc = GetDC(listBox_)) {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(listBox_, WM_GETFONT, 0, 0));
        HGDIOBJ prev = font ? SelectObject(dc, font) : nullptr;
        SIZE sz{};
        if (GetTextExtentPoint32W(dc, clean.c_str(), static_cast<int>(clean.size()), &sz)) {
            int want = sz.cx + 16;
            if (want > hExtent_) {
                hExtent_ = want;
                SendMessageW(listBox_, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(hExtent_), 0);
            }
        }
        if (prev) SelectObject(dc, prev);
        ReleaseDC(listBox_, dc);
    }

    SendMessageW(listBox_, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
}

} // namespace ui
