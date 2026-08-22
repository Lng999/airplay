# PHASE 2 SPEC — Windows GUI, Milestone 1 (child-process wrapper)

Kaynaklar: `docs/SPEC.md` §2/§3 Phase 2, `docs/DESIGN.md` §6.1 (argv tablosu + stdout satırları), §5.2/5.3 (Windows blokları, HOME tuzağı).
Sözleşme: `app/include/airplay/uxplay_host.h` (değiştirmek = bu dosyayı da değiştirmek).

## Kararlar
- C++17, saf Win32 (Qt/MFC yok). MSYS2 UCRT64 + CMake/Ninja (UxPlay ile aynı toolchain). GUI exe `-static -municode -mwindows` ile MSYS2 DLL'siz; `uxplay.exe` child'ı kendi DLL'lerini `PATH` üzerinden bulur.
- **M1 = süreç sarmalama**: GUI `uxplay.exe`'yi child olarak başlatır, stdout'u parse eder. Video penceresi child'ın kendi penceresidir (d3d11videosink), **embed yok** (M2).
- Graceful stop: `CREATE_NEW_PROCESS_GROUP` + `FreeConsole/AttachConsole(pid)/SetConsoleCtrlHandler(NULL,TRUE)/GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,pid)`; 3 s sonra `TerminateProcess`. Gerçek `build/uxplay.exe` ile doğrulanacak.
- Çözünürlük sadece `debug=true` iken gelir (DESIGN §6.1); FPS/bitrate M1'de **yok** (M2, renderer içinden).
- Always-on-top M1'de GUI penceresine uygulanır (video penceresine M2'de).
- Config: `%APPDATA%\airplay\config.ini` — `[receiver] name, port, video_sink, audio_sink, fullscreen, h265, debug, nohold, reset` · `[app] always_on_top, start_minimized, autostart_receiver, msys_root, uxplay_path` · `[window] x,y,w,h`.
- Single instance: named mutex `Local\airplay-gui-{GUID}` + `FindWindowW(L"AirplayGuiMainWindow")` → `SetForegroundWindow`.
- Tray: Shell_NotifyIcon, menü Show / Start / Stop / Exit; kapat düğmesi tray'e küçültür (Exit ile çıkılır). İkon: şimdilik `IDI_APPLICATION` (kendi .ico Phase 3).
- Log: son 500 satır GUI listbox'ında + `%LOCALAPPDATA%\airplay\logs\gui.log` (append, 5 MB'ta döndür).

## Dosya sahipliği (lane'ler)
| Lane | Sahip olduğu dosyalar |
|---|---|
| H (host) | `app/src/host/CMakeLists.txt`, `app/src/host/uxplay_host.cpp`, `app/src/host/line_parser.{h,cpp}`, `app/src/host/child_env.{h,cpp}`, `app/tests/CMakeLists.txt`, `app/tests/test_line_parser.cpp`, `app/tests/test_host_live.cpp` |
| U (ui) | `app/CMakeLists.txt`, `app/src/ui/*.{h,cpp}` (`main.cpp`, `main_window`, `tray`, `config_store`, `single_instance`, `ui_log`), `app/res/app.rc`, `app/res/app.manifest`, `app/README.md` |
| Orkestratör | `app/include/airplay/uxplay_host.h`, bu spec, `scripts/build-app.sh` |

`app/CMakeLists.txt` (U): `project(airplay_gui CXX)`, `add_subdirectory(src/host)` (H: `airplay_host` static lib; `add_subdirectory(../tests)` değil — testler `app/tests/CMakeLists.txt` üzerinden `add_subdirectory(tests)` ile U tarafından eklenir, hedef adları `airplay_host_tests`, `airplay_host_live`), `add_executable(airplay-gui WIN32 ...)` link `airplay_host shell32 comctl32 user32 gdi32 advapi32 iphlpapi ws2_32`. `-DUNICODE -D_UNICODE`, `-municode`, `-static`, `-Wall -Wextra`.

## Kabul kriterleri
- H: `airplay_host_tests` (console) — `parseUxplayLine` için DESIGN §6.1 tablosundaki **her satır** için bir test (banner, ports, MAC, key storage, client connecting, rejected, blocked, audio format ct=8/ct=2, PIN, registered, lost connection, feedback timeout, Stopping, video/audio_disabled, fullscreen hint, debug resolution, WARNING, ERROR, bilinmeyen satır → sadece LogLine). `buildArgs` testleri (port 0 → çıplak `-p`; debug → `-d`; extraArgs sona). `airplay_host_live`: gerçek `build/uxplay.exe` ile start → Waiting'e ≤5 s'de geçiş (Ports event) → stop → child çıktı ve state Stopped; graceful yol (CTRL_BREAK) çalıştı mı yoksa TerminateProcess'e mi düştü raporla.
- U: `airplay-gui.exe` açılır, Start → child başlar, status "Waiting (AirPlay-PC @ 192.168.x.x)" olur (IP: `GetAdaptersAddresses` ile ilk non-loopback IPv4), `scripts/mdns-browse.py --expect AirPlay-PC` PASS; Stop → child kapanır; config.ini yazılır/okunur; ikinci örnek ilkini öne getirir; tray menüsü çalışır. Manuel (iPhone): `docs/MANUAL-VERIFY.md` Phase 2.
- Ortak: `scripts/build-app.sh` (orkestratör) `app/` → `build-app/airplay-gui.exe`; uyarısız derlenir.

## M2 → `docs/PHASE2-M2-SPEC.md` (yapıldı, 2026-08-22)
Bu bölümün öngörüsü — kendi `uxplay_core`'umuz + `GstVideoOverlay` — **uygulanmadı ve
uygulanamazdı**: sink kendisine verilen HWND'yi subclass ediyor,
`SetWindowLongPtr(GWLP_WNDPROC)` ise başka sürecin penceresinde çalışmıyor. Bunun yerine
alıcının kendi penceresi `SetParent` ile evlat ediniliyor; ölçümler ve gerekçe
`docs/PHASE2-M2-SPEC.md` içinde. FPS/bitrate ise `patches/0005` ile alıcının kendi
ölçümünden geliyor. Aspect-ratio hem letterbox hem `WM_SIZING` ile korunuyor.
