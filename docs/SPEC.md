# SPEC v2 — Windows AirPlay Receiver (Fable yorumu)

Kaynak: `docs/prompt-original.md`. Bu belge orijinal prompt'un *yorumlanmış ve iyileştirilmiş* halidir.
Durum etiketleri: **[KARAR]** kesinleşti · **[AÇIK]** ajan verisi bekliyor · **[MANUEL]** iPhone ile elle doğrulanacak.

## 1. Hedef (değişmedi)
iPhone 13 → Windows 10/11 PC, yerleşik AirPlay Screen Mirroring, Wi-Fi üzerinden. iOS kodu yok, Apple hesabı yok. mDNS ile ilan, oturum kabul, H.264 + AAC/ALAC decode & render.

## 2. Orijinal prompt'a göre iyileştirmeler
| # | Orijinal | İyileştirme | Gerekçe |
|---|---|---|---|
| 1 | "Fork UxPlay" | UxPlay **pinned submodule** (`third_party/UxPlay`) + `patches/` dizini; kendi kodumuz `app/` altında ayrı | Upstream güncellemeleri kolay çekilir, diff küçük kalır, GPL sınırı net |
| 2 | Phase 0 elle kurulum | `scripts/setup-msys2.ps1` + `scripts/build.sh` ile **tek komut** kurulum/build | Tekrarlanabilirlik; CI'da aynı betik koşar |
| 3 | CI yok | GitHub Actions (`msys2/setup-msys2`) her commit'te build | "Her değişiklik commit" kuralı build kırılmasını hızlı yakalasın |
| 4 | "Qt/SDL2/Win32 seç" | **[AÇIK]** Ön tercih Win32 + GStreamer `d3d11videosink` HWND embed (GstVideoOverlay) | Sıfır ek bağımlılık, MSYS2 runtime zaten var; Qt paket boyutunu katlar |
| 5 | iPhone olmadan test yok | **iPhone'suz duman testi**: PC'den `_airplay._tcp` mDNS keşfi (python zeroconf / dns-sd), `gst-inspect` plugin kontrolü, `uxplay -d` debug çıktısı | Ajanlar cihazsız ilerleyebilsin |
| 6 | Config belirsiz | `%APPDATA%\airplay\config.ini` (isim, port, sink, always-on-top, fullscreen, autostart) | Kalıcı ayar, Phase 3 "remember settings" baştan çözülür |
| 7 | Log yok | Dosya logu `%LOCALAPPDATA%\airplay\logs\` + GUI'de son N satır | Hata ayıklama |
| 8 | Paketleme belirsiz | Self-contained klasör (ntldd ile DLL toplama) + opsiyonel Inno Setup | MSYS2 olmayan makinede çalışsın |
| 9 | DRM belirsiz | FairPlay/DRM video **kapsam dışı** (prompt'taki gibi), ayrıca HLS/YouTube cast de kapsam dışı | Net sınır |

## 3. Fazlar
### Phase 0 — Kanıtla (custom kod yok)
- [ ] `scripts/setup-msys2.ps1`: MSYS2 sessiz kurulum/var olanı kullan, pacman paketleri **[AÇIK: liste]**
- [ ] `scripts/build.sh`: UxPlay'i `-DUSE_MDNS=1` ile derle → `build/uxplay.exe`
- [ ] `scripts/smoke-test.ps1`: gst-inspect plugin kontrolü + mDNS ilan görünürlüğü
- [ ] Firewall kuralları (UDP 5353 + UxPlay portları) **[AÇIK: port listesi]**
- [ ] `docs/BUILD-NOTES.md`: tam komutlar, gotcha'lar
- [ ] **[MANUEL]** iPhone → PC mirroring: video + ses + keşif

### Phase 1 — Anla
- [ ] `docs/DESIGN.md`: handshake akışı (pair-setup/verify, GET /info, SETUP, /feedback, /audio), TXT kayıtları, portlar, features bitmask — hepsi `file:line` atıflı **[AÇIK: ajan haritası]**
- [ ] Embed stratejisi kararı: lib/ doğrudan link vs uxplay.exe süreç sarmalama **[AÇIK]**

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
