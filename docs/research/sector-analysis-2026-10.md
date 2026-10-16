# Sektör analizi: Windows için AirPlay alıcıları (2026-10)

Hazırlayan: araştırma ajanı (Opus), 2026-10-17. Kod yazılmadı, yalnızca bu dosya üretildi.
Etiketler: **[doğrulandı]** bir kaynakta veya yerel kodda görüldü · **[doğrulanamadı]** kaynak
çelişkili, üçüncü taraf ya da hiç bulunamadı · **[çıkarım]** kaynaklardan benim yorumum.
Fiyatların çoğu satıcı sayfasından değil, üçüncü taraf sitelerden geliyor; her birinde ayrıca belirtildi.

---

## 1. Özet

1. Windows'ta yerleşik AirPlay alıcısı hâlâ yok; Phone Link iPhone ekranı yansıtmıyor, Miracast iPhone'da yok [S20, S21]. Pazarın tamamını üçüncü taraf alıcılar dolduruyor.
2. Ticari taraf iki gruba ayrılıyor. Ücretli "profesyonel" alıcılar (AirServer $19.99, Reflector $19.99, Mirroring360 $14.99) bir kere satın alınıyor. Freemium yansıtıcılar (LetsView, ApowerMirror, AirDroid Cast, PigeonCast) ise filigran, oturum süresi ve 720p sınırıyla satıyor.
3. Ücretlilerin ortak özellikleri: **kayıt (+mikrofon)**, **çoklu cihaz**, **cihaz çerçevesi**, **erişim kontrolü (PIN/parola/onay)**, **tam ekranda arka plan** [S3, S5, S8].
4. Kullanıcı şikâyetlerinin çoğu protokol eksikliğinden gelmiyor. Asıl sorunlar ağ/keşif (firewall, AP izolasyonu, Bonjour), kopma ve donma, reklam ve arka plan süreçleri, imzasız kurulumun antivirüse takılması [S6, S12, S13, S16].
5. Açık kaynak tarafında aynı işi yapan en az 4 UxPlay sarmalayıcısı var: `leapbtw/uxplay-windows` 805★, AeroMirror, AirPlayPC ve `uxplay-nobonjour`. Bizim ayrıştığımız iki nokta görüntünün kendi penceremizde olması ve telefon çerçevesi. AirPlayPC de ayrı bir "çerçeve penceresi" çiziyor [S15, S17, S18].
6. **En kritik bulgu:** pinlediğimiz UxPlay (`a3c19cbc`, 2026-08-09) iOS 27 düzeltmelerinden önceki bir sürüm. Upstream'de sonradan gelenler: iOS 27 TEARDOWN düzeltmesi, Hyper-V/WSL adaptör düzeltmesi (bizim `patches/0001`'e çok benziyor), memcpy güvenlik düzeltmesi, kernel timestamp'li NTP yeniden yazımı ve MP4'ü yazarken diske basma [S10].
7. **İkinci kritik bulgu:** "kayıt kapsam dışı" kararının yeniden açılması gerekiyor. Pinli 1.74'te `-mp4` var ve kaydı **alt süreç** kendisi yapıyor (`uxplay.cpp:1581`). Kareleri bizim sürecimize almamız gerekmiyor. Aynı şekilde `-pin/-pw/-restrict/-allow/-block`, `-ca/-md` (kapak resmi + metadata dosyası), `-vrtp/-artp` (RTP ile dışarı aktarma) ve `-vsync x` de argv ile kullanılabiliyor.
8. Kullanım senaryoları beş başlıkta toplanıyor: sunum/eğitim, app demo/QA, oyun/yayın (OBS, TikTok Live), video izleme (DRM'siz) ve AirPlay hoparlör. En büyük boşluk yayıncılarda: OBS'te pencere yakalama siyah görünüyor, kayıt yok, sanal kamera yok [S11, S14, S19].
9. Teknik trendler: H.265/4K yalnız kablolu ağda ve yeni cihazlarda pratik. iOS 27 istemcileri `-h265` olmadan sorun çıkarabiliyor; upstream `-h265`'i varsayılan yapmayı tartışıyor [S9]. HDR ve gerçek AirPlay 2 ekran protokolü (legacy dışı) için açık kaynakta çalışan bir şey bulamadım [doğrulanamadı].
10. Öncelik önerisi (ilk 5): ① upstream senkronu + iOS 27/H.265 uyumu, ② bağlıyken uyku/ekran koruyucuyu engelleme, ③ tek tuşla kayıt (`-mp4`), ④ PIN/parola ekranı + cihaz izin listesi, ⑤ "kullanım amacı" ön ayarı (düşük gecikme / senkron / ağ kalitesi).

---

## 2. Rakip tablosu

| Ürün | Fiyat | Güçlü yön | Zayıf yön | Kaynak |
|---|---|---|---|---|
| **AirServer** (Windows Desktop Ed.) | Consumer **$19.99**, Edu $7.99–11.99, 30 gün deneme [doğrulandı, resmi] | AirPlay+Google Cast+Miracast, çoklu cihazda "Smart Layouts", 60 fps, MS Store'da UWP, düşük CPU iddiası | Deneme sürümü "Internet access required"; Trustpilot 2.7/5 (11 yorum). "Müşteri görüşmesinde 15 dakikada 19 kez koptu" yorumu var. Gecikme için wired/720p/"slow network" öneriyor | S1, S2, S6, S7 |
| **Reflector 4** (Squirrels) | **$19.99** (resmi sayfa), 7 gün deneme | Cihaz çerçeveleri, tüm cihazları kayıt + voiceover, bağlantı onayı (allow/deny), tam ekranda arka plan rengi/görseli, cihaz gizle/vurgula | Son Windows sürümü **4.1.2, Aralık 2024** (iPhone 16 çerçeveleri). Güncelleme seyrek, iPhone 17 çerçevesi yok [çıkarım: sayfada daha yeni sürüm yok] | S3, S4 |
| **LonelyScreen** | Çelişkili: $14.95/yıl (2020), "$15 lifetime" (2025 üçüncü taraf) [doğrulanamadı] | En basit kurulum, hesap/eşleştirme yok | Deneme sürümünde periyodik pencere/filigran; "gecikme hissedilir"; 720p iddiası [doğrulanamadı] | S12, S13, S22 |
| **Mirroring360** (Splashtop) | Standard **$11.99 kampanya / $14.99** tek sefer, Pro $29.99/yıl [doğrulandı, resmi] | Ucuz, "Mirroring Assist", MSI ile toplu dağıtım, Pro'da web link ile 40 izleyiciye yayın | Lisans makineye bağlı (tek taşıma hakkı) | S5 |
| **X-Mirage** | Resmi fiyat bulunamadı; üçüncü taraf "$20" [doğrulanamadı] | Çoklu cihaz, tek tık kayıt + mikrofon, AirPlay parolası, medya kontrol çubuğu, "4K" iddiası | Bağımsız test yok; 2020 testinde "ses gecikiyor" (AirServer için) türü yorumlar karışık | S8, S12 |
| **5KPlayer** | Ücretsiz (reklamlı) | Ücretsiz AirPlay + kayıt, medya oynatıcı | Trustpilot: reklam/pop-up, kapatıldıktan sonra arka planda çalışma, kaldırılamama, "her 10 sn ping sıçraması" | S23, S24 |
| **LetsView** (Apowersoft) | Freemium; ücretsiz sürümde 1 bağlantı, filigran kaldırma ücretli (üçüncü taraf) [kısmen doğrulanamadı] | Ücretsiz temel yansıtma, kayıt, beyaz tahta, PPT kontrolü | iPhone'da uzaktan kontrol yok; zayıf Wi-Fi'de gecikme | S25, S26 |
| **ApowerMirror** | Ücretsiz: **oturum başı 10 dk, iOS 720p, filigran**; ücretli LocalCast/AirCast [doğrulandı, resmi tablo] | 4 cihaz, USB+Wi-Fi, kayıt, ekran görüntüsü, beyaz tahta | Agresif ücretlendirme; aylık fiyat üçüncü tarafta $29.95 [doğrulanamadı] | S27, S28 |
| **AirDroid Cast** | Freemium; ~$2.99–3/ay (üçüncü taraf) [doğrulanamadı] | AirPlay alıcısı + USB, 3 bilgisayarda hesap | Hesap/abonelik modeli, birçok özellik yıllık aboneliğe bağlı | S29, S30 |
| **PigeonCast** | Ücretsiz katmanda **5 dk oturum sınırı** (satıcı beyanı) | AirPlay+Google Cast+DLNA, "4K60" iddiası, QR/PIN eşleştirme | Bağımsız inceleme yok; tüm bilgi satıcıdan | S31, S32 |
| **AirParrot 3** (gönderici) | ~$15.99–17.99 (2020 lansmanı) [doğrulanamadı] | PC → Apple TV, genişletilmiş masaüstü, tek pencere gönderme, "<16 ms" iddiası | Alıcı değil; ilgisi Reflector ile paket olması | S33 |
| **uxplay-windows** (leapbtw) | Ücretsiz, GPL; **805★, 216 fork** | Tray, x64+ARM64 MSI/ZIP, kendi mDNSResponder'ı, `arguments.txt` + ProgramData ile makine geneli yapılandırma | Ayar arayüzü yok (metin dosyası), log yok, kayıt yok (`-mp4` eklenince açılmıyordu, #38). Bilinen hata: "ilk bağlantıda donuk görüntü, yeniden bağlanınca düzeliyor". Win10'da `d3dcompiler_47` giriş noktası hatası (#41), imzasız olduğu için Defender/AV uyarısı (#6) | S15, S16 |
| **AeroMirror** | Ücretsiz, açık kaynak (0★) | Tam ekran büyük PIN overlay'i, keşif sağlığını izleyip yeniden ilan, telemetri yok | uxplay-windows 2.0 çekirdeğine bağlı; fiziksel test belgelenmemiş | S17 |
| **AirPlayPC** | Ücretsiz, açık kaynak | WPF UI, **iPhone tarzı çerçeve**, `doctor.ps1` teşhisi, PIN'i UI'da gösterme, "düşük gecikme/senkron" modları, diğer alıcıları tespit | UxPlay 1.72.1'e kilitli; betik tabanlı | S18 |
| **UxPlay** (çekirdek) | Ücretsiz, GPLv3 | Mirror + ALAC audio + HLS (YouTube), `-mp4`, `-vrtp`, PIN/parola, H.265/4K | Komut satırı; Windows'ta "MSYS2 ile derle"; bir anda tek istemci | S9, S10 |
| **scrcpy** (Android, UX referansı) | Ücretsiz | 35–70 ms gecikme, ~1 sn ilk kare, kayıt, ekran kapalıyken yansıtma, kamera, V4L2 webcam (Linux) | iOS yok | S34 |

---

## 3. Kullanıcı şikâyet kalıpları

**K1. Keşif ve ağ: "listede görünmüyor / bir süre sonra kayboluyor."**
- UxPlay #297: *"the UxPlay device will disappear from available devices after some time. Only way to reconnect is to relaunch the app."* [S9]
- uxplay-windows #34: DRM içerik sorununu araştırırken bakımcı Bonjour'u, ağ ayarlarını ve "Phone Link" gibi yazılımları suçluyor. Sonuç: *"idk, your output is fine i have no clue why it isn't working"* [S16]
- AirPlayPC'nin teşhis sırası (gerçek arıza sıklığına göre): firewall veya Public ağ profili → AP/istemci izolasyonu → farklı alt ağ → çok sayıda ağ kartı → üçüncü taraf firewall → port 7000'i tutan başka bir alıcı [S18].
- Forum: *"Stick to 5 GHz Wi-Fi, otherwise the lag is unbearable"*, *"Windows firewall might block it the first time"* [S22].

**K2. Donma ve kopma.**
- uxplay-windows #47: *"the mirrored screen on my PC frequently freezes… even though the AirPlay connection remains active"*. Bakımcının yanıtı: *"disconnect… and reconnect it, and the second time it works… yes it's a known bug"* [S16]
- UxPlay #328 (hâlâ açık): *"if the sender sends 0 FPS for too long, it will become permanently stuck"* [S9]
- AirServer, Trustpilot: *"When on client calls and sharing my screen, in the first 15 minutes it cut out 19 times."* [S6]
- UxPlay #503: suspend/resume sonrasında takılma (düzeltildi) [S9].

**K3. Gecikme ve ses-görüntü senkronu.**
- OBS forumunda AirServer ile yaklaşık **200 ms** ses-görüntü farkı ölçülmüş. Önerilen çözüm OBS'te negatif audio offset vermek [S19].
- UxPlay #169: *"The video is a bit choppy when connected via WLAN… Is it possible to set a buffer/delay?"* [S9]
- UxPlay #489: oyun kaydeden bir kullanıcı ses gecikmesi yüzünden "yalnız video" modu istiyor. Upstream bunun iOS tarafında 60 sn sonra teardown'a yol açtığını yazıyor; çözüm yok [S9].

**K4. Kayıt/yayın ihtiyacı karşılanmıyor.**
- uxplay-windows #38: *"Could you add built in recording using something like the -mp4 argument… the app won't open up."* Bakımcının yanıtı: *"OBS is a much better option"*. Kullanıcının itirazı: *"OBS captures the window not the raw input so it changes depending on the window size"* [S16]
- uxplay-windows #4 ve UxPlay #427: *"window capture in OBS only displays a black screen"*. Display Capture ve **Game Capture (belirli pencere)** ile çalışıyor [S9, S16].
- uxplay-windows #39: kullanıcı USB desteği istiyor ve TikTok Live Studio/Trovo'yu örnek veriyor [S16].

**K5. Reklam, süre sınırı, filigran (freemium).**
- *"A lot of 'free' AirPlay apps cut you off after a minute or plaster ads"* [S22]
- ApowerMirror ücretsiz sürümde 10 dk/oturum ve 720p [S27]; PigeonCast'te 5 dk [S31].
- 5KPlayer, Trustpilot: *"They will spam you with adverts… Don't install spam-ware"*; *"cannot uninstall this app in the usual way"* [S24]

**K6. Kurulum güveni.**
- uxplay-windows #6: VirusTotal/Avira kurulumu işaretliyor; sebep imzasız ikili [S16].
- uxplay-windows README'si kullanıcıya Defender'da "More info → Run anyway" demesini söylüyor [S15].

**K7. Windows'a özel uyumluluk.**
- uxplay-windows #41: Win10'da `__std_parallel_algorithms_hw_threads … d3dcompiler_47.dll` hatası (Win11 runner'da derlendiği için) [S16].
- #45: ASCII dışı profil yolunda sessizce açılmıyor. Bizim `patches/0003` aynı sınıftan bir hatayı çözmüştü.
- #15: ARM64 talebi [S16].
- UxPlay #414: D3D12 decoder bazı NVIDIA kartlarda segfault veriyor, çözümü D3D11'e zorlamak [S9]. Bizde `video_decoder` ayarı bunu kapsıyor.

**K8. Uyku ve ekran koruyucu.**
- UxPlay #71: *"prolonged idleness causes the screen to dim and get locked"* [S9]
- UxPlay #436 (açık): ekran koruyucu engelleme şu an yalnız Linux D-Bus'ta, Windows için yapılmamış [S9].
- Reflector 2.7.5 notu: *"now prevents the computer display from sleeping during YouTube video playback"* [S4]

**K9. Küçük UX sürtünmeleri.**
- uxplay-windows #10: pencere başlığı "Direct3D11 renderer" yazıyor. Bizde bu sorun yok, kendi penceremiz var.
- #28: tam ekran yok veya Alt+Enter çalışmıyor. Bizde F11 var.
- #57: `-r R` ile döndürme isteniyor (yatayda kilitli uygulamalar için).
- #3: ayar arayüzü yok [S16].

**K10. Kapsam dışı ama sık sorulanlar.** DRM (Netflix), multi-room AirPlay 2 ses (uxplay-windows #14, #34) ve iPhone'u PC'den kontrol etme (AeroMirror FAQ: mümkün değil) [S16, S17]. Bu sorular SSS'te cevaplanmalı.

---

## 4. Fikir listesi

Mimari kısıtı tekrar edeyim: görüntüyü `uxplay.exe` alt süreci çiziyor, biz yalnızca pencereyi `SetParent` ile evlat ediniyoruz. **Kareler bizim süreçte değil.** Alt sürecin argv'si yalnız açılışta okunuyor; argv'ye giren her ayar Durdur/Başlat gerektiriyor (`app/README.md`). Aşağıdaki "Kolay/Orta/Zor" notları bu kısıta göre verildi.

| # | Fikir: ne | Kime yarar | Uygulanabilirlik | Rakipte var mı | Kaynak |
|---|---|---|---|---|---|
| F1 | **Upstream senkronu.** Pin'i `a3c19cbc`'den güncel master'a taşı. Gelenler: iOS 27 TEARDOWN düzeltmesi (08-29), Hyper-V/WSL adaptör filtresi (09-03, bizim `0001` ile örtüşüyor, yama emekliye ayrılabilir), memcpy/strstr güvenlik düzeltmesi (09-03), MP4'ü yazarken diske basma (08-12), NTP yeniden yazımı ve httpd ölü bağlantı zaman aşımı (10-01) | Herkes, özellikle iOS 27 kullanıcıları | **Kolay–Orta.** `build.sh` yamaları sırayla uyguluyor. `0001` çakışacak, `0004/0005` bağlam satırları kayabilir. MANUEL iPhone testi gerekiyor | – | S10, `docs/SPEC.md` §2b |
| F2 | **H.265'i akıllı varsayılan yap** ya da iOS 27+ istemcide otomatik öner. Upstream'de bir iOS 27 istemcisi `-h265` olmadan "type 0x01 packet with no payload" ile düşüyor; bakımcı varsayılanı değiştirmeyi düşünüyor | iOS 27 ve yeni iPhone sahipleri | **Kolay** (config varsayılanı + argv). Risk: 4K'da Wi-Fi bant genişliği. `-h265` varsayılan çözünürlüğü 3840x2160 yapıyor, `-s` ile 1080p'ye sabitlenmeli | AirServer/PigeonCast "4K" iddiası | S9 (#535, #607), UxPlay README `-h265` |
| F3 | **Tek tuşla kayıt.** `-mp4 <yol>` alt süreçte çalışıyor; dosya `fn.n.H264.AAC.mp4` adıyla bağlantı başına artıyor. GUI'de "Kayıt" anahtarı, klasör seçimi ve "kayıtlar klasörünü aç". Kısıtı bozmuyor çünkü kayıt bizim süreçte yapılmıyor | İçerik üreticisi, QA, eğitimci | **Kolay–Orta.** argv olduğu için kayıt **bir sonraki bağlantıda** başlar (UI bunu söylemeli). Bilinen hata: istemci dönünce dosyanın geri kalanı bozuluyor (#606, açık) → "kayıtta döndürme kilidini aç" uyarısı. Arttırımlı yazma commit'i pin'den sonra (F1'e bağlı) | Reflector, X-Mirage, LetsView, ApowerMirror, 5KPlayer, LonelyScreen | S9 (#606), S16 (#38), `uxplay.cpp:1581` |
| F4 | **PIN/parola ekranı.** `-pin` her yeni istemci için terminale 4 haneli kod basıyor; satırı parse edip kodu büyük harflerle göster (video penceresinde veya geçici overlay'de). Alternatif olarak `-pw` ile kalıcı parola alanı | Ortak ağdaki kullanıcılar (ofis, yurt, sınıf) | **Kolay** (stdout zaten parse ediliyor). PIN satırının biçimi kaynakta doğrulanmalı | AeroMirror (tam ekran PIN), AirPlayPC, X-Mirage (parola), Reflector (security) | S17, S18, S8, UxPlay README `-pin/-pw` |
| F5 | **Cihaz izin listesi / engelleme.** Logdaki `connection request from <ad> (<model>) with deviceID = <id>` satırından "son bağlananlar" listesi çıkar; satır başına "İzin ver / Engelle" ekle → `-restrict -allow id` / `-block id`; `-reg` ile güvenilir kayıt | Ortak ağ, sınıf, ofis | **Orta.** Her değişiklik yeniden başlatma gerektiriyor. Reflector'daki gibi **canlı** "izin ver?" penceresi mümkün değil: karar alt süreçte veriliyor (`app/README.md`) | Reflector (connection prompts), Mirroring360 Pro | S3, S16, UxPlay README |
| F6 | **Bağlıyken uyku ve ekran koruyucuyu engelle.** Bağlandı durumunda `SetThreadExecutionState(ES_CONTINUOUS\|ES_DISPLAY_REQUIRED)` çağır, kopunca bırak | Video izleyen, sunum yapan | **Kolay.** Tamamen bizim süreçte, alt süreçle ilgisi yok | Reflector 2.x (YouTube sırasında); UxPlay'de yalnız Linux | S4, S9 (#71, #436) |
| F7 | **"Kullanım amacı" ön ayarı.** Sunum/oyun → `-vsync no` (kare düşürmez, düşük gecikme); Video izleme → varsayılan timestamp senkronu; ek olarak **ses gecikmesi kaydırıcısı** (`-vsync x` ms) | Oyuncu, yayıncı, film izleyen | **Kolay** (argv). Gerçek gecikme farkı bu makinede ölçülmedi [doğrulanamadı] | AirPlayPC (`-Sync` / düşük gecikme), OBS'teki elle offset | S18, S19, UxPlay README `-vsync` |
| F8 | **Ağ kalitesi ön ayarı.** `-s 1280x720@60` / `1920x1080@60` / `2560x1440@60` ve `-fps` (Düşük ağ / Dengeli / Yüksek) | Zayıf Wi-Fi kullanıcıları | **Kolay** (argv; `max_fps` zaten var) | AirServer "slow network" + 720p önerisi; uxplay-windows #63 PR önerisi | S7, S16 |
| F9 | **Ses çıkış cihazı seçici.** WASAPI cihazlarını listeleyip `-as "wasapi2sink device=<id>"` ile ver | Kulaklık/hoparlör ayıran kullanıcı, yayıncı (sanal kablo) | **Orta.** Cihaz kimliği biçimi ve `wasapi2sink`'in `device` özelliği MSYS2 GStreamer 1.28'de doğrulanmalı [doğrulanamadı] | Belirgin değil | UxPlay README `-as` |
| F10 | **AirPlay hoparlör modu ekranı.** Audio-only (ALAC) bağlantıda `-ca <dosya>` ve `-md <dosya>` ile gelen kapak resmini ve şarkı bilgisini kendi penceremizde göster. Dosyalar bizim dosya sisteminde olduğu için **kısıt yok** | Müzik dinleyen, HTPC kullanıcısı | **Kolay–Orta** (dosya izleme + çizim) | X-Mirage medya kontrol çubuğu, Reflector audio-only, AirServer multi-room | S8, S3, UxPlay README `-ca/-md` |
| F11 | **Teşhis sihirbazı.** "iPhone'umda görünmüyor" düğmesi şunları kontrol etsin: ağ profili Public mi, firewall kuralları, varsayılan ağ geçidi adaptörü ve vEthernet/Hyper-V varlığı, UDP 5353'ü dinleyen diğer süreçler (Bonjour/Spotify), TCP 7000/7100'ü tutan **başka** alıcılar (AirServer vb.; `stale_receivers` şu an yalnız kendi exe'mizi görüyor), AP izolasyonu ipucu | Yeni kullanıcı, destek yükü | **Orta.** Hepsi bizim süreçte yapılabilir. AP izolasyonu doğrudan tespit edilemez, yalnız ipucu verilir [çıkarım] | AirPlayPC `doctor.ps1`, AeroMirror keşif sağlığı | S18, S17, S9 (#297) |
| F12 | **NTP kernel timestamp'ini aç** (Gelişmiş'te, yönetici izni isteyen bir düğme): `Set-NetAdapterAdvancedProperty -DisplayName "Software Timestamp" -DisplayValue "RxAll"` | Senkron hassasiyeti isteyen | **Kolay**, ama F1'e bağlı (özellik pin'den sonra geldi). Görünür etkisi ölçülmedi [doğrulanamadı] | – | S10 (3f7b96ebdf) |
| F13 | **Monitör seçimi ve otomatik tam ekran.** "Bağlanınca şu monitörde tam ekran aç" | Projeksiyon/TV'ye bağlı PC, sunum | **Kolay.** Pencere bizim, `MonitorFromWindow` zaten kullanılıyor | Reflector tam ekran modu | S9 (#188), S3 |
| F14 | **Sunum modu.** Tam ekranda düz renk veya görsel arka plan, ortada çerçeveli telefon, imleç gizleme, isteğe bağlı cihaz adı etiketi ve "iPhone'da Odak modunu aç" hatırlatması (bildirimler de yansıyor) | Eğitimci, app demo | **Kolay.** Gövdeyi ve arka planı zaten biz boyuyoruz | Reflector (background colors/images), AirPlayPC çerçevesi | S3, S18 |
| F15 | **Çerçeve cilası.** Kasa rengi seçimi, gölge, iPad çerçeveleri, güncel model tablosu (iPhone 17 vb.). Reflector'ın son çerçeve güncellemesi iPhone 16'da kaldı, burada boşluk var | Demo/QA, içerik üreticisi | **Kolay** (`device_frames.*`) | Reflector, AirPlayPC | S4, S18 |
| F16 | **Ekran görüntüsü (spike).** Kendi penceremizi `Windows.Graphics.Capture` (`CreateForWindow`) veya `PrintWindow(PW_RENDERFULLCONTENT)` ile yakala. Evlat edinilmiş yabancı D3D11 swapchain'in bu yollarla gelip gelmeyeceği **doğrulanamadı**. OBS'te BitBlt siyah veriyor, Game Capture çalışıyor (#427) | QA, destek, sosyal medya | **Orta** (önce 1 günlük ölçüm). Çalışırsa "pikseller bizde değil" kısıtının ekran görüntüsü kısmı kalkar | ApowerMirror, LetsView | S9 (#427), S35 |
| F17 | **OBS/yayın yolu, 3 kademe.** (a) Yardım sayfası: "OBS'te Game Capture veya Windows 10 (1903+) yöntemini seç". (b) "Yayın modu": `-vrtp "config-interval=1 ! udpsink host=127.0.0.1 port=N"` ile alt süreç **yerel çizimi bırakıp** H.264 RTP gönderir; OBS'te obs-gstreamer kaynağı alır. obs-gstreamer'ın Windows derlemesi [doğrulanamadı]. (c) Uzun vade: aynı RTP'yi **kendi sürecimizde** çöz ve çiz. Böylece `uxplay_core` yazmadan kareler bizim olur; kayıt, ekran görüntüsü ve sanal kamera açılır | Yayıncı (Twitch/TikTok Live), YouTuber | (a) **Kolay**, (b) **Orta**: `-vrtp` modunda yerel görüntü yok (`video_renderer.c:367-373`), (c) **Zor** | Rakiplerin hiçbirinde doğrudan OBS kaynağı yok [çıkarım]; scrcpy'de V4L2 (Linux) | S9 (#459, #427), S16 (#4, #38), S34 |
| F18 | **Sanal kamera** (Discord/Zoom/Teams/TikTok Live Studio'da "kamera" olarak görünme) | Yayıncı, toplantı sunumu | **Zor.** Kareler gerekiyor (F17c veya F16'ya bağlı). `MFCreateVirtualCamera` **yalnız Windows 11 (build 22000+)**; hedef makine Win10 22H2 olduğu için Win10'da DirectShow tabanlı bir filtre gerekir | scrcpy (yalnız Linux V4L2); Windows rakiplerinde görülmedi [doğrulanamadı] | S36, S34 |
| F19 | **Çoklu cihaz.** İkinci bir `uxplay.exe` örneği: farklı `-n`, `-m` (MAC), `-p` aralığı; her birine ayrı pencere ya da ızgara düzeni. UxPlay tek istemcili ama çoklu örnek destekleniyor | Sınıf, oyun karşılaştırması, QA (iki cihaz) | **Orta–Zor.** `stale_receivers` ve "tek alıcı" kuralı değişmeli; firewall port aralığı, job object, ayrı `HOME` gerekiyor | AirServer, Reflector, X-Mirage, ApowerMirror (4) | S1, S3, S8, S27, UxPlay README `-m/-p` |
| F20 | **Donmuş görüntüden kurtarma.** `patches/0004` (`mirror idle/active`) hâlâ parse ediliyor ama kullanılmıyor. Uzun süreli idle + bağlı durumda "Görüntü donmuş olabilir → Yeniden bağlan" ipucu ya da tek tık "akışı yenile" (alt süreci yeniden başlatır, telefon tekrar seçmeli) | uxplay-windows #47 türü bilinen hata yaşayanlar | **Orta.** Kilitli telefonla gerçek donmayı ayırt etmek zor (`app/README.md` "Sleeping phone"). Otomatik müdahale **değil**, yalnız ipucu olmalı | – | S16 (#47), S9 (#328) |
| F21 | **Manuel döndürme ve çevirme** (`-r L/R`, `-f H/V/I`), Gelişmiş'te | Yatayda kilitli uygulama, ters monte ekran, ayna görüntü | **Kolay** (argv). Çerçevenin yön mantığıyla birlikte test edilmeli | – | S16 (#57) |
| F22 | **Bağlantı kancası.** "Bağlanınca/kopunca şu komutu çalıştır" (OBS sahnesi değiştir, amfiyi aç, Discord durumu). Bağlantı olaylarını zaten alıyoruz | İleri kullanıcı, yayıncı | **Kolay.** Bizim süreçte `CreateProcess`; güvenlik için yalnız elle yazılmış config değeri kabul edilir | – | S9 (#485) |
| F23 | **Mac'i ikinci ekran olarak kullanma ön ayarı.** `-vsync no` ve uygun `-s`. UxPlay README bu kullanımı açıkça öneriyor. Mac istemcide çerçeve zaten çizilmiyor | Mac + Windows kullanan | **Kolay** (F7'nin bir varyantı) | AirServer ("Mac'i PC'ye yansıttım") | UxPlay README `-vsync no`, S6 |
| F24 | **İngilizce arayüz.** Repo public ve rakiplerin hepsi İngilizce; `strings.h` şu an tek dil (UX-SPEC kararı) | Türkçe bilmeyen ziyaretçi | **Orta.** Bir dil tablosu ve seçim gerekiyor; UX-SPEC'teki "tek dil" kararının açıkça revize edilmesi lazım | uxplay-windows #63 (ES/EN/PT önerisi) | S16 |
| F25 | **Dağıtım güveni.** winget manifesti, kod imzalama (SmartScreen/AV yanlış pozitifi), SHA-256 yayınlama | Yeni kullanıcı | winget **Kolay**. İmza **Orta** (maliyet; açık kaynak için ücretsiz imza programlarına uygunluk [doğrulanamadı]) | uxplay-windows (winget talebi, AV şikâyeti), AirPlayPC (SHA-256) | S16 (#6, #21), S18 |
| F26 | **ARM64 derlemesi** (MSYS2 CLANGARM64) | Snapdragon dizüstü kullanıcıları | **Orta.** CI'de ARM64 runner gerekiyor, fiziksel test cihazı yok | uxplay-windows (deneysel ARM64) | S16 (#15) |
| F27 | **SSS'te "neden olmaz" bölümü:** DRM, multi-room, iPhone'u PC'den kontrol, USB, YouTube HLS. Destek yükünü azaltır, kapsam dışı kararları görünür kılar | Herkes | **Kolay** (yalnız belge) | AeroMirror FAQ | S16, S17 |

Fikirlerin dayandığı UxPlay seçeneklerinin pinli sürümde var olduğunu doğruladım (`uxplay.cpp` satırları): `-restrict` 1305, `-vsync` 1365, `-s` 1385, `-f` 1403, `-r` 1409, `-vrtp` 1541, `-artp` 1549, `-mp4` 1581, `-ca` 1616, `-md` 1629, `-pin` 1693, `-pw` 1732. Hiçbiri GUI'de şu an kullanılmıyor (`app/src` grep). Yalnız `-as` var.

---

## 5. Öncelik önerisi (etki × kolaylık)

Puanlama: Etki 1–5 (kaç kullanıcıyı ve ne kadar etkiliyor), Kolaylık 1–5 (5 = en kolay). Skor = çarpım.

| Fikir | Etki | Kolaylık | Skor |
|---|---|---|---|
| F1 Upstream senkronu (+F2 H.265) | 5 | 4 | **20** |
| F6 Uyku engelleme | 3 | 5 | **15** |
| F3 Tek tuşla kayıt (`-mp4`) | 4 | 4 | **16** |
| F4 PIN/parola ekranı (+F5 sonra) | 3 | 5 | **15** |
| F7+F8 Kullanım amacı / kalite ön ayarı | 3 | 5 | **15** |
| F11 Teşhis sihirbazı | 4 | 3 | 12 |
| F14/F15 Sunum modu + çerçeve cilası | 3 | 4 | 12 |
| F13 Monitör seçimi | 2 | 5 | 10 |
| F10 Hoparlör modu ekranı | 2 | 4 | 8 |
| F16 Ekran görüntüsü spike'ı | 3 | 2 (belirsiz) | 6 |
| F19 Çoklu cihaz | 3 | 2 | 6 |
| F17c / F18 Kareleri sürece alma, sanal kamera | 4 | 1 | 4 |

```
 Etki ↑
   5 | F1
   4 |            F3         F11            F17c/F18
   3 | F6 F4 F7/8 F14/15                 F16  F19
   2 | F13 F10
     +-------------------------------------------------→ Zorluk
       kolay                 orta                zor
```

**İlk 5:**
1. **F1 + F2: Upstream senkronu ve iOS 27 / H.265 uyumu.** Diğer her şeyin önkoşulu. iOS 27'ye geçen bir kullanıcıda ürün sessizce bozulabilir (TEARDOWN sonrası donuk kare #540, H.265 olmadan düşme #535). `0001` muhtemelen upstream'e girdiği için emekliye ayrılır. MANUEL iPhone doğrulaması şart.
2. **F3: Tek tuşla kayıt.** "Kareler bizde değil" engeli burada geçerli değil. Rakiplerin hepsinde olan bir özellik ve uxplay-windows'ta en çok istenenlerden. #606 (döndürme bozulması) için UI uyarısı eklenmeli.
3. **F6: Bağlıyken uyku/ekran koruyucu engelleme.** Birkaç satırlık iş. Upstream Windows için açık bıraktı (#436), video izleyenin ilk takıldığı yer.
4. **F4: PIN/parola ekranı** (sonra F5 izin listesi). Repo public ve kullanıcılar ortak ağlarda. Şu an aynı ağdaki herkes yansıtabiliyor. AeroMirror ve AirPlayPC bunu ana özellik olarak sunuyor.
5. **F7 + F8: "Kullanım amacı" ön ayarı** (Sunum/Oyun = düşük gecikme, Video = senkron, Zayıf ağ = 720p). Gelişmiş'teki teknik alanları (`-vsync`, `-s`, `-fps`) Türkçe, tek bir seçime indiriyor; UX-SPEC'in "sade" ilkesiyle uyumlu.

Bir sonraki dalga: F11 teşhis sihirbazı (destek yükü) ve F14/F15 sunum modu (çerçeveyi farklılaştırıcı olarak öne çıkarmak). Stratejik uzun vade: F17c (RTP'yi kendi sürecimizde çözmek). Bu yol `uxplay_core` yazmadan ekran görüntüsü, kayıt ve sanal kamerayı birlikte açar, ama yerel görüntü yolunu baştan yazmayı gerektirir.

---

## 6. Kaynak listesi

- S1 AirServer Windows Desktop Edition: https://www.airserver.com/WindowsDesktop
- S2 AirServer fiyat/indirme: https://www.airserver.com/PC/Buy/PC
- S3 Reflector özellikleri: https://www.airsquirrels.com/reflector/features/mac/?hsLang=en · https://www.airsquirrels.com/reflector
- S4 Reflector 4 Windows sürüm notları: https://airsquirrels.com/reflector/release-notes/windows
- S5 Mirroring360 fiyatları: https://www.mirroring360.com/pricing
- S6 AirServer Trustpilot: https://www.trustpilot.com/review/airserver.com
- S7 AirServer destek, gecikme: https://support.airserver.com/support/solutions/articles/43000516036-when-i-mirror-to-airserver-windows-desktop-edition-the-connection-is-very-laggy-what-can-i-do-
- S8 X-Mirage: https://www.x-mirage.com/x-mirage/
- S9 UxPlay issues: https://github.com/FDH2/UxPlay/issues (#71, #169, #188, #297, #328, #414, #427, #436, #459, #485, #489, #503, #535, #540, #606, #607)
- S10 UxPlay commit geçmişi (2026-08-09 sonrası): https://github.com/FDH2/UxPlay/commits/master (8d62eba92e, 746ec0eac2, 3f7b96ebdf vb.)
- S11 LonelyScreen: https://www.lonelyscreen.com/
- S12 2020 karşılaştırma (note.com): https://note.com/task0223/n/ne46d6b4d9406?hl=en
- S13 ofzenandcomputing rehberi: https://ofzenandcomputing.com/can-i-use-airplay-on-my-windows-pc/
- S14 MS Store AirServer: https://apps.microsoft.com/detail/9nblggh52639?gl=US&hl=en-US
- S15 uxplay-windows: https://github.com/leapbtw/uxplay-windows
- S16 uxplay-windows issues: https://github.com/leapbtw/uxplay-windows/issues (#3, #4, #6, #10, #14, #15, #21, #28, #34, #38, #39, #41, #45, #47, #57, #63)
- S17 AeroMirror: https://github.com/pyram1da/aeromirror
- S18 AirPlayPC incelemesi: https://zuphp.com/4987.html
- S19 OBS forumu, iOS ses senkronu: https://obsproject.com/forum/threads/ios-games-and-audio-sync.36227/
- S20 Microsoft Q&A, Phone Link iPhone yansıtma: https://learn.microsoft.com/en-us/answers/questions/5586798/can-i-mirror-my-iphone-screen-to-my-windows-laptop
- S21 Level1Techs forumu: https://forum.level1techs.com/t/how-can-i-screen-mirror-iphone-to-pc-on-windows-11/249003
- S22 Helpdesk forumu (kullanıcı deneyimleri): https://helpdesk.uptimemadeeasy.com/learn/can-i-mirror-iphone-to-pc-windows-11-via-wifi-5571634.aspx
- S23 5KPlayer: https://www.5kplayer.com/
- S24 5KPlayer Trustpilot: https://www.trustpilot.com/review/5kplayer.com
- S25 LetsView: https://letsview.com/
- S26 LetsView incelemesi (AirDroid, rakip kaynak): https://www.airdroid.com/screen-mirror/letsview-reviews
- S27 ApowerMirror resmi sayfa ve katman tablosu: https://www.apowersoft.com/phone-mirror
- S28 ApowerMirror fiyatı (GetApp): https://www.getapp.com/collaboration-software/a/apowermirror/pricing/
- S29 AirDroid Cast özellik tablosu: https://marketing.airdroid.com/hubfs/airdroid%20personal%20website%20materials/Cast/AirDroid%20Cast%20Feature%20Comparison%20Table_English.pdf
- S30 AirDroid Cast (AlternativeTo): https://alternativeto.net/software/airdroid-cast/about
- S31 PigeonCast ve AirScreen karşılaştırması: https://pigeoncast.com/vs/airscreen
- S32 PigeonCast Windows SSS: https://pigeoncast.com/windows-faq
- S33 AirParrot 3 (9to5Mac): https://9to5mac.com/2020/05/26/airparrot-3-released-features/
- S34 scrcpy: https://github.com/Genymobile/scrcpy (v5.0.1, 2026-10-08)
- S35 OBS Windows 10 (1903) yakalama yöntemi: https://obsproject.com/forum/threads/windows10-1903-and-up-greyed-out-and-dont-record-seperate-windows.151324/
- S36 MFCreateVirtualCamera: https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
- Yerel: `third_party/UxPlay/README.md` (seçenek açıklamaları), `third_party/UxPlay/uxplay.cpp`, `third_party/UxPlay/renderers/video_renderer.c:360-395`, `app/README.md`, `docs/SPEC.md`

### Doğrulanamayanlar (toplu)
- LonelyScreen, X-Mirage, AirDroid Cast, AirParrot ve ApowerMirror'ın aylık fiyatları (yalnız üçüncü taraf ya da eski kaynak var).
- HDR desteği: hiçbir Windows AirPlay alıcısı için güvenilir bir kaynak bulamadım.
- Reddit'teki gerçek kullanıcı başlıkları: arama araçları Reddit sonucu döndürmedi. Şikâyet alıntıları GitHub, Trustpilot ve forumlardan.
- MS Store'daki AirServer yorumlarının içeriği (yalnız "433 ratings" görüldü).
- F9 (`wasapi2sink device`), F16 (WGC/PrintWindow ile evlat edinilmiş swapchain'i yakalama) ve F17b (obs-gstreamer'ın Windows derlemesi) teknik varsayımları.
- iOS 27'nin bizim pinli derlememizde gerçekten bozulup bozulmadığı. Upstream raporları farklı istemcilerden geliyor; bizim iPhone 13'ümüzün iOS sürümünü bilmiyorum.
