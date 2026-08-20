# Manuel doğrulama listesi (iPhone gerektirir)

Ajanlar bu adımları **yapamaz**; "çalışıyor" iddiası ancak bu liste elle işaretlenince geçerlidir.
Her satır: `[ ]` bekliyor · `[x] YYYY-MM-DD` doğrulandı · `[!]` başarısız (not ekle).

## Phase 0 — Upstream uxplay.exe (custom kod yok)
- [x] 2026-08-20 `scripts/smoke-test.ps1` yeşil — 11 PASS / 0 FAIL (17 gst elementi, `uxplay.exe -v` 1.74, dahili mDNS `_airplay._tcp`+`_raop._tcp` ilanı aynı host'tan görüldü)
- [!] 2026-08-20 iPhone Ekran Yansıtma listesinde **görünmedi** — PC Ethernet 192.168.1.107, sunucu ayakta; teşhis: ilan edilen A kaydı 127.0.0.1 (patches/0001 bekleniyor)
- [ ] Oturum kuruluyor, uxplay penceresinde görüntü var (video)
- [ ] Ses PC'den geliyor (audio)
- [ ] Döndürme (portrait↔landscape) düzgün
- [ ] iPhone'dan "Yansıtmayı Durdur" → uxplay temiz şekilde beklemeye dönüyor, tekrar bağlanabiliyor
- [ ] Gecikme kabul edilebilir (ms tahmini not edilsin)
- Notlar: kullanılan mDNS modu (`USE_MDNS` dahili / `USE_DNS_SD`), video sink, ses sink, build tarihi

## Phase 2 — Kendi GUI'miz
- [ ] GUI açılınca "waiting" durumu, bağlanınca "connected" + cihaz adı/çözünürlük/FPS
- [ ] Pencere boyutlandırma aspect-ratio koruyor
- [ ] Tam ekran / always-on-top / tray ikonu / tek örnek (ikinci açılış öne getirir)
- [ ] Start/Stop/Reconnect döngüsü 5 kez sorunsuz
- [ ] Ayarlar `%APPDATA%\airplay\config.ini`'ye yazılıyor ve yeniden açılışta korunuyor

## Phase 3 — Paket
- [ ] MSYS2 kurulu olmayan temiz bir Windows'ta self-contained klasör çalışıyor
- [ ] Autostart açıkken oturum açılışında başlıyor

## Bilinen gotcha'lar (kontrol et)
- Ağ profili **Private** olmalı; firewall UDP 5353 + UxPlay TCP/UDP portları açık
- UDP 5353'ü Windows svchost + Spotify de dinliyor → çakışma şüphesinde Spotify'ı kapatıp tekrar dene
- `HOME`/`XDG_CONFIG_HOMEDIR` ayarlı değilse uxplay kalıcı durumunu kaybeder (launcher ayarlamalı)
