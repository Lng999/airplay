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
- [ ] **Telefon uykusu:** yansıtma sürerken yan tuşla ekranı kapat → GUI bilerek hiçbir şey
      yapmıyor: durum `Bağlandı` (yeşil nokta) kalıyor, görüntü penceresi yerinde, son kare
      donuk; telefonu aç → görüntü kendiliğinden devam ediyor, odak çalınmıyor
      (`app/README.md` "Sleeping phone")
- [ ] Bağlıyken durum satırında gerçek kare hızı görünüyor (ör. "· 44 fps") ve makul
- [ ] Ayrıntılar günlüğünde `mirror idle:` / `mirror active:` satırları görünüyor (patches/0004
      yolu; yalnız günlüğe gidiyor, arayüz bunlara tepki vermiyor)
- [ ] Pencere başlığında, görev çubuğunda ve bildirim alanında yeni ikon görünüyor
- [ ] Telefonu 1 dakikadan uzun kapalı tut → açınca yine geri geliyor (zaman aşımı Kapalı)
- [ ] Gecikme kabul edilebilir (ms tahmini not edilsin)
- [ ] **Akıcılık:** 30 FPS ile 60 FPS arasında gözle görülür fark var mı? (Gelişmiş →
      Akıcılık; değişiklik Durdur/Başlat gerektirir). `-fps` iPhone'a `maxFPS` olarak
      gidiyor, varsayılanı 30 (`lib/raop.c:623`)
- [ ] **Görüntü çözücü:** Otomatik (d3d12h264dec) ile Direct3D 11 arasında fark var mı?
      Sink d3d11videosink olduğu için Otomatik'te her kare API sınırını geçiyor
- [ ] Ayrıntılar günlüğünde ham FPSdata XML'i **görünmüyor** (sadece ayrıştırılmış hız)
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

## Phase 2 M2 — Görüntü kendi penceremizde (`docs/PHASE2-M2-SPEC.md`)
Ajan tarafı doğrulandı: `app/tests/test_embed_live.cpp` gerçek `d3d11videosink` penceresini
bulup evlat ediniyor, oranı koruyor, X'te gizliyor, geri veriyor — ilk sürüm 15/15 PASS
(2026-08-22, videotestsrc ile); çerçeve ve gizleme kontrolleri eklendikten sonra 34/34 PASS
(2026-10-17).
Aşağıdakiler yalnız iPhone ile görülebilir:
- [ ] iPhone bağlanınca görüntü **uygulamanın penceresinde** açılıyor (masaüstünde ayrı
      "Direct3D11 renderer" penceresi kalmıyor)
- [ ] Pencere kenarından boyutlandırınca en-boy oranı kilitli, görüntü bozulmuyor, siyah
      bantlar dışında artefakt yok
- [ ] F11 (ya da Alt+Enter / çift tık / sistem menüsü) tam ekran yapıyor, Esc çıkıyor
- [ ] `Her zaman üstte` işaretliyken görüntü penceresi de üstte kalıyor
- [ ] Durum satırında çözünürlük **`Ayrıntılı günlük` kapalıyken** görünüyor
- [ ] Durum satırında bit hızı (`· 12.5 Mbps`) ve kare hızı görünüyor, makul değerler
- [ ] iPhone'dan yansıtmayı durdur → görüntü penceresi kendiliğinden kapanıyor, durum
      "Hazır"a dönüyor; tekrar bağlanınca pencere yeniden açılıyor
- [ ] Durdur → görüntü penceresi kapanıyor, masaüstünde yetim pencere kalmıyor
- [ ] Görüntü penceresini X ile kapat → oturum ve ses devam ediyor, pencere yalnızca
      gizleniyor (masaüstünde alıcının kendi penceresi açılmıyor), ilk seferde bilgi balonu
      çıkıyor; bildirim alanı simgesi sağ tık → `Görüntüyü göster` görüntüyü geri getiriyor
- [ ] `Görüntüyü uygulamada göster` kutusunu yansıtma sürerken kapat → görüntü anında
      alıcının kendi penceresine dönüyor; tekrar aç → geri alınıyor
- [ ] Görüntü penceresinin konumu/boyutu kapanışta hatırlanıyor (`[video]` bölümü)
- [ ] `Windows açılışında başlat` işaretle → oturumu kapat/aç → uygulama bildirim alanında
      açılıyor (pencere açılmıyor); kutuyu kaldır → bir daha açılmıyor

## Cihaz çerçevesi (`docs/PHASE2-M2-SPEC.md` §2b)
Ajan tarafı doğrulandı: `airplay_frame_tests` (tablo + geometri) ve `airplay_embed_live`
(gerçek d3d11videosink üzerinde gövde + çentik oyuğu + kırpma, kendi çektiği ekran
görüntüsüyle) — 2026-08-22, yeşil. Model tanımlayıcısının geldiği de eski logdan doğrulandı
(`iPhone14,5`). iPhone gerektirenler:
- [ ] Bağlanınca çerçeve çıkıyor ve **iPhone 13'e benziyor**: çentik üstte, ekrandan oyulmuş
      görünüyor, köşeler yuvarlak
- [ ] Durum satırında model **"iPhone 13"** yazıyor (`iPhone14,5` değil)
- [ ] Görüntü telefondaki ile aynı — kenarlardan kırpılmış ya da esnetilmiş değil
      (**önemli:** akışın bantlı gelip gelmediği burada anlaşılacak; günlükteki
      `video: adopted the receiver window (WxH)` satırını not et)
- [ ] Telefonu yan çevir → çerçeve de yan dönüyor. Dönmüyorsa `R` tuşu düzeltiyor mu?
- [ ] `Telefon çerçevesi çiz` kutusunu kapat → düz görüntüye dönüyor, tekrar aç → çerçeve
      geri geliyor (yansıtma sürerken)
- [ ] Tam ekranda (F11) çerçeve düzgün büyüyor, oyuk yerinde kalıyor
- [ ] Pencereyi boyutlandırınca çerçeve oranı kilitli, oyuk kaymıyor

## Phase 3 — Paket
- [ ] MSYS2 kurulu olmayan temiz bir Windows'ta self-contained klasör çalışıyor
- [ ] Autostart açıkken oturum açılışında başlıyor

- [ ] Başlat'a basmadan önce elle bir `uxplay.exe` çalıştır → Başlat onu kapatıyor, günlükte
      "terminated N stale uxplay.exe" satırı var
- [ ] GUI'yi Görev Yöneticisi'nden zorla kapat → `uxplay.exe` de gidiyor (yetim kalmıyor)

## Upstream 4e95a9e (pin `a3c19cbc` → `4e95a9e`, 2026-10-17)
Kaynak: `docs/research/upstream-delta-2026-10.md` §8, `docs/UPSTREAM-SYNC-SPEC.md`. iPhone'suz
kısım 2026-10-17'de yeşil (build 0 uyarı, smoke 12 PASS, `airplay_host_live` PASS); aşağıdakiler
iPhone ister. Her maddede iOS sürümünü not et.
- [ ] Keşif: Denetim Merkezi → Ekran Yansıtma listesinde alıcı görünüyor (`patches/0001` artık
      yok, upstream `8d62eba`). Mümkünse VMware/Hyper-V adaptörü açıkken de dene
- [ ] **Hyper-V External switch / VPN adaptörü riski:** gateway'li tek adaptörün açıklamasında
      `Virtual`/`vEthernet`/`Hyper-V`/`WSL` geçiyorsa upstream adres seçmiyor → A kaydı
      yayınlanmıyor (`mdnsd.c:374-380` @`4e95a9e`, okuyarak çıkarım). Böyle bir makinede iPhone
      alıcıyı görüyor ve bağlanabiliyor mu? (Bu makinede yok: Ethernet Realtek, VMware host-only
      adaptörlerinin gateway'i yok)
- [ ] Mirroring + **ses**: video/müzik oynatırken ses geliyor (#605 yolu, `4c86094`). `-d` ile
      kernel timestamp `(kernel)`/`(clock)` satırına bak
- [ ] iPhone'dan mirroring'i durdur → pencere kapanıyor/sıfırlanıyor (TEARDOWN, #540, `546820c`).
      Hemen yeniden başlat → görüntü bozuk değil (AES-CTR re-key, `f0b042b`)
- [ ] `-h265` kapalı ve açık: bağlantı kuruluyor mu, donuk kare var mı (#535/#607)
- [ ] Mirror idle/active ve video stats (bit hızı · kare hızı) GUI'de hâlâ geliyor (0004/0005)
- [ ] Ekran kilitli 2+ dakika: oturum ve `-reset` davranışı (`httpd` 60 sn timeout, `e60d981`)
- [ ] GUI'den durdurma temiz (Ctrl-Break, 0002); Türkçe karakterli `-n` adı çalışıyor (0003)
- [ ] Pause/resume sonrası ses geri geliyor (#539 upstream'de açık; regresyon mu bak)

## Bilinen gotcha'lar (kontrol et)
- Ağ profili **Private** olmalı; firewall UDP 5353 + UxPlay TCP/UDP portları açık
- **İki uxplay aynı anda çalışabilir** (SO_REUSEADDR): iPhone yanlış olana bağlanırsa GUI
  hiçbir olay görmez. Şüphelenirsen: `netstat -ano | findstr 7100` → bağlantının hangi PID'de
  olduğuna bak
- UDP 5353'ü Windows svchost + Spotify de dinliyor → çakışma şüphesinde Spotify'ı kapatıp tekrar dene
- `HOME`/`XDG_CONFIG_HOMEDIR` ayarlı değilse uxplay kalıcı durumunu kaybeder (launcher ayarlamalı)
