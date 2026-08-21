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
inline constexpr const wchar_t* kTrayUpdate = L"Güncellemeleri kontrol et";

// --- updates ----------------------------------------------------------------
inline constexpr const wchar_t* kUpdateTitle    = L"Yeni sürüm";
inline constexpr const wchar_t* kUpdateFoundPre = L"Yeni bir sürüm çıktı: ";
inline constexpr const wchar_t* kUpdateAskTail  =
    L"\n\nŞimdi indirilip kurulsun mu? Kurulum sırasında uygulama kapanıp yeniden açılacak.";
inline constexpr const wchar_t* kUpdateNone     = L"Zaten en güncel sürümü kullanıyorsunuz.";
inline constexpr const wchar_t* kUpdateChecking = L"Güncelleme kontrol ediliyor…";
inline constexpr const wchar_t* kUpdateFailPre  = L"Güncelleme kontrolü başarısız: ";
inline constexpr const wchar_t* kUpdateDownloading = L"Yeni sürüm indiriliyor…";
inline constexpr const wchar_t* kUpdateNoAsset  =
    L"Bu sürüm için kurulum dosyası bulunamadı. Sürüm sayfası tarayıcıda açılsın mı?";

// --- status line ------------------------------------------------------------
inline constexpr const wchar_t* kStateStopped  = L"Kapalı";
inline constexpr const wchar_t* kStateStarting = L"Başlatılıyor…";
inline constexpr const wchar_t* kStateWaiting  = L"Hazır";
inline constexpr const wchar_t* kStateConnected= L"Bağlandı";
inline constexpr const wchar_t* kStateStopping = L"Durduruluyor…";
inline constexpr const wchar_t* kStateError    = L"Hata";

// Second line: what the user should do next.
inline constexpr const wchar_t* kHintStopped   = L"Alıcıyı başlatmak için Başlat'a basın";
inline constexpr const wchar_t* kHintWaitingPre= L"iPhone: Denetim Merkezi → Ekran Yansıtma → ";
inline constexpr const wchar_t* kHintUnknownClient = L"Bir cihaz bağlandı";
inline constexpr const wchar_t* kHintUnknownError  = L"Bilinmeyen hata";
inline constexpr const wchar_t* kHintErrorPrefix   = L"Alıcı kapandı: ";

// --- receiver settings ------------------------------------------------------
inline constexpr const wchar_t* kLabelName  = L"Bu bilgisayarın adı:";
inline constexpr const wchar_t* kLabelPort  = L"Port";
inline constexpr const wchar_t* kLabelVideo = L"Görüntü çıkışı";
inline constexpr const wchar_t* kLabelFps     = L"Akıcılık";
inline constexpr const wchar_t* kLabelDecoder = L"Görüntü çözücü";
inline constexpr const wchar_t* kLabelReset   = L"Bağlantı zaman aşımı";
inline constexpr const wchar_t* kLabelAudio = L"Ses çıkışı";

inline constexpr const wchar_t* kChkFullscreen  = L"Tam ekran";
inline constexpr const wchar_t* kChkH265        = L"H.265";
inline constexpr const wchar_t* kChkDebug       = L"Ayrıntılı günlük";
inline constexpr const wchar_t* kChkAlwaysOnTop = L"Her zaman üstte";
inline constexpr const wchar_t* kChkAutostart   = L"Açılışta alıcıyı başlat";

// Frame-rate ceiling advertised to the client. 30 is UxPlay's own default and the reason
// mirroring can feel choppy; 60 is what we ask for.
inline constexpr const wchar_t* kFps60 = L"60 FPS (akıcı)";
inline constexpr const wchar_t* kFps30 = L"30 FPS (varsayılan)";

// Decoder choices. The empty value means UxPlay's "decodebin", which picks by rank.
inline constexpr const wchar_t* kDecAuto  = L"Otomatik";
inline constexpr const wchar_t* kDecD3D11 = L"Direct3D 11 (sink ile aynı)";
inline constexpr const wchar_t* kDecNv    = L"NVIDIA NVDEC";
inline constexpr const wchar_t* kDecSw    = L"Yazılım (avdec_h264)";

// How long the client may stay silent before UxPlay declares the connection lost
// (-reset n). A locked iPhone goes quiet, so a short limit ends the session while the
// phone is merely in a pocket.
inline constexpr const wchar_t* kReset0  = L"Kapalı (telefon uykuda kalabilir)";
inline constexpr const wchar_t* kReset15 = L"15 saniye";
inline constexpr const wchar_t* kReset60 = L"60 saniye";

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
