// strings.h - every string the user can see, in one place (docs/PHASE2-UX-SPEC.md).
//
// The UI language is Turkish; code, comments, log lines and config.ini keys stay English.
// There is no i18n machinery on purpose: one language, compile-time constants.
//
// This file is UTF-8. GCC reads UTF-8 sources by default and widens L"..." to UTF-16, so the
// Turkish letters below survive the build without an explicit -finput-charset.
#pragma once

namespace ui {
namespace str {

// --- window / tray ----------------------------------------------------------
inline constexpr const wchar_t* kAppName   = L"airplay";
inline constexpr const wchar_t* kTitleSep  = L" — ";

inline constexpr const wchar_t* kTrayShow  = L"Göster";
inline constexpr const wchar_t* kTrayStart = L"Başlat";
inline constexpr const wchar_t* kTrayStop  = L"Durdur";
inline constexpr const wchar_t* kTrayExit  = L"Çıkış";

// --- status line ------------------------------------------------------------
inline constexpr const wchar_t* kStateStopped  = L"Kapalı";
inline constexpr const wchar_t* kStateStarting = L"Başlatılıyor…";
inline constexpr const wchar_t* kStateWaiting  = L"Hazır";
inline constexpr const wchar_t* kStateConnected= L"Bağlandı";
inline constexpr const wchar_t* kStateStopping = L"Durduruluyor…";
inline constexpr const wchar_t* kStateError    = L"Hata";

// Second line: what the user should do next.
inline constexpr const wchar_t* kHintStopped   = L"Alıcıyı başlatmak için Başlat'a basın";
inline constexpr const wchar_t* kHintWaitingPre= L"iPhone'unda Denetim Merkezi → Ekran Yansıtma → ";
inline constexpr const wchar_t* kHintUnknownClient = L"Bir cihaz bağlandı";
inline constexpr const wchar_t* kHintUnknownError  = L"Bilinmeyen hata";

// --- receiver settings ------------------------------------------------------
inline constexpr const wchar_t* kLabelName  = L"Bu bilgisayarın adı:";
inline constexpr const wchar_t* kLabelPort  = L"Port";
inline constexpr const wchar_t* kLabelVideo = L"Görüntü çıkışı";
inline constexpr const wchar_t* kLabelAudio = L"Ses çıkışı";

inline constexpr const wchar_t* kChkFullscreen  = L"Tam ekran";
inline constexpr const wchar_t* kChkH265        = L"H.265";
inline constexpr const wchar_t* kChkDebug       = L"Ayrıntılı günlük";
inline constexpr const wchar_t* kChkAlwaysOnTop = L"Her zaman üstte";
inline constexpr const wchar_t* kChkAutostart   = L"Açılışta alıcıyı başlat";

// --- buttons / sections -----------------------------------------------------
inline constexpr const wchar_t* kBtnStart    = L"Başlat";
inline constexpr const wchar_t* kBtnStop     = L"Durdur";
inline constexpr const wchar_t* kBtnCopyCmd  = L"Komutu kopyala";
inline constexpr const wchar_t* kSecAdvanced = L"Gelişmiş";
inline constexpr const wchar_t* kSecDetails  = L"Ayrıntılar";
// Prefix on a section button; the glyphs live in Segoe UI itself, no symbol font needed.
inline constexpr const wchar_t* kSecClosed   = L"► ";   // right-pointing triangle
inline constexpr const wchar_t* kSecOpen     = L"▼ ";   // down-pointing triangle

// --- messages ---------------------------------------------------------------
inline constexpr const wchar_t* kErrNoUxplay =
    L"uxplay.exe bulunamadı. Önce derleyin (scripts/build.sh) ya da config.ini içindeki "
    L"[app] uxplay_path değerini ayarlayın.";
inline constexpr const wchar_t* kErrStartFailed = L"uxplay.exe başlatılamadı";
inline constexpr const wchar_t* kPinTitle       = L"Eşleştirme kodu";
inline constexpr const wchar_t* kTrayHintTitle  = L"airplay çalışmaya devam ediyor";
inline constexpr const wchar_t* kTrayHintText   =
    L"Pencere kapandı ama alıcı bildirim alanında çalışıyor. Tamamen kapatmak için "
    L"simgeye sağ tıklayıp Çıkış'ı seçin.";

} // namespace str
} // namespace ui
