# SPEC v2 — Windows AirPlay Receiver (Fable yorumu)

Kaynak: `docs/prompt-original.md`. Bu belge orijinal prompt'un *yorumlanmış ve iyileştirilmiş* halidir.
Durum etiketleri: **[KARAR]** kesinleşti · **[AÇIK]** ajan verisi bekliyor · **[MANUEL]** iPhone ile elle doğrulanacak.

## 1. Hedef (değişmedi)
iPhone 13 → **Windows 10 Pro 22H2** PC (ortam envanteri: Win11 değil; Ryzen 5 5600, RTX 4060, Ethernet 192.168.1.107, ağ profili Private), yerleşik AirPlay Screen Mirroring, Wi-Fi üzerinden. iOS kodu yok, Apple hesabı yok. mDNS ile ilan, oturum kabul, H.264 + AAC/ALAC decode & render.

## 2. Orijinal prompt'a göre iyileştirmeler
| # | Orijinal | İyileştirme | Gerekçe |
|---|---|---|---|
| 1 | "Fork UxPlay" | UxPlay **pinned submodule** (`third_party/UxPlay`) + `patches/` dizini; kendi kodumuz `app/` altında ayrı | Upstream güncellemeleri kolay çekilir, diff küçük kalır, GPL sınırı net |
| 2 | Phase 0 elle kurulum | `scripts/setup-msys2.ps1` + `scripts/build.sh` ile **tek komut** kurulum/build | Tekrarlanabilirlik; CI'da aynı betik koşar |
| 3 | CI yok | GitHub Actions (`msys2/setup-msys2`) her commit'te build | "Her değişiklik commit" kuralı build kırılmasını hızlı yakalasın |
| 4 | "Qt/SDL2/Win32 seç" | **[KARAR]** Win32 + GStreamer `d3d11videosink` HWND embed (GstVideoOverlay) | Sıfır ek bağımlılık, MSYS2 runtime zaten var; Qt paket boyutunu katlar; RTX 4060 var |
| 5 | iPhone olmadan test yok | **iPhone'suz duman testi**: PC'den `_airplay._tcp` mDNS keşfi (python zeroconf / dns-sd), `gst-inspect` plugin kontrolü, `uxplay -d` debug çıktısı | Ajanlar cihazsız ilerleyebilsin |
| 6 | Config belirsiz | `%APPDATA%\airplay\config.ini` (isim, port, sink, always-on-top, fullscreen, autostart) | Kalıcı ayar, Phase 3 "remember settings" baştan çözülür |
| 7 | Log yok | Dosya logu `%LOCALAPPDATA%\airplay\logs\` + GUI'de son N satır | Hata ayıklama |
| 8 | Paketleme belirsiz | Self-contained klasör (ntldd ile DLL toplama) + opsiyonel Inno Setup | MSYS2 olmayan makinede çalışsın |
| 9 | DRM belirsiz | FairPlay/DRM video **kapsam dışı** (prompt'taki gibi), ayrıca HLS/YouTube cast de kapsam dışı | Net sınır |

## 2b. Kaynak haritasından gelen sabit gerçekler (docs/research/uxplay-source-map.md)
- Pin: `a3c19cbc` (2026-08-09, VERSION "1.74" `uxplay.cpp:75`; son tag v1.73.6 → HEAD yayınlanmamış). **Hash pinle, branch değil.**
- Dahili mDNSResponder `lib/mdnsd/` Windows'ta **varsayılan, bayrak gerekmez** (`CMakeLists.txt:51-66`, Fable doğruladı). **Prompt'taki `-DUSE_MDNS=1` yanlış** — o bayrak yalnızca Apple'da anlamlı. Bonjour fallback'i `-DUSE_DNS_SD=1` (+ Bonjour SDK `dnssd.dll`). Bilinen risk: upstream issue #546 — dahili mDNS native Windows'ta bir kullanıcıda çalışmadı (açık) → **proje #1 riski**, fallback planı hazır tutulur.
- Features bitmask: `lib/dnssdint.h:32-34` tohum, **`uxplay.cpp:2001-2092` bit bit yeniden yazıyor** → DESIGN.md o bloğu alıntılar.
- Handler tabloları: `lib/raop.c:406-444` (RTSP), `:445-478` (HTTP). Video pipeline `renderers/video_renderer.c:360-395`; varsayılan sink her platformda `autovideosink` (`uxplay.cpp:107`) → biz `d3d11videosink` geçeceğiz.
- **Tuzak:** `HOME`/`XDG_CONFIG_HOMEDIR` yoksa Windows'ta kalıcı durum kaybolur (`uxplay.cpp:773-777`, `:3153-3162`) → launcher ayarlar.
- Lisans: GPLv3 + `lib/playfair` "unclear" (README:2486-2489) → dağıtım yok, kişisel.
- Build: `cmake -B build -G "Unix Makefiles"`/Ninja, paketler UCRT64: `mingw-w64-ucrt-x86_64-{cmake,gcc,openssl,libplist,gstreamer,gst-plugins-base,gst-plugins-good,gst-plugins-bad,gst-libav,pkg-config}` + `base-devel git`.
- Ortam: MSYS2/GStreamer/pkg-config **yok**; VS 2026 var ama MinGW gerekli. UDP 5353'ü svchost+Spotify de dinliyor (paylaşımlı multicast, gotcha).

## 2c. GStreamer/MSYS2 araştırmasından gelen kararlar (docs/research/gstreamer-msys2-windows.md)
- **[KARAR]** Ortam UCRT64 (MINGW64 2026-03 itibarıyla deprecated). Sessiz kurulum: `msys2-x86_64-latest.exe in --confirm-command --accept-messages --root C:/msys64` (winget alternatif).
- **[KARAR]** Paketler: `pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,openssl,libplist,gstreamer,gst-plugins-base,gst-plugins-good,gst-plugins-bad,gst-libav,ntldd} git` (GStreamer 1.28.6). `gst-libav` zorunlu: `avdec_aac`/`avdec_alac` başka yerde yok; `h264parse` bad'de.
- **[KARAR]** Sink'ler: video `d3d11videosink` (GstVideoOverlay + render-rectangle destekli, `force-aspect-ratio` varsayılan true), ses `wasapi2sink`. d3d12 `direct-swapchain` ile render-rectangle'ı yok sayıyor → seçilmedi.
- UxPlay'de **hiç GstVideoOverlay kodu yok** → HWND embed için kendi patch'imiz/core'umuz gerekir (Phase 2). `d3d11videosink` HWND'nin WNDPROC'unu subclass eder ve WS_CHILD pencere parent'lar → ayrı child panel ver, pencereyi yok etmeden pipeline'ı NULL'a çek.
- Portlar varsayılan rastgele → her zaman `-p` ile sabitle: TCP 7100/7000/7001, UDP 6000/6001/7011 + UDP 5353. Firewall: program-scoped kural (`New-NetFirewallRule -Program … -Protocol Any -Profile Private`).
- `GST_REGISTRY` kendi dosyamıza yönlendir (OBS vb. ile %LOCALAPPDATA% cache çakışması).
- Apple Bonjour Service varsa UDP 5353 için yarışır (issue #297) — bu makinede yok, yine de kontrol et.

## 3. Fazlar
### Phase 0 — Kanıtla (custom kod yok)
- [ ] `scripts/setup-msys2.ps1`: MSYS2 sessiz kurulum (winget/installer) + pacman paketleri (liste §2b)
- [ ] `scripts/build.sh`: UxPlay'i varsayılan (dahili mDNS) ile derle → `build/uxplay.exe`; `-DUSE_DNS_SD=1` opsiyonel fallback
- [ ] `scripts/smoke-test.ps1`: gst-inspect plugin kontrolü + mDNS ilan görünürlüğü
- [ ] Firewall kuralları (UDP 5353 + UxPlay portları) **[AÇIK: port listesi]**
- [ ] `docs/BUILD-NOTES.md`: tam komutlar, gotcha'lar
- [ ] **[MANUEL]** iPhone → PC mirroring: video + ses + keşif

### Phase 1 — Anla
- [x] `docs/DESIGN.md` yazıldı (968 satır, ~400 atıf). Prompt düzeltmeleri: **`/audio` endpoint'i yok** (ses = UDP RTP, `SETUP` type 96); **`/fp-setup` düz mirroring'de zorunlu** (AES anahtarı FairPlay-şifreli gelir, `raop_handlers.h:800`); efektif varsayılan features **`0x527FFEE6,0x0`** (`dnssdint.h:33` yorumdaki değer); SPS/PPS ayrı gönderilmez, sonraki IDR'a eklenir; çözünürlük/features çıktısı sadece `-d` ile basılır → launcher `-d` açık başlatır.
- [x] **[KARAR]** Embed stratejisi (kaynak haritası §8): **Milestone 1 = `uxplay.exe`'yi child process olarak sar** (stdout parse, `-p`/`-n`/`-vs` argümanları). **Son hedef = kendi `uxplay_core`'umuz** `lib/` (`raop.h:125-144`, `dnssd.h:58-75` extern "C" API) + `renderers/` üstüne; `uxplay.cpp` monolitik (static'ler, `exit()`), olduğu gibi link edilemez.

### Phase 2 — Windows uygulaması
- [ ] `app/` Win32 GUI: durum (waiting/connected), cihaz adı, çözünürlük, FPS, bitrate
- [ ] Config, start/stop/reconnect, single-instance (named mutex), tray ikonu
- [ ] Video HWND embed, aspect-ratio korumalı resize

### Phase 3 — Cila + paket
- [ ] Autostart (HKCU Run), ekran görüntüsü, log
- [ ] Self-contained build + DLL listesi, opsiyonel installer

## 4. Teslimatlar (bu geçiş)
1. Phase 0 betikleri + BUILD-NOTES.md
2. DESIGN.md (atıflı)
3. Derlenen fork + minimal GUI **veya** net ROADMAP.md
4. `docs/MANUAL-VERIFY.md`: doğrulanmış vs tahmin ayrımı

Lisans: GPLv3 (upstream ile aynı). Kişisel kullanım.
