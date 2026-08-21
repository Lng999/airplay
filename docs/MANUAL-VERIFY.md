# Manuel doğrulama listesi (iPhone gerektirir)

Ajanlar bu adımları **yapamaz**; "çalışıyor" iddiası ancak bu liste elle işaretlenince geçerlidir.
Her satır: `[ ]` bekliyor · `[x] YYYY-MM-DD` doğrulandı · `[!]` başarısız (not ekle).

## Phase 0 — Upstream uxplay.exe (custom kod yok)
- [x] 2026-08-20 `scripts/smoke-test.ps1` yeşil — 11 PASS / 0 FAIL (17 gst elementi, `uxplay.exe -v` 1.74, dahili mDNS `_airplay._tcp`+`_raop._tcp` ilanı aynı host'tan görüldü)
- [!] 2026-08-20 iPhone Ekran Yansıtma listesinde **görünmedi** — PC Ethernet 192.168.1.107, sunucu ayakta; teşhis: ilan edilen A kaydı 127.0.0.1 → `patches/0001` uygulandı, 2026-08-21 yamalı build ilanı **192.168.1.107** (smoke 12 PASS). iPhone'da yeniden denendi: **[x] 2026-08-21 görüldü**
- [x] 2026-08-21 Oturum kuruldu, görüntü geldi (kullanıcı raporu: "sorunsuz çalıştı") — yamalı build (patches/0001+0002), `-vs d3d11videosink -as wasapi2sink`
- [x] 2026-08-21 Ses geldi (kullanıcı raporu)
- [ ] Döndürme (portrait↔landscape) düzgün
- [ ] iPhone'dan "Yansıtmayı Durdur" → uxplay temiz şekilde beklemeye dönüyor, tekrar bağlanabiliyor
- [ ] **Telefon uykusu:** yansıtma sürerken yan tuşla ekranı kapat → PC'deki görüntü ~3 sn
      içinde kayboluyor, durum "Duraklatıldı" (turuncu nokta); telefonu aç → görüntü
      kendiliğinden geri geliyor ve odak çalınmıyor
- [ ] Telefonu 1 dakikadan uzun kapalı tut → açınca yine geri geliyor (zaman aşımı Kapalı)
- [ ] Gecikme kabul edilebilir (ms tahmini not edilsin)
- [ ] **Akıcılık:** 30 FPS ile 60 FPS arasında gözle görülür fark var mı? (Gelişmiş →
      Akıcılık; değişiklik Durdur/Başlat gerektirir). `-fps` iPhone'a `maxFPS` olarak
      gidiyor, varsayılanı 30 (`lib/raop.c:623`)
- [ ] **Görüntü çözücü:** Otomatik (d3d12h264dec) ile Direct3D 11 arasında fark var mı?
      Sink d3d11videosink olduğu için Otomatik'te her kare API sınırını geçiyor
- [ ] İstemci FPS raporu açıkken günlükteki XML gerçekte kaç fps geldiğini gösteriyor mu
- Notlar: dahili mDNS (lib/mdnsd + patches/0001), video `d3d11videosink`, ses `wasapi2sink`, build 2026-08-21, iPhone 13; PC Ethernet 192.168.1.107 ↔ iPhone Wi-Fi aynı modem

## Phase 2 — Kendi GUI'miz
- [ ] Sade ekran (M1.5): Başlat → nokta maviye döner, "Hazır" + "iPhone: Denetim Merkezi → …"
      satırı doğru adı ve IP'yi gösteriyor; iPhone'dan bağlanınca nokta yeşil, "Bağlandı" +
      cihaz adı
- [ ] Gelişmiş / Ayrıntılar açılıp kapanınca pencere düzgün büyüyüp küçülüyor, düzen bozulmuyor
- [ ] Kapat düğmesi → bildirim balonu bir kez çıkıyor, uygulama bildirim alanında kalıyor,
      sağ tık → Çıkış gerçekten kapatıyor (child uxplay.exe de gidiyor)
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
