# UI v0.5 SPEC — kayıt, erişim, kullanım amacı, uyku, teşhis, ilk açılış, bildirimler

Hazırlayan: arayüz tasarımcısı ajanı (Opus), 2026-10-17. Kod yazılmadı; tek çıktı bu dosya.
Girdiler: `docs/PHASE2-UX-SPEC.md` (bağlayıcı kararlar), `docs/PHASE2-M2-SPEC.md`,
`app/src/ui/strings.h`, `app/src/ui/main_window.cpp` (`createControls` :297, `layout` :387),
`docs/research/sector-analysis-2026-10.md` §3–5, `third_party/UxPlay/README.md`,
`third_party/UxPlay/uxplay.cpp`, `third_party/UxPlay/renderers/mux_renderer.c`.

Etiketler: **[KARAR]** tasarım kararı (Fable onaylarsa bağlayıcı) · **[KAYNAKTA]** kodda/README'de
görüldü, `file:line` ile · **[DOĞRULANAMADI]** bu makinede ölçülmedi · **MANUEL DOĞRULAMA
GEREKLİ** iPhone ile elle denenecek.

Bayrakların varlığı yerel derlemede de görüldü: `build\uxplay.exe -h` çıktısında `-mp4`, `-pin`,
`-reg`, `-pw`, `-vsync`, `-s wxh[@r]`, `-fps`, `-restrict`, `-allow`, `-block`, `-nohold`,
`-scrsv` satırları var (pinli sürüm `a3c19cb`, 2026-08-09).

---

## 0. Ortak zemin (tüm özelliklerin kullandığı üç parça)

Yedi özelliğin beşi aynı üç ihtiyaca çıkıyor: "şunu bilmen lazım" demek, bağlıyken ek bilgi
göstermek ve "bu değişiklik yeniden başlatma ister" demek. Her özellik kendi çözümünü
icat etmesin diye önce bunlar tanımlanıyor.

### 0.1 Bilgi şeridi (strip) — [KARAR]

Durum satırının altında, gerektiğinde beliren **tek satırlık** bir şerit. Yeni kontrol türü
değil: bir `STATIC` (metin, `SS_ENDELLIPSIS`), en çok iki `BUTTON` ve bir kapatma `BUTTON`'ı
(`×`). Zemin rengini ana pencere `WM_PAINT` içinde `FillRect` ile boyar, metnin zemini
`WM_CTLCOLORSTATIC` ile aynı fırçadan gelir (durum noktasıyla aynı yaklaşım, owner-draw yok).

```
x=14                                                        x=406
┌────────────────────────────────────────────────────────────┐  h=28
│ Ali'nin iPhone'u bağlanmak istiyor.  [İzin ver][Engelle][×]│
└────────────────────────────────────────────────────────────┘
  metin x=22, w = 392 - 8 - düğmeler      düğme 72×22, × 22×22, aralık 4
```

| Tür | Zemin | Ne zaman kaybolur |
|---|---|---|
| bilgi | açık mavi `#E8F1FB` | 8 sn sonra kendiliğinden ya da `×` |
| uyarı | açık sarı `#FFF4CE` | `×` ya da durum değişince |
| soru (düğmeli) | açık sarı `#FFF4CE` | bir düğmeye basınca ya da `×` |
| hata | açık kırmızı `#FDE7E9` | `×` ya da Başlat |

- Aynı anda **tek şerit**. Öncelik sırası: PIN kodu > bağlanma isteği > hata > kayıt uyarısı >
  "yeniden başlatınca geçerli" > bilgi. Yüksek öncelikli gelen düşüğü kuyruğa iter; kapanınca
  kuyruktaki gelir.
- Metin ≤ ~48 karakter (9 pt Segoe UI, 392 px'te iki düğmeyle sığan uzunluk). Uzun metin
  kesilir (`…`), tamamı aynı anda `logUi` ile Ayrıntılar'a yazılır.
- Şerit görünürken altındaki her şey `s(34)` aşağı kayar ve pencere `resizeToContent()` ile
  büyür. Gelişmiş/Ayrıntılar katlanmasıyla aynı mekanik.

### 0.2 Durum alt satırı (meta line) — [KARAR]

Hint satırının altında, gri, 9 pt, `h=16`. Yalnız **bağlıyken** veya **kayıt açıkken** dolu;
aksi halde boş ama yeri ayrılmış (durum değişince düzen zıplamasın diye). İçerik `·` ile
birleştirilir:

```
Ekran açık tutuluyor · ● Kayıt 03:12 · 148 MB
```

Hint satırı bugün zaten uzun (`<ad> (iPhone 13) · 1170×2532 · 12.4 Mbps · 60 fps`,
`main_window.cpp:742-760`); yeni bilgiyi oraya eklemek `SS_ENDELLIPSIS` ile kesilmesine yol
açardı. Bu yüzden ayrı satır.

### 0.3 "Yeniden başlatınca geçerli" deseni — [KARAR]

Alıcının argv'si yalnız açılışta okunuyor (UX-SPEC "M1 kısıtı"). Gelişmiş'teki alanlar bu
yüzden çalışırken gri; **bu kural değişmiyor**. Ama ana ekrana çıkan iki seçim (Kullanım,
Erişim) ve Kayıt düğmesi çalışırken de değiştirilebilmeli, yoksa ana ekranda gri combo
oturur. Desen:

| Alıcının durumu | Kullanıcı ana ekranda bir argv ayarını değiştirdi |
|---|---|
| Kapalı | Kaydedilir, sessiz. Bir sonraki Başlat'ta geçerli. |
| Hazır (bağlı cihaz yok) | **Kendiliğinden** yeniden başlatılır (~2 sn "Başlatılıyor…"). Kaybedilecek bir şey yok. Bilgi şeridi: `Ayar uygulandı.` |
| Bağlı | Soru şeridi: `Uygulamak yansıtmayı keser.` `[Şimdi uygula]` `[Sonra]`. "Sonra" → bağlantı bitince (`Waiting`'e dönünce) kendiliğinden uygulanır. |

Ortak yeniden başlatma, mevcut `host_.stop()` + `startReceiver()` çağrısı. Uygulanmayı bekleyen
değişiklik varsa meta satırına `Ayar bağlantı bitince uygulanacak` yazılır.

### 0.4 Pencere genişliği notu

Brief "~420 px" diyor; kodda en küçük genişlik `ptMinTrackSize.x = s(500)`
(`main_window.cpp:1346`) ve Gelişmiş'te `chkLogon_` zaten `x=230, w=210` ile 440'a kadar
uzanıyor. Bu spec'teki **yeni** her şey `W=420` (içerik `392`, kenar `m=14`) varsayımıyla
çizildi; yani hem 420'ye hem 500'e sığar. Mevcut minimumu değiştirmeyi önermiyorum.

### 0.5 Ortak `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kStripClose` | `×` |
| `kBtnApplyNow` | `Şimdi uygula` |
| `kBtnLater` | `Sonra` |
| `kStripApplied` | `Ayar uygulandı.` |
| `kStripApplyCuts` | `Uygulamak yansıtmayı keser.` |
| `kMetaPendingApply` | `Ayar bağlantı bitince uygulanacak` |
| `kMetaSep` | ` · ` |

---

## 1. Tek tuşla kayıt

**Amaç:** Kullanıcı tek düğmeyle yansıtmayı MP4 dosyasına kaydedebilsin ve dosyanın nerede
olduğunu, kaydın gerçekten yazılıp yazılmadığını bilsin.

### 1.1 Kaynakta ne var (tasarımı belirleyen gerçekler)

- `-mp4 [fn]`: dosya adı `fn.n.(format).mp4`, `n` her yeni bağlantıda artar
  (**[KAYNAKTA]** `README.md:1215-1219`, `uxplay.cpp:1581-1591`).
- `n` sayacı **her alıcı açılışında sıfırlanıyor**: `mux_renderer_init` içinde
  `file_count = 0` (`mux_renderer.c:71`), sonra bağlantı başına `file_count++` (`:99`). Yani
  sabit bir `fn` ile Durdur/Başlat yapan kullanıcı bir önceki `recording.1.H264.AAC.mp4`'ün
  **üstüne yazar** (`filesink location=` `:135`). → [KARAR] önek her alıcı açılışında
  zaman damgalı üretilir.
- Kayıt, bağlantı kapanınca (`conn_destroy` → `mux_renderer_stop`, `uxplay.cpp:2318-2333`) ya
  da reset döngüsünde (`uxplay.cpp:3388-3390`) kapatılır; kapatma EOS bekler, **5 saniyeye
  kadar** (`mux_renderer.c:288`). `cleanup()` (`uxplay.cpp` çıkış yolu) `mux_renderer_destroy`
  çağırmıyor.
- Bizim `UxplayHost::stop()` varsayılan `graceMs = 3000` (`uxplay_host.h:109`), sonra
  `TerminateProcess` (`uxplay_host.cpp:503-504`). `mp4mux` dosyanın dizinini (`moov`) sona
  yazar; süreç öldürülürse dosya **oynatılamaz**. Upstream'deki "MP4'ü yazarken diske bas"
  düzeltmesi pin'den sonra (analiz §4 F1/F3).
- Telefon döndürülünce dosyanın kalanı bozuluyor (UxPlay #606, açık; analiz F3).
- Klasör yazılamıyorsa alıcı açılışta `exit(1)` (`uxplay.cpp:1587-1589`):
  `"... cannot be written to: option "-mp4 <fn>" must be to a file with write access"`.

### 1.2 Kararlar

- **[KARAR] Kayıt bir "mod"dur, anlık bir eylem değil.** Düğme bir `BS_AUTOCHECKBOX |
  BS_PUSHLIKE` (basılı kalan düğme; standart kontrol). Basılıyken alıcı `-mp4` ile çalışır ve
  **her bağlantı ayrı dosyaya** kaydedilir. Bunu gizlemek yerine söylüyoruz, çünkü kaynak
  böyle çalışıyor.
- **[KARAR] Konum:** varsayılan `%USERPROFILE%\Videos\AirPlay` (`SHGetKnownFolderPath
  (FOLDERID_Videos)` + `AirPlay`, yoksa oluşturulur). Gelişmiş'te değiştirilebilir.
- **[KARAR] Önek:** her alıcı açılışında `airplay-YYYY-MM-DD_HH-MM-SS`. Ortaya çıkan dosya:
  `airplay-2026-10-17_14-05-12.1.H264.AAC.mp4`. Sondaki `.1.H264.AAC` kısmı UxPlay'in;
  değiştirilemez, kullanıcıya açıklanmaz.
- **[KARAR] Kayıt açıkken Durdur/Çıkış, `graceMs` en az 7000 ile çağrılır** (EOS'un 5 sn'lik
  bekleyişi + pay). Bu bir host çağrısı parametresi; sözleşme değişmiyor.
- **[KARAR] Kaydın gerçekten yazıldığı dosya boyutundan gösterilir.** Bağlıyken saniyede bir
  `FindFirstFileW(<klasör>\<önek>.*.mp4)` ile en yeni dosyanın boyutu okunur; büyümüyorsa
  "kayıt yazılıyor" denmez. Debug açmadan bilinebilen tek güvenilir sinyal bu
  (`Mux renderer initialized` satırı yalnız açılışı söyler, `mux_renderer.c:72`).

### 1.3 Ekran akışı

```
Kayıt kapalı ──[● Kayıt]──▶ alıcı Kapalı ise: yalnız işaretlenir
                         ├▶ Hazır ise: kendiliğinden yeniden başlar (0.3), şerit:
                         │   "Kayıt açık. Sıradaki bağlantı kaydedilecek."
                         └▶ Bağlı ise: soru şeridi:
                             "Kayıt yansıtma yeniden başlayınca başlar."
                             [Şimdi başlat] → yeniden başlat; iPhone'dan tekrar seçilmeli
                             [Sonra]        → bu bağlantı bitince

Kayıt açık + bağlantı geldi ──▶ meta: "● Kayıt 00:00 · 0 MB" (sayaç ilk MirrorStats'ta başlar)
                              ──▶ bir kerelik uyarı şeridi:
                                  "Kayıt sürerken telefonu döndürmeyin."
Kayıtta telefon döndü (görüntü penceresinin oranı değişti) ──▶ uyarı şeridi:
                                  "Telefon döndü: kaydın devamı bozuk olabilir."
Bağlantı bitti ──▶ bilgi şeridi + (pencere gizliyse) tepsi balonu:
                   "Kayıt kaydedildi · 148 MB"  [Klasörü aç]

[■ Kaydı durdur] bağlıyken ──▶ soru şeridi: "Kaydı durdurmak yansıtmayı da bitirir."
                                [Durdur] [Vazgeç]
```

Kaydı yansıtmayı kesmeden durdurmanın yolu yok: dosya yalnız bağlantı kapanınca ya da reset'te
kapanıyor (1.1). "Durdur" = alıcıyı `-mp4` olmadan yeniden başlatmak.

### 1.4 Mockup — ana ekran, bağlı ve kaydediyor (W=420, 96 dpi)

```
┌─ airplay ─────────────────────────────────────────── W=420 ┐
│ y=12   ●  Bağlandı                                         │ status 14pt kalın, h28
│ y=42      Mustafa'nın iPhone'u (iPhone 13) · 60 fps        │ hint h18
│ y=60      Ekran açık tutuluyor · ● Kayıt 03:12 · 148 MB    │ meta h16, gri
│ y=82   ┌──────────────────────────────────────────────────┐│ şerit h28 (varsa)
│        │ Kayıt sürerken telefonu döndürmeyin.          [×]││
│        └──────────────────────────────────────────────────┘│
│ y=116  Bu bilgisayarın adı:  [ AirPlay-PC           ]      │ (bugünkü yerinde)
│ y=154  [       Durdur       ]  [ ■ Kaydı durdur ]          │ 160×34 , x=182 130×34
│ ...                                                        │
```

Kayıt düğmesi: `x = m + 168 = 182`, `w = 130`, `h = 34`, toggle'ın sağında, aynı satırda.
Basılı değilken metin `● Kayıt`, basılıyken `■ Kaydı durdur`. (`●`/`■` Segoe UI'da var, sembol
fontu gerekmez — `kSecClosed` ile aynı gerekçe, `strings.h:101`.)

Görüntü penceresi başlığı kayıtta `airplay — Görüntü (kaydediliyor)`. Görüntünün **üstüne**
kırmızı nokta çizilmez: yabancı swapchain bizim boyadığımızın üstünde (M2-SPEC §2b).

### 1.5 Mockup — Gelişmiş'te kayıt satırı

```
│  Kayıt klasörü  [ C:\Users\pc\Videos\AirPlay      ] [Değiştir…]│
│  x=14 w=88       x=106 w=200 (ES_READONLY)          x=310 w=80 │
```

`Değiştir…` → `IFileOpenDialog` + `FOS_PICKFOLDERS`. Alıcı çalışırken gri (Gelişmiş kuralı).
Ayrıca ana ekrandaki kayıt düğmesine sağ tık → `Kayıt klasörünü aç` (kısa yol, opsiyonel).

### 1.6 Durumlar ve hatalar

| Durum | Gösterim |
|---|---|
| Kayıt açık, bağlantı yok | meta: `Kayıt açık: sıradaki bağlantı kaydedilecek` |
| Kaydediyor, dosya büyüyor | meta: `● Kayıt mm:ss · N MB` |
| Bağlı ama 5 sn'dir dosya büyümüyor | meta: `● Kayıt bekliyor` (telefon kilitli olabilir; hata sayılmaz) |
| Bağlı, kayıt açık, hiç dosya oluşmadı (10 sn) | uyarı şeridi: `Kayıt dosyası oluşmadı.` + `logUi` |
| Klasör yazılamıyor (alıcı `exit(1)`) | hata durumu, hint: `Kayıt klasörüne yazılamıyor` ; şerit `[Klasörü değiştir]` |
| Klasör yok | Başlat'tan önce `ensureDir` (`config_store.h:31`); başaramazsa üstteki hata |
| Disk doldu | GStreamer hatası `*** ERROR` olarak gelirse hint'e düşer; ayrı tasarım yok [DOĞRULANAMADI: mesaj biçimi] |
| Yalnız ses (ALAC, hoparlör modu) | Dosya `...1.ALAC.mp4` olarak yine yazılır (`mux_renderer.c:100-112`); meta aynı |
| Durdur sırasında 7 sn içinde kapanmadı | `stop: terminated` logu → şerit: `Kayıt yarım kalmış olabilir.` |

### 1.7 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kBtnRecord` | `● Kayıt` |
| `kBtnRecordStop` | `■ Kaydı durdur` |
| `kMetaRecArmed` | `Kayıt açık: sıradaki bağlantı kaydedilecek` |
| `kMetaRecPre` | `● Kayıt ` |
| `kMetaRecWaiting` | `● Kayıt bekliyor` |
| `kStripRecArmed` | `Kayıt açık. Sıradaki bağlantı kaydedilecek.` |
| `kStripRecNeedsRestart` | `Kayıt yansıtma yeniden başlayınca başlar.` |
| `kBtnStartNow` | `Şimdi başlat` |
| `kStripRecRotate` | `Kayıt sürerken telefonu döndürmeyin.` |
| `kStripRecRotated` | `Telefon döndü: kaydın devamı bozuk olabilir.` |
| `kStripRecSavedPre` | `Kayıt kaydedildi · ` (arkasına boyut) |
| `kBtnOpenFolder` | `Klasörü aç` |
| `kStripRecStopCuts` | `Kaydı durdurmak yansıtmayı da bitirir.` |
| `kBtnStop` | (var: `Durdur`) |
| `kBtnCancel` | `Vazgeç` |
| `kStripRecNoFile` | `Kayıt dosyası oluşmadı.` |
| `kStripRecTruncated` | `Kayıt yarım kalmış olabilir.` |
| `kHintRecDirError` | `Kayıt klasörüne yazılamıyor` |
| `kBtnChangeFolder` | `Klasörü değiştir` |
| `kLabelRecDir` | `Kayıt klasörü` |
| `kBtnBrowse` | `Değiştir…` |
| `kVideoTitleRec` | `airplay — Görüntü (kaydediliyor)` |
| `kBalloonRecSaved` | `Kayıt kaydedildi` |
| `kMenuOpenRecDir` | `Kayıt klasörünü aç` |

`config.ini`: `[receiver] record=0`, `record_dir=` (boş = Videos\AirPlay).

### 1.8 Uxplay bağlantısı

`-mp4 <klasör>\airplay-<zaman>` — `README.md:1215-1219`, `uxplay.cpp:1581-1591`. Önek bir dosya
adı, klasör değil; uzantı UxPlay tarafından eklenir.

### 1.9 Açık sorular

1. **Durdur'a basınca dosya kapanıyor mu?** Çıkış yolunda `mux_renderer_destroy` yok;
   `stop_raop_server` bağlantıları kapatıp `conn_destroy`'u tetikliyorsa kapanır, tetiklemiyorsa
   her Durdur bozuk dosya bırakır. [DOĞRULANAMADI] — ilk geliştirme adımı bu olmalı
   (`videotestsrc` olmadığı için gerçek iPhone ya da canlı test gerekir; MANUEL DOĞRULAMA
   GEREKLİ). Kapanmıyorsa `patches/0006` (cleanup'ta `mux_renderer_destroy`) gerekir.
2. Telefondan "Yansıtmayı Durdur" bağlantıyı (`conn_destroy`) bitiriyor mu, yoksa yalnız
   video akışını mı? Bitirmiyorsa dosya bir sonraki bağlantıya kadar açık kalır. MANUEL.
3. `_access()` ANSI; Türkçe karakterli bir kullanıcı klasöründe (`C:\Users\Şule\Videos`)
   `file_has_write_access` (`uxplay.cpp:325-345`) yanlış "yazılamaz" diyebilir.
   `patches/0003` argv'yi UTF-8 yapıyor ama `_access` geniş karakter değil. [DOĞRULANAMADI]
4. Upstream senkronu (F1) "yazarken diske bas" düzeltmesini getiriyor; geldiğinde
   1.6'daki "yarım kalmış olabilir" uyarısı yumuşatılabilir.
5. Rotasyon tespiti: görüntü penceresinin evlat edinilen çocuğunun en-boy oranının dikey↔yatay
   değişmesi yeterli mi, yoksa pillarbox'lı akışta (M2-SPEC §2b "yön belirsizliği") hiç
   değişmiyor mu? Değişmiyorsa 1.3'teki "Telefon döndü" uyarısı çıkmaz; yalnız ilk uyarı kalır.

---

## 2. PIN / izin listesi (erişim)

**Amaç:** Ortak ağda (ofis, yurt, sınıf) yalnız istenen cihazların yansıtabilmesi.

### 2.1 Kaynakta ne var

- `-pin [nnnn]`: yeni istemci ilk kez bağlanırken 4 haneli kod; sabit kod verilebilir. Başarılı
  eşleşme istemcide kalıcıdır (`README.md:1254-1268`, `uxplay.cpp:1693-1704`).
- `-reg [fn]`: PIN'le eşleşmiş istemcilerin kaydı (`$HOME/.uxplay.register`); satırı silmek
  kaydı düşürür. **`-reg` olmadan geri dönen istemciler denetlenmez** (`README.md:1279-1289`).
  Bizde `HOME = %APPDATA%\airplay` (`uxplay_host.h` `homeDir`).
- `-restrict` / `-allow id` / `-block id`: deviceID listesi; `-block` kısıt olmasa da geçerli
  (`README.md:1559-1578`, `uxplay.cpp:1297-1305`).
- Karar **alt süreçte, senkron** veriliyor: `report_client_request` `*admit`'i hemen yazıyor
  (`uxplay.cpp:2362-2380`). Yani "izin verilsin mi?" diye **o bağlantı için** soramayız;
  ancak reddedileni görüp listeye ekleyip yeniden başlatabiliriz.
- Ret satırları zaten ayrıştırılıyor: `LineTag::ClientRejected` (iki satır,
  `uxplay.cpp:2366-2368`), `LineTag::ClientBlocked` (`:2375`) — `line_parser.h:29-30`. Ama
  `HostEventKind`'a çıkmıyorlar (`uxplay_host.h:23-45`).
- **Hata adayı (bugün de geçerli):** `connection request from ...` satırı karar verilmeden önce
  basılıyor (`uxplay.cpp:2363`) ve host bu satırda durumu `Connected` yapıyor
  (`uxplay_host.cpp:222-226`). Reddedilen bir cihaz arayüzde "Bağlandı" görünür. Erişim
  özelliği bunu düzeltmeden yapılamaz.
- PIN zaten yakalanıyor (`HostEventKind::Pin`) ama `MessageBoxW` ile gösteriliyor
  (`main_window.cpp:1074-1078`): modal, odak çalar, eşleşme bitince de ekranda kalır.

### 2.2 Kararlar

- **[KARAR] Üç mod, ana ekranda tek combo:** `Herkes bağlanabilir` / `Kodla (PIN)` /
  `Yalnız izin verdiklerim`. Engellenen cihazlar listesi her modda geçerli (kaynak da öyle).
- **[KARAR] `-pw` (parola) v0.5'te yok.** Bkz. "Yapmamayı önerdiklerim".
- **[KARAR] PIN modu `-pin` + `-reg` birlikte.** `-reg` olmadan "Eşleşmeleri unut" yapılamaz ve
  geri dönen cihaz hiç denetlenmez; erişim modu seçmiş birinin beklediği bu değil.
- **[KARAR] PIN, durum satırının kendisi olur.** Ayrı pencere yok, MessageBox kalkar:
  `● Kod: 4819` (14 pt kalın, turuncu nokta) + hint `iPhone'daki kutuya bu kodu girin`.
  Ana pencere gizliyse (tepside) `SW_SHOWNOACTIVATE` ile öne getirilir **ve** balon atılır;
  çünkü kullanıcının telefonunda kod soran bir kutu açık ve kodu görmeden ilerleyemez.
- **[KARAR] "Bu cihaza izin ver?" = ret sonrası soru şeridi.** Canlı soru mümkün değil (2.1).
  Akış: reddedildi → şerit → `İzin ver` → listeye eklenir → alıcı yeniden başlar (bağlı kimse
  yok, kayıp yok) → şerit: `İzin verildi. iPhone'dan yeniden seçin.`

### 2.3 Ekran akışı

```
Mod: Herkes ──────────▶ bugünkü davranış (+ engellenenler yine engelli)

Mod: Kodla (PIN)
  iPhone listeden seçer ──▶ Pin olayı ──▶ ● Kod: 4819  "iPhone'daki kutuya bu kodu girin"
                                           (pencere gizliyse öne gelir + balon)
     kod doğru ──▶ ilk MirrorStats ──▶ ● Bağlandı (normal akış)
     3 yanlış  ──▶ yeni Pin olayı (README:1307-1309) ──▶ kod güncellenir
     iPhone vazgeçti / 60 sn akış yok ──▶ ● Hazır'a dön, şerit kapanır
  Daha önce eşleşmiş cihaz ──▶ kod sorulmaz, doğrudan Bağlandı

Mod: Yalnız izin verdiklerim
  izinli cihaz ──▶ Bağlandı
  izinsiz cihaz ──▶ ClientRejected ──▶ durum Hazır'da kalır, soru şeridi:
       "Ali'nin iPhone'u bağlanmak istiyor."  [İzin ver] [Engelle] [×]
       İzin ver ──▶ [allow] listesine (deviceID, ad, model) ──▶ yeniden başlat (0.3)
                ──▶ bilgi şeridi "İzin verildi. iPhone'dan yeniden seçin."
       Engelle  ──▶ [block] listesine ──▶ yeniden başlat ──▶ "Engellendi."
       ×        ──▶ hiçbir şey; bir sonraki denemede yine sorulur
  engelli cihaz (her modda) ──▶ ClientBlocked ──▶ sessiz; yalnız logUi
                                (engellenen birinin her denemede şerit açması taciz aracı olur)

Mod değiştirme: ana ekrandaki combo → 0.3 deseni.
"Yalnız izin verdiklerim"e geçerken liste boşsa ve bu oturumda bağlanmış bir cihaz varsa
  ──▶ o cihaz kendiliğinden listeye eklenir, bilgi şeridi: "<ad> izinliler listesine eklendi."
  (kullanıcının kendini dışarıda bırakmasını önler)
```

Bağlıyken izinsiz bir cihaz istek gönderirse (`-nohold` açık, `uxplay_host.h` `noHold{true}`),
ret zaten alt süreçte oluyor, mevcut oturum sürüyor. `İzin ver`'e basılırsa yeniden başlatma
mevcut oturumu keser → 0.3'teki gibi soru: `Şu anki bağlantı bitince uygulanacak.` varsayılan.

### 2.4 Mockup — ana ekran, PIN beklenirken

```
┌─ airplay ─────────────────────────────────────────── W=420 ┐
│ y=12   ●  Kod: 4819                                        │ turuncu nokta, 14pt kalın
│ y=42      iPhone'daki kutuya bu kodu girin                 │
│ y=60      Mustafa'nın iPhone'u bağlanıyor                  │ meta
│ y=82   Bu bilgisayarın adı:  [ AirPlay-PC           ]      │
│ y=120  [       Durdur       ]  [ ● Kayıt ]                 │
│ y=168  Kullanım:  [ Genel ve video (önerilen)       ▾ ]    │ etiket w70, combo x=88 w230
│ y=200  Erişim:    [ Kodla (PIN)                     ▾ ]    │
│ ...                                                        │
```

Kodu 14 pt'den büyük göstermeyi düşündüm (AeroMirror tam ekran gösteriyor, analiz S17). Gerek
yok: kullanıcı kodu bilgisayarın başında, 40 cm'den okuyor; projeksiyon senaryosunda da kod
ana pencerede, görüntü penceresinde değil (görüntü penceresi henüz yok).

### 2.5 Mockup — Gelişmiş'te erişim bölümü

```
│  Sabit kod  [ ____ ]   boş = her bağlantıda yeni kod              │ x=14 w=70 / x=88 w=60
│                                                                   │ ES_NUMBER, EM_LIMITTEXT 4,
│                                                                   │ EM_SETCUEBANNER
│  Cihazlar                                                         │
│  ┌─────────────────────────────────────────────────┐ [ Kaldır  ] │ LISTBOX x=14 w=290 h=68
│  │ İzinli · Mustafa'nın iPhone'u (iPhone 13)        │ [Eşleşme- │ │ (4 satır), düğmeler
│  │ İzinli · iPad (iPad13,1)                         │ [leri unut]│ │ x=310 w=96 h=24
│  │ Engelli · Bilinmeyen iPhone                      │            │
│  └─────────────────────────────────────────────────┘            │
```

- `Sabit kod` yalnız PIN modunda etkin; `-pin nnnn`'e gider (`uxplay.cpp:1696-1702`).
- `Kaldır` seçili satırı `[allow]`/`[block]`'tan siler.
- `Eşleşmeleri unut` → `%APPDATA%\airplay\.uxplay.register` dosyasını siler (onay kutusu ile:
  `Tüm eşleşmiş cihazlar yeniden kod girecek. Devam edilsin mi?`).
- Hepsi alıcı çalışırken gri (Gelişmiş kuralı); ana ekrandaki mod combo'su değil (0.3).

### 2.6 Durumlar ve hatalar

| Durum | Nokta | Status | Hint |
|---|---|---|---|
| PIN bekleniyor | turuncu | `Kod: 4819` | `iPhone'daki kutuya bu kodu girin` |
| PIN olayı geldi ama rakam çözülemedi (`Pin` boş, `uxplay_host.cpp:168` yorum) | turuncu | `Kod bekleniyor` | `Kod Ayrıntılar'da` + Ayrıntılar kendiliğinden açılır |
| Reddedildi (liste modu) | mavi (Hazır) | `Hazır` | normal hint; soru şeridi |
| Engellendi | mavi | `Hazır` | normal hint; şerit yok |
| Sabit kod 4 haneden kısa | — | — | alan kırmızı çerçeve yok (GDI); Başlat'ta uyarı şeridi `Sabit kod 4 haneli olmalı.` ve kod yok sayılır |
| Liste modunda deviceID gelmedi (eski istemci) | — | — | `İzin ver` düğmesi gösterilmez; şerit: `Bu cihaz tanınamadı.` |

### 2.7 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kLabelAccess` | `Erişim:` |
| `kAccessAll` | `Herkes bağlanabilir` |
| `kAccessPin` | `Kodla (PIN)` |
| `kAccessList` | `Yalnız izin verdiklerim` |
| `kStatePinPre` | `Kod: ` |
| `kStatePinWaiting` | `Kod bekleniyor` |
| `kHintPin` | `iPhone'daki kutuya bu kodu girin` |
| `kHintPinInLog` | `Kod Ayrıntılar'da` |
| `kMetaConnectingSuf` | ` bağlanıyor` |
| `kStripAskSuf` | ` bağlanmak istiyor.` |
| `kBtnAllow` | `İzin ver` |
| `kBtnBlock` | `Engelle` |
| `kStripAllowed` | `İzin verildi. iPhone'dan yeniden seçin.` |
| `kStripBlockedSuf` | ` engellendi.` |
| `kStripAllowLater` | `Şu anki bağlantı bitince uygulanacak.` |
| `kStripAutoAllowedSuf` | ` izinliler listesine eklendi.` |
| `kStripUnknownDevice` | `Bu cihaz tanınamadı.` |
| `kStripPinTooShort` | `Sabit kod 4 haneli olmalı.` |
| `kLabelFixedPin` | `Sabit kod` |
| `kCueFixedPin` | `boş = her seferinde yeni` |
| `kLabelDevices` | `Cihazlar` |
| `kDevAllowedPre` | `İzinli · ` |
| `kDevBlockedPre` | `Engelli · ` |
| `kBtnRemove` | `Kaldır` |
| `kBtnForgetPaired` | `Eşleşmeleri unut` |
| `kAskForgetPaired` | `Tüm eşleşmiş cihazlar yeniden kod girecek. Devam edilsin mi?` |
| `kBalloonAskTitle` | `Bağlanma isteği` |
| `kPinTitle` | (var: `Eşleştirme kodu`) — artık balon başlığı |

`config.ini`:
```ini
[receiver]
access=all            ; all | pin | list
pin_fixed=            ; boş = rastgele
[allow]
46:F2:E9:84:EA:27=mustafanın iPhone|iPhone14,5
[block]
```

### 2.8 Uxplay bağlantısı

| Mod | argv |
|---|---|
| Herkes | (yok) + her `[block]` için `-block id` |
| Kodla | `-pin` veya `-pin nnnn` + `-reg` + `-block ...` — `README.md:1254`, `:1279` |
| Yalnız izinliler | `-restrict` + her `[allow]` için `-allow id` + `-block ...` — `README.md:1559-1578` |

### 2.9 Geliştiriciye ön koşullar (UI'nin dayandığı host değişiklikleri)

1. `ClientRejected` / `ClientBlocked` → yeni `HostEventKind::ClientDenied` (deviceID + blocked
   bayrağı) ve **durum `Connected` olmamalı** (ya `ClientInfo`'da durum değişimi ertelenir ya da
   ret gelince `Waiting`'e geri dönülür). `airplay_host_tests`'e regresyon.
2. PIN modunda `ClientInfo` → `Connected` geçişi de erken: kod girilmeden "Bağlandı" yazar. UI
   tarafında `Pin` olayı gelmişse ilk `MirrorStats`'a kadar "Kod" durumunda kalınır.

### 2.10 Açık sorular

1. Varsayılan mod ne olsun? Ben **Herkes** öneriyorum (ilk açılış 30 sn hedefi, bkz. §6);
   ama repo public ve `-nohold` açık: aynı ağdaki biri **mevcut oturumu devralabilir**
   (`README.md:1555-1557`). PIN modunda `-nohold`'u kapatmak mantıklı mı? Fable kararı.
2. `-reg` ile kayıt dosyasından silinen bir cihaz yeniden PIN'e mi düşer, yoksa reddedilir mi?
   README "deregisters" diyor, sonucunu söylemiyor. MANUEL DOĞRULAMA GEREKLİ.
3. PIN satırının iOS 27'de hâlâ aynı yoldan geldiği upstream senkronundan sonra yeniden
   kontrol edilmeli (analiz F1).
4. deviceID iOS'un "özel Wi-Fi adresi"nden bağımsız mı? README "appears to be immutable" diyor
   (`README.md:1562-1568`), kesin değil. MANUEL: aynı iPhone farklı ağda aynı ID'yi veriyor mu?

---

## 3. Kullanım amacı ön ayarı

**Amaç:** Gelişmiş'teki teknik alanları (`-vsync`, `-s`, `-fps`) kullanıcının diliyle tek bir
seçime indirmek.

### 3.1 Kararlar

- **[KARAR] Dört seçenek, ana ekranda tek combo.** Brief'teki "Sunum" ve "Oyun" aynı argv'ye
  çıkıyor (`-vsync no`); iki ayrı satır olarak sunmak farkı olmayan bir seçim yaptırır. Birleşik.

| Seçenek (ekranda) | `config` | `-vsync` | `-s` | `-fps` | Gerekçe |
|---|---|---|---|---|---|
| Genel ve video (önerilen) | `general` | (varsayılan = açık) | (varsayılan 1920x1080) | 60 | Bugünkü davranış; film/YouTube'da ses-görüntü eşleşir |
| Sunum ve oyun (düşük gecikme) | `live` | `no` | (varsayılan) | 60 | "no video frames are dropped", ikinci ekran/canlı kullanım için önerilen (`README.md:1319-1324`) |
| Zayıf Wi-Fi (720p) | `weak` | (varsayılan) | `1280x720@60` | 30 | Daha az veri; `-s`'de yükseklik belirleyici (`README.md:1363-1375`) |
| Özel (Gelişmiş'teki ayarlar) | `custom` | Gelişmiş'ten | Gelişmiş'ten | Gelişmiş'ten | — |

- **[KARAR] Ön ayar, Gelişmiş'teki alanları doldurur; ayrı bir gizli katman değildir.**
  Ön ayar seçilince Gelişmiş'teki `Akıcılık`, yeni `Çözünürlük` ve yeni `Ses-görüntü eşitleme`
  combo'ları o değerlere çekilir (görünür değişiklik; kullanıcı ne olduğunu görebilir).
  Gelişmiş'te bu üç alandan biri elle değişirse ana ekrandaki combo kendiliğinden `Özel` olur.
  Argv yine tek kaynaktan, alanlardan üretilir → `Komutu kopyala` doğru kalır.
- **[KARAR] H.265 ön ayarın parçası değil.** `-h265` varsayılan çözünürlüğü 3840x2160'a
  çeker (`README.md:1210-1213`); `weak` seçiliyken `-s 1280x720@60` açıkça verildiği için
  çakışma yok, `general`'da H.265 açıksa 4K istenir — bu bugünkü davranış, değiştirmiyorum.

### 3.2 Ekran akışı

Combo değişir → Gelişmiş alanları güncellenir → 0.3 deseni (kapalıysa sessiz, hazırsa
kendiliğinden yeniden başlat, bağlıysa sor).

### 3.3 Mockup — ana ekran (Kapalı, varsayılan, şerit yok)

```
┌─ airplay ─────────────────────────────────────────── W=420 ┐
│ y=12   ●  Kapalı                                           │
│ y=42      Alıcıyı başlatmak için Başlat'a basın            │
│ y=60                                                       │ meta (boş, yeri ayrılı)
│ y=82   Bu bilgisayarın adı:  [ AirPlay-PC           ]      │ lbl w132 / edit x=150 w180
│ y=120  [       Başlat       ]  [ ● Kayıt ]                 │ 160×34 / x=182 130×34
│ y=168  Kullanım:  [ Genel ve video (önerilen)       ▾ ]    │ lbl y+5 w70 / combo x=88 w230
│ y=200  Erişim:    [ Herkes bağlanabilir             ▾ ]    │
│ y=238  iPhone beni görmüyor                                │ SysLink h18 (bkz. §5)
│ y=264  ► Gelişmiş                                          │ secAdvanced (bugünkü)
│ y=290  ► Ayrıntılar                                        │ secDetails  (bugünkü)
└────────────────────────────────────────────────────────────┘  istemci yüksekliği ≈ 330
```

Bugünkü kapalı pencere ~300 px; artış ~30 px + iki combo. Ana ekranda hâlâ hiçbir GStreamer
adı, port veya yol yok (UX-SPEC kabul kriteri korunuyor).

### 3.4 Mockup — Gelişmiş'in görüntü grubu (yeni iki satır)

```
│  Port [ 7100 ]   Akıcılık [ 60 FPS (akıcı)        ▾]         │ (bugünkü satır)
│  Çözünürlük          [ 1080p (varsayılan)     ▾]             │ lbl w120 / combo x=138 w180
│  Ses-görüntü eşitleme [ Açık (varsayılan)      ▾]            │
│  Görüntü çıkışı [ ... ]                                      │ (bugünkü satırlar devam)
```

### 3.5 Durumlar ve hatalar

- Alıcı istemcinin isteğine uymayabilir: `-s` ve `-fps` "yalnızca istek" (`README.md:1366`,
  `:1591-1593`). Bu bir hata değil; arayüz "720p'de" diye **iddia etmez**, yalnız
  bağlıyken gerçek çözünürlüğü (zaten hint'te) gösterir.
- Config'de bilinmeyen `preset` değeri → `custom` sayılır.

### 3.6 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kLabelPreset` | `Kullanım:` |
| `kPresetGeneral` | `Genel ve video (önerilen)` |
| `kPresetLive` | `Sunum ve oyun (düşük gecikme)` |
| `kPresetWeak` | `Zayıf Wi-Fi (720p)` |
| `kPresetCustom` | `Özel (Gelişmiş'teki ayarlar)` |
| `kLabelResolution` | `Çözünürlük` |
| `kRes1080` | `1080p (varsayılan)` |
| `kRes720` | `720p (az veri)` |
| `kLabelSync` | `Ses-görüntü eşitleme` |
| `kSyncOn` | `Açık (varsayılan)` |
| `kSyncOff` | `Kapalı (düşük gecikme)` |

`config.ini`: `[receiver] preset=general`, `vsync=1`, `resolution=` (boş = varsayılan, `720`).

### 3.7 Uxplay bağlantısı

`-vsync no` — `README.md:1319-1324`, `uxplay.cpp:1365`. `-s wxh@r` — `README.md:1363-1375`.
`-fps n` — `README.md:1585-1593` (mevcut `maxFps`).

### 3.8 Açık sorular

1. "Düşük gecikme" iddiası ölçülmedi (analiz F7 [doğrulanamadı]). Etiketten "(düşük gecikme)"
   kalkmalı mı, yoksa MANUEL bir ölçümle (ekranda kronometre, telefon+PC aynı karede fotoğraf)
   desteklenmeli mi? Ölçülmeden yayınlanırsa iddia bizim değil UxPlay README'sinin.
2. Zayıf Wi-Fi'de `-fps 30` mu, 60 mı? 720p@60 bant genişliği 1080p@30'a yakın olabilir.
   [DOĞRULANAMADI] — bit hızı göstergesi (patches/0005) ile ölçülebilir, MANUEL.
3. 1440p/4K seçeneği eklenmeli mi? `-s` h>1080 ancak H.265 ile anlamlı (`README.md:1208-1210`).
   Önerim: v0.5'te yok; H.265 kutusu zaten 4K'yı açıyor.

---

## 4. Bağlıyken uyku / ekran koruyucu engelleme

**Amaç:** Yansıtma sürerken bilgisayarın ekranı kararmasın, uykuya geçmesin.

### 4.1 Kararlar

- **[KARAR] Otomatik, varsayılan açık; kutu Gelişmiş'te.** Ana ekranda kutu yok: kimse
  "yansıtırken ekran kararsın" istemez. Kapatmak isteyen (pil) Gelişmiş'te
  `Bağlıyken ekranı açık tut` kutusunu kaldırır.
- **[KARAR] Gösterge = meta satırı:** `Ekran açık tutuluyor`. Tepsi ipucu metnine de eklenir.
- **[KARAR] "Bağlı" değil, "görüntü akıyor" ölçüt.** `resetSeconds` varsayılanı 0
  (`config_store.h` `resetSeconds = 0`): kilitli telefonla oturum sonsuza dek "Bağlı" kalabilir.
  `Connected` ölçüt olsaydı cebinde kilitli telefon olan biri bilgisayarının hiç uyumamasıyla
  karşılaşırdı. Kural:

| Durum | İstek |
|---|---|
| Bağlı + görüntü akıyor (`MirrorActivity = 1` ya da son 5 sn'de `MirrorStats fps > 0`) | ekran açık + sistem açık |
| Bağlı + yalnız ses (ALAC/hoparlör, `AudioFormat usingScreen=0`, `line_parser.cpp:307`) | yalnız sistem açık (ekran kararabilir, müzik sürer) |
| Bağlı + görüntü 2 dk'dır durmuş (telefon kilitli) | bırakılır |
| Hazır / Kapalı | bırakılır |

- **[KARAR] API: `PowerCreateRequest` + `PowerSetRequest(PowerRequestDisplayRequired /
  PowerRequestSystemRequired)`**, `SetThreadExecutionState` değil. Gerekçe: neden metni
  (`AirPlay yansıtma sürüyor`) `powercfg /requests` çıktısında görünür → teşhis raporuna da
  girer ve kullanıcı "bilgisayarım neden uyumuyor" diye sorduğunda cevap kendini söyler.
  (`-scrsv` Windows'ta işlemiyor: "only implemented on Linux/*BSD ... D-Bus",
  `README.md:1242-1244`.)

### 4.2 Mockup

Ana ekranda ek kontrol yok; meta satırı (§1.4). Gelişmiş'te mevcut kutu ızgarasına bir kutu:

```
│  [x] Görüntüyü uygulamada göster   [x] Windows açılışında başlat │ (bugünkü)
│  [x] Telefon çerçevesi çiz         [x] Bağlıyken ekranı açık tut │ x=230 w=180 (yeni)
```

`chkFrame_` satırında sağ yarı boş (`main_window.cpp:448`); yeni kutu oraya oturur, yükseklik
artmaz.

### 4.3 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kChkKeepAwake` | `Bağlıyken ekranı açık tut` |
| `kMetaAwake` | `Ekran açık tutuluyor` |
| `kMetaAwakeAudio` | `Bilgisayar açık tutuluyor` |
| `kPowerReason` | `AirPlay yansıtma sürüyor` |

`config.ini`: `[app] keep_awake=1`.

### 4.4 Uxplay bağlantısı

Yok; tamamen bizim süreç. (Neden `-scrsv` değil: `README.md:1242-1244`.)

### 4.5 Açık sorular

1. MS belgeleri `SetThreadExecutionState` için "ekran koruyucuyu durdurmaz" diyor; display
   power request'in ekran koruyucuyu engellediği yaygın gözlem ama **bu makinede ölçülmedi**.
   MANUEL: ekran koruyucu 1 dk, yansıtmada video oynat, 2 dk bekle.
2. Kurumsal "kilit ekranı zaman aşımı" politikası (GPO) bu istekle engellenmez; belgelenmeli mi?
3. `AudioFormat` olayı host'tan UI'ya çıkmıyor; yalnız ses tespiti için yeni olay mı, yoksa
   "görüntü penceresi yok + bağlı" yeterli bir vekil mi?

---

## 5. Teşhis sihirbazı ("iPhone beni görmüyor")

**Amaç:** Şikâyet kalıbı K1'i (keşif/firewall, analiz §3) kullanıcının kendi başına, tek
pencerede çözmesi; çözemezse yapıştırılabilir bir rapor vermesi.

### 5.1 Kararlar

- **[KARAR] Giriş noktası:** ana ekranda bir `SysLink` (comctl v6 manifestte var,
  `app/res/app.manifest:9-10`; `ICC_LINK_CLASS` eklenmeli, `main.cpp:53`): `iPhone beni
  görmüyor`. Görünür olduğu durumlar: Hazır, Hata, Kapalı. Bağlıyken gizli. Tepsi menüsüne de
  `Bağlantı kontrolü` olarak eklenir.
- **[KARAR] Ayrı küçük pencere** (`airplay — Bağlantı kontrolü`, 420×~360, ana pencereye owned,
  modal değil). UX-SPEC "ayrı dialog yok" kararı **ayarlar** içindi; bu bir ayar değil,
  geçici bir araç. Ana pencereye katlanır bölüm olarak koymak onu 700 px'e uzatırdı.
- **[KARAR] Kontroller açılınca kendiliğinden, arka iş parçacığında koşar** (~1 sn). Her satırda
  durum noktası (ana penceredeki `Ellipse`'in aynısı: yeşil/sarı/kırmızı/gri), kısa metin ve
  gerekiyorsa düğme.
- **[KARAR] Tek UAC, tek düğme:** yönetici gerektiren düzeltmeler (firewall kuralı, ağ
  profilini Özel yapma) `Düzelt (yönetici izni)` düğmesinde toplanır. Uygulama kendini
  `ShellExecuteW(..., L"runas", airplay-gui.exe, L"--fix-network <port> <uxplay.exe yolu>")`
  ile yükseltilmiş ikinci kopya olarak çalıştırır; o kopya pencere açmadan işi yapıp çıkar.
  Kurallar `scripts/firewall-rules.ps1` ile **aynı içerikte** ve aynı `airplay:` önekiyle
  (betikle çakışmasın, biri diğerini temizleyebilsin). Ağ profilini değiştirmek her zaman
  ayrı onay ister, sessizce yapılmaz.

### 5.2 Kontroller (sıra, AirPlayPC'nin gözlenen arıza sıklığına göre — analiz §3 K1, S18)

| # | Kontrol | Nasıl (hepsi bizim süreçte) | Yeşil | Sarı/Kırmızı + eylem |
|---|---|---|---|---|
| 1 | Alıcı çalışıyor mu | `state_ == Waiting/Connected` | `Alıcı çalışıyor` | K: `Alıcı kapalı` `[Başlat]` |
| 2 | Hangi ağdayım | `GetAdaptersAddresses` (varsayılan ağ geçidi olan bağdaştırıcı) + `WlanQueryInterface` (SSID) | `Wi-Fi «EvAğı» · 192.168.1.107` | bilgi satırı: `iPhone da şu ağda olmalı: «EvAğı»` |
| 3 | Ağ türü | `INetworkListManager` → `GetCategory` | `Ağ türü: Özel` | K: `Ağ türü: Ortak — Windows engelliyor` → Düzelt |
| 4 | Güvenlik duvarı | `INetFwPolicy2` kuralları: `uxplay.exe` için **Engelle** kuralı (kullanıcı ilk izin penceresinde İptal'e bastıysa Windows bunu yaratır) ya da etkin profilde izin yok | `Güvenlik duvarı izin veriyor` | K: `Güvenlik duvarı engelliyor` / S: `Kural bulunamadı` → Düzelt |
| 5 | Keşif servisi (mDNS) | `GetExtendedUdpTable` → UDP 5353 sahipleri; bizim çocuk PID'imiz aralarında mı | `Keşif servisi yayında` | K: `Keşif servisi çalışmıyor` `[Yeniden başlat]`; bilgi: `5353'ü paylaşanlar: Spotify, mDNSResponder` |
| 6 | Port çakışması | `GetExtendedTcpTable` → bizim port (7100..7102) ve 7000'i dinleyen **başka** süreç | (satır gizli) | K: `Port başka bir uygulamada: AirServer.exe` — kapatmasını söyle; biz kapatmayız (`stale_receivers` yalnız `uxplay.exe` öldürür, `stale_receivers.h:16-19`) |
| 7 | Birden çok bağdaştırıcı | aktif + ağ geçidi olan >1 bağdaştırıcı, VPN/`vEthernet` adları | (gizli) | S: `VPN ya da sanal ağ açık: ...` — `patches/0001` bunun bir kısmını çözüyor, uyarı yine de |
| 8 | Ağ yalıtımı | **tespit edilemez** (analiz F11 [çıkarım]) | — | her zaman en altta gri ipucu: `Misafir Wi-Fi'ları cihazları birbirinden ayırabilir.` |

### 5.3 Mockup

```
┌─ airplay — Bağlantı kontrolü ─────────────────────── W=420 ┐
│ y=12  ●  Alıcı çalışıyor                                   │ nokta 10px x=14, metin x=32
│ y=36  ●  Wi-Fi «EvAğı» · 192.168.1.107                     │ satır h=24
│ y=52        iPhone da şu ağda olmalı: «EvAğı»              │ alt metin gri h16
│ y=76  ●  Ağ türü: Ortak — Windows engelliyor               │ kırmızı
│ y=100 ●  Güvenlik duvarı engelliyor                        │ kırmızı
│ y=124 ●  Keşif servisi yayında                             │
│ y=140       5353'ü paylaşanlar: Spotify                    │
│ y=164 ●  Misafir Wi-Fi'ları cihazları birbirinden ayırabilir│ gri
│ ────────────────────────────────────────────────────────── │
│ y=200 2 sorun bulundu.                                     │ kalın
│ y=228 [ Düzelt (yönetici izni) ]  [ Yeniden dene ]         │ 170×30 / x=192 110×30
│ y=268 [ Raporu kopyala ]                       [ Kapat ]   │ 130×28 / x=326 80×28
└────────────────────────────────────────────────────────────┘
```

Her şey yeşilse orta bölüm: `Sorun bulunamadı.` + `iPhone'da Ekran Yansıtma listesini kapatıp
yeniden açın.` (iOS listeyi önbellekliyor; analiz K1 #297 "bir süre sonra kayboluyor").

### 5.4 Rapor (panoya kopyalanan metin)

Düz metin, İngilizce anahtarlar (log dili), Türkçe değil: destek için GitHub issue'suna
yapıştırılacak. İçerik: uygulama sürümü, Windows build, her kontrolün sonucu, bağdaştırıcı
listesi (ad, IPv4, ağ geçidi var/yok), ağ türü, firewall özet, 5353/7000/7100 sahipleri (süreç
adı), `config.ini` argv (`Komutu kopyala` çıktısı), `powercfg /requests`'teki bizim satırımız,
son 40 log satırı. **Kullanıcı adı ve bilgisayar adı yerine `<user>`/`<pc>` yazılır**; yerel IP
kalır (teşhis için gerekli, internete açık değil).

### 5.5 Durumlar ve hatalar

| Durum | Gösterim |
|---|---|
| Kontroller sürüyor | her satırda gri nokta + `Kontrol ediliyor…`, düğmeler gri |
| UAC reddedildi | alt satır: `Yönetici izni verilmedi.` (hata değil, bilgi) |
| Düzeltme bitti | kontroller kendiliğinden yeniden koşar |
| Firewall okunamadı | sarı: `Güvenlik duvarı okunamadı` — "engelliyor" demeyiz |
| Üçüncü taraf güvenlik duvarı (Windows'unki kapalı) | sarı: `Başka bir güvenlik duvarı kullanılıyor` (`INetFwPolicy2` profil `FirewallEnabled=false` + WSC kaydı) [DOĞRULANAMADI] |
| Rapor kopyalandı | alt satır: `Rapor panoya kopyalandı.` |

### 5.6 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kLinkDiag` | `<a>iPhone beni görmüyor</a>` (SysLink biçimi) |
| `kTrayDiag` | `Bağlantı kontrolü` |
| `kDiagTitle` | `airplay — Bağlantı kontrolü` |
| `kDiagChecking` | `Kontrol ediliyor…` |
| `kDiagReceiverOk` | `Alıcı çalışıyor` |
| `kDiagReceiverOff` | `Alıcı kapalı` |
| `kDiagWifiPre` | `Wi-Fi ` |
| `kDiagWired` | `Kablolu ağ` |
| `kDiagSameNetPre` | `iPhone da şu ağda olmalı: ` |
| `kDiagProfilePrivate` | `Ağ türü: Özel` |
| `kDiagProfilePublic` | `Ağ türü: Ortak — Windows engelliyor` |
| `kDiagFwOk` | `Güvenlik duvarı izin veriyor` |
| `kDiagFwBlocked` | `Güvenlik duvarı engelliyor` |
| `kDiagFwMissing` | `Kural bulunamadı` |
| `kDiagFwUnknown` | `Güvenlik duvarı okunamadı` |
| `kDiagFwThirdParty` | `Başka bir güvenlik duvarı kullanılıyor` |
| `kDiagMdnsOk` | `Keşif servisi yayında` |
| `kDiagMdnsFail` | `Keşif servisi çalışmıyor` |
| `kDiagMdnsSharedPre` | `5353'ü paylaşanlar: ` |
| `kDiagPortBusyPre` | `Port başka bir uygulamada: ` |
| `kDiagMultiNicPre` | `VPN ya da sanal ağ açık: ` |
| `kDiagIsolation` | `Misafir Wi-Fi'ları cihazları birbirinden ayırabilir.` |
| `kDiagProblemsSuf` | ` sorun bulundu.` |
| `kDiagAllOk` | `Sorun bulunamadı.` |
| `kDiagAllOkHint` | `iPhone'da Ekran Yansıtma listesini kapatıp yeniden açın.` |
| `kBtnFix` | `Düzelt (yönetici izni)` |
| `kBtnRecheck` | `Yeniden dene` |
| `kBtnCopyReport` | `Raporu kopyala` |
| `kBtnClose` | `Kapat` |
| `kBtnRestartReceiver` | `Yeniden başlat` |
| `kDiagUacDeclined` | `Yönetici izni verilmedi.` |
| `kDiagCopied` | `Rapor panoya kopyalandı.` |
| `kAskMakePrivate` | `Bu ağ «Özel» olarak işaretlenecek. Yalnız evinizdeki ya da güvendiğiniz bir ağda yapın. Devam edilsin mi?` |

### 5.7 Uxplay bağlantısı

Bayrak yok. Port bilgisi `HostConfig.port` (`-p n` → n..n+2, `uxplay_host.h` yorum) ve
`Ports` olayından; firewall kural içeriği `scripts/firewall-rules.ps1:100-150`.

### 5.8 Açık sorular

1. `INetFwPolicy2` kurallarını **yönetici olmadan** okumak mümkün mü? `Get-NetFirewallRule`
   normal kabukta "access denied" veriyor (`firewall-rules.ps1:10-11`) ama o CIM yolu; COM yolu
   [DOĞRULANAMADI]. Okunamıyorsa kontrol 4 her zaman "okunamadı" der ve sihirbazın en değerli
   satırı boşa gider → ilk geliştirme adımı 10 dakikalık bir ölçüm olmalı.
2. Windows'un ilk izin penceresi hangi profili varsayılan işaretliyor (yalnız Özel mi, mevcut
   ağın türü mü)? §6'nın uyarı metni buna bağlı. [DOĞRULANAMADI], MANUEL.
3. Kendini `runas` ile yükselten ikinci kopya `single_instance` korumasına takılır mı
   (`single_instance.h`)? `--fix-network` argümanında tekil örnek kontrolü atlanmalı.
4. `WlanQueryInterface` için `wlanapi` bağlanması gerekiyor (bugün `CMakeLists.txt:70`'te yok);
   Ethernet-only makinede DLL yoksa yükleme hatası olmasın diye gecikmeli yükleme önerilir.

---

## 6. İlk açılış deneyimi

**Amaç:** İlk kez açan kullanıcı 30 saniyede bağlanabilsin.

### 6.1 30 saniyenin önündeki engeller (sırayla)

1. Alıcıyı başlatmayı bilmek (bugün `Başlat`'a basmak gerekiyor; `autostartReceiver = false`).
2. Windows güvenlik duvarı penceresi: kurulum yönetici istemediği için kural yok; ilk
   dinlemede Windows sorar (`installer/airplay.iss:9-11`). Yanlış yanıt = keşif yok.
3. Ağ "Ortak" ise izin penceresindeki varsayılan seçim yetmeyebilir (5.8-2).
4. iPhone'da nereye bakılacağını bilmek.

### 6.2 Kararlar

- **[KARAR] İlk açılış = `config.ini` yok** (ya da `[app] first_run_done` yok). Taşınabilir
  kopyalar `%APPDATA%`'yı paylaşıyor (`config_store.h` `isPortableInstall` yorumu); ikinci kopya
  ilk açılış sayılmaz — bu kabul edilebilir.
- **[KARAR] İlk açılışta alıcı kendiliğinden başlar**, ama önce ağ türü okunur (5.2-3, yönetici
  istemez) ve güvenlik duvarı penceresi için uyarı şeridi gösterilir. Başlat düğmesine basmayı
  öğretmek 30 saniyenin ilk 10'unu yer.
- **[KARAR] Hint satırı ilk açılışta üç adımlı listeye dönüşür** (yalnız Hazır durumunda ve
  ilk başarılı bağlantıya kadar). Ayrı karşılama ekranı yok; kullanıcı zaten ana ekranda.
- **[KARAR] 45 sn içinde bağlantı yoksa** teşhis bağlantısı şeride yükselir:
  `Listede göremiyor musunuz?` `[Kontrol et]`.
- **[KARAR] İlk başarılı bağlantıda** bilgi şeridi `Bağlandı! Bundan sonra yalnız listeden
  seçmeniz yeter.` ve `first_run_done=1`.
- Bilgisayar adı önerisi: varsayılan `AirPlay-PC` kalır. `COMPUTERNAME` önermek (ör.
  `DESKTOP-7Q3K2LM`) telefondaki listede daha kötü okunur.

### 6.3 Mockup — ilk açılış, güvenlik duvarı sorulmadan hemen önce

```
┌─ airplay ─────────────────────────────────────────── W=420 ┐
│ y=12   ●  Başlatılıyor…                                    │ turuncu
│ y=42                                                       │
│ y=60                                                       │
│ y=82   ┌──────────────────────────────────────────────────┐│ uyarı şeridi
│        │ Windows izin soracak: «İzin ver»e basın.      [×]││
│        └──────────────────────────────────────────────────┘│
│ y=116  Bu bilgisayarın adı:  [ AirPlay-PC           ]      │
│ ...                                                        │
```

Ağ Ortak ise şerit metni ve düğmesi değişir:
`Ağınız «Ortak»: izin penceresinde onu da işaretleyin.` `[Ağı Özel yap]` (→ 5.6
`kAskMakePrivate` onayı + UAC).

### 6.4 Mockup — ilk açılış, Hazır

```
┌─ airplay ─────────────────────────────────────────── W=420 ┐
│ y=12   ●  Hazır                                            │ mavi
│ y=42      1. iPhone ile bu bilgisayar aynı Wi-Fi'da olsun  │ hint h18
│ y=60      2. iPhone: Denetim Merkezi → Ekran Yansıtma      │ meta satırı yerine h18
│ y=78      3. Listeden seçin: AirPlay-PC                    │ +1 satır h18 (yalnız ilk açılış)
│ y=104  Bu bilgisayarın adı:  [ AirPlay-PC           ]      │
│ y=142  [       Durdur       ]  [ ● Kayıt ]                 │
│ ...                                                        │
```

İlk açılışta hint üç satır (`h=18` × 3); 3. adımdaki ad kalın değil, düz metin (owner-draw
istemesin). İlk bağlantıdan sonra normal tek satırlık hint'e döner.

### 6.5 Durumlar ve hatalar

| Durum | Gösterim |
|---|---|
| Ağ yok (hiçbir bağdaştırıcıda ağ geçidi yok) | Başlatılmaz; hata şeridi `Ağ bağlantısı yok.` |
| `uxplay.exe` bulunamadı | bugünkü `kErrNoUxplay` (değişmez) |
| Kullanıcı güvenlik duvarında İptal'e bastı | Keşif olmaz; 45 sn sonra `[Kontrol et]` → sihirbaz kontrol 4 kırmızı |
| Bağlanmadan kapattı | `first_run_done` yazılmaz; bir sonraki açılışta adımlar yine görünür, ama alıcı artık kendiliğinden başlamaz (kullanıcının `autostartReceiver` tercihi geçerli) |

### 6.6 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kFirstStep1` | `1. iPhone ile bu bilgisayar aynı Wi-Fi'da olsun` |
| `kFirstStep2` | `2. iPhone: Denetim Merkezi → Ekran Yansıtma` |
| `kFirstStep3Pre` | `3. Listeden seçin: ` |
| `kStripFwPrompt` | `Windows izin soracak: «İzin ver»e basın.` |
| `kStripFwPublic` | `Ağınız «Ortak»: izin penceresinde onu da işaretleyin.` |
| `kBtnMakePrivate` | `Ağı Özel yap` |
| `kStripFirstNudge` | `Listede göremiyor musunuz?` |
| `kBtnCheck` | `Kontrol et` |
| `kStripFirstSuccess` | `Bağlandı! Bundan sonra yalnız listeden seçmeniz yeter.` |
| `kStripNoNetwork` | `Ağ bağlantısı yok.` |

`config.ini`: `[app] first_run_done=0`.

### 6.7 Uxplay bağlantısı

Yok (mevcut argv).

### 6.8 Açık sorular

1. Güvenlik duvarı penceresi tam olarak ne zaman çıkıyor — `uxplay.exe` ilk `bind`/`listen`'de
   mi, ilk gelen pakette mi? Şeridin zamanlaması buna göre (Başlat'tan hemen önce gösterip
   `Waiting`'e geçince kapatmayı öneriyorum). MANUEL, temiz bir kullanıcı hesabında.
2. Kurulum sonunda (`airplay.iss` `[Run]`) "güvenlik duvarı kuralını şimdi ekle (yönetici)"
   isteğe bağlı görevi eklemek 2. ve 3. engeli tek seferde kaldırır. Bu UI değil kurulum işi;
   Fable'a bırakıyorum.

---

## 7. Durum satırı ve bildirimler

**Amaç:** Bağlandı/koptu/kayıt gibi olayların kullanıcıya, onu rahatsız etmeden ulaşması.

### 7.1 Kararlar

- **[KARAR] Üç kanal, tek kural:** ana pencere görünürse **pencere içi** (durum satırı + şerit);
  gizliyse (tepside) **tepsi balonu** (`tray_.showBalloon`, `main_window.cpp:577` vb.). İkisi
  birden yok — tek istisna PIN (bkz. 2.2).
- **[KARAR] Görüntü penceresi tam ekransa balon yok** (sunum/film üstüne balon düşmez);
  olay pencere içi şeride yazılır ve kullanıcı tam ekrandan çıkınca görür. İstisna yine PIN ve
  bağlanma isteği (bunlar beklemez).
- **[KARAR] Balon sınırı:** 10 sn'de en çok bir balon; aynı tür üst üste gelirse sonuncusu.
- **[KARAR] Toast (WinRT) yok, balon (`NIIF_INFO`/`NIIF_WARNING`) var.** Windows 10 balonları
  zaten bildirim merkezine düşürüyor; toast için AUMID + kısayol kaydı gerekir.
- **[KARAR] Tepsi ipucu metni durumu taşır:** `airplay — Bağlandı: Mustafa'nın iPhone'u`,
  kayıtta sonuna ` · kayıt`. Simge değişmez (v0.5'te ikon seti yok).
- **[KARAR] Kullanıcının kendi yaptığı şey bildirilmez.** Telefondan "Yansıtmayı Durdur" =
  normal son, sessiz. Yalnız beklenmeyen kopuş (`LineTag::LostConnection`, `line_parser.cpp:132`)
  bildirilir.

### 7.2 Olay tablosu

| Olay | Pencere görünür | Pencere gizli | Tam ekran |
|---|---|---|---|
| Bağlandı | durum satırı | — (kullanıcı telefonun başında, görüntü zaten açılıyor) | — |
| Normal bitiş | durum satırı | — | — |
| Bağlantı koptu (`LostConnection`) | uyarı şeridi `Bağlantı koptu. iPhone'dan yeniden seçin.` | balon (uyarı) | şerit |
| PIN | durum satırı (§2) | pencere öne + balon | pencere öne + balon |
| Bağlanma isteği (liste modu) | soru şeridi | balon; tıklanınca pencere açılır | balon |
| Kayıt başladı | meta satırı | — | — |
| Kayıt kaydedildi | bilgi şeridi + `[Klasörü aç]` | balon; tıklanınca (`NIN_BALLOONUSERCLICK`) klasör açılır | şerit |
| Hata (alıcı kapandı) | durum satırı (bugünkü) | balon (hata) | balon |
| Güncelleme | bugünkü balon (değişmez) | | |

### 7.3 Mockup — bağlıyken tam durum alanı

```
│ y=12   ●  Bağlandı                                         │ yeşil
│ y=42      Mustafa'nın iPhone'u (iPhone 13) · 12.4 Mbps · 60 fps │
│ y=60      Ekran açık tutuluyor · ● Kayıt 03:12 · 148 MB    │
│ y=82   ┌──────────────────────────────────────────────────┐│
│        │ Bağlantı koptu. iPhone'dan yeniden seçin.     [×]││  (koptuysa; durum Hazır'a döner)
│        └──────────────────────────────────────────────────┘│
```

### 7.4 `strings.h` ekleri

| Anahtar | Metin |
|---|---|
| `kStripLost` | `Bağlantı koptu. iPhone'dan yeniden seçin.` |
| `kBalloonLostTitle` | `Bağlantı koptu` |
| `kBalloonLostText` | `iPhone'dan yeniden seçerek bağlanabilirsiniz.` |
| `kBalloonErrorTitle` | `Alıcı kapandı` |
| `kTipConnectedPre` | `Bağlandı: ` |
| `kTipRecordingSuf` | ` · kayıt` |

### 7.5 Uxplay bağlantısı

Yok; mevcut olaylar (`LostConnection`, `Stopping`, `ClientInfo`, `Pin`).

### 7.6 Açık sorular

1. `LostConnection` iOS'un kendi zaman aşımında da geliyor mu, yoksa yalnız ağ kopuşunda mı?
   Kilitli telefon `resetSeconds = 0` ile hiç kopmuyor; bu durumda "koptu" hiç çıkmaz — doğru
   davranış ama MANUEL teyit.
2. Windows Odak Yardımı (Focus Assist) açıkken balonlar zaten bastırılıyor; ayrıca kontrol
   etmeye gerek var mı? Önerim: hayır.

---

## 8. Ana pencerenin v0.5 tam yerleşimi (özet)

Kapalı, şerit yok, ilk açılış değil — `layout()`'un yeni akışı (y değerleri 96 dpi):

| y | Öğe | x / w / h |
|---|---|---|
| 12 | durum noktası + `status_` | nokta x=14; metin x=36 w=370 h=28 |
| 42 | `hint_` | x=36 w=370 h=18 |
| 60 | **yeni** `meta_` (STATIC, gri) | x=36 w=370 h=16 |
| 82 | **yeni** şerit (görünürse; +34) | x=14 w=392 h=28 |
| 82 | ad etiketi + edit | bugünkü (`lbl w132`, `edit x=150 w180 h24`) |
| 120 | `btnToggle_` + **yeni** `btnRecord_` | 14/160/34 ; 182/130/34 |
| 168 | **yeni** `Kullanım:` + combo | etiket 14/70 (y+5); combo 88/230 |
| 200 | **yeni** `Erişim:` + combo | aynı ölçüler |
| 238 | **yeni** SysLink `iPhone beni görmüyor` | 14/200/18 (Bağlıyken gizli, y ilerlemez) |
| 264 | `secAdvanced_` | bugünkü |
| 290 | `secDetails_` | bugünkü |

Gelişmiş'e eklenenler (sırayla, bugünkü bloğun içinde): `Çözünürlük`, `Ses-görüntü eşitleme`
(Akıcılık'ın altına); `Bağlıyken ekranı açık tut` (Telefon çerçevesi satırının sağı);
`Kayıt klasörü` satırı; `Sabit kod` + `Cihazlar` listesi (Komutu kopyala'nın üstüne). Tahmini
Gelişmiş yüksekliği bugünkünden +~170 px.

---

## 9. Önerilen uygulama sırası

| Sıra | İş | Gerekçe |
|---|---|---|
| 0 | **Ön koşul ölçümleri (yarım gün, kod değil):** (a) Durdur'da MP4 kapanıyor mu (1.9-1), (b) `INetFwPolicy2` yöneticisiz okunuyor mu (5.8-1), (c) display power request ekran koruyucuyu durduruyor mu (4.5-1) | Üç özelliğin tasarımı bu cevaplara göre dallanıyor; yanlış dalda kod yazmamak için. |
| 1 | **Ortak zemin:** şerit + meta satırı + 0.3 "yeniden başlatınca geçerli" deseni + host'ta `ClientDenied` ve "reddedilen cihaz Bağlandı görünmesin" düzeltmesi (2.9) | Sonraki her özellik bunları kullanıyor; 2.9 bugün de var olan bir hata. |
| 2 | **Uyku engelleme (§4)** | En küçük iş, alt süreçle ilgisi yok, meta satırını ilk kullanan özellik → zemini sınar. Analiz skoru 15. |
| 3 | **Kullanım amacı (§3)** | Yalnız argv + iki combo; 0.3 desenini ilk gerçekten kullanan iş. Kayıt ve erişim de aynı deseni kullanacak, önce en risksizinde oturmalı. |
| 4 | **Kayıt (§1)** | En yüksek kullanıcı değeri (analiz skoru 16) ama 0-(a)'nın cevabına bağlı; gerekirse `patches/0006`. Upstream senkronu (analiz F1) bundan önce gelirse "yazarken diske bas" bedavaya gelir. |
| 5 | **Erişim: önce PIN, sonra liste (§2)** | PIN, MessageBox'ı kaldırdığı için kendi başına bir düzeltme; liste modu 1'deki `ClientDenied`'a dayanıyor. |
| 6 | **Bildirim politikası (§7)** | 1–5 olayları ürettikten sonra balon/şerit kuralını tek yerde bağlamak; erken yapılırsa her özellik kendi balonunu atar. |
| 7 | **Teşhis sihirbazı (§5)** | En büyük UI işi (yeni pencere, COM, iphlpapi, UAC'li ikinci kopya); 0-(b)'ye bağlı. |
| 8 | **İlk açılış (§6)** | Sihirbazın ağ türü kontrolünü ve `[Kontrol et]` girişini yeniden kullanıyor; en son yapılınca neredeyse yalnız metin + akış. |

Upstream senkronu (analiz öncelik 1) bu listenin dışında ama **4 ve 5'ten önce** bitmesi
gerekir: ikisi de upstream'in değiştirdiği yollara (mux, PIN/pairing) dokunuyor ve iOS 27 ile
MANUEL test edilmeleri lazım.

---

## 10. Yapmamayı önerdiklerim

1. **`-pw` parola modu.** Parola `config.ini`'de açık metin durur, README'nin kendisi md5
   Digest'i "weak security" diyor (`README.md:1291-1300`), `-pin` ile birlikte verilince biri
   sessizce düşüyor (`:1309-1310`). PIN + izin listesi aynı ihtiyacı karşılıyor.
2. **Bağlantı anında canlı "izin verilsin mi?" penceresi.** Karar alt süreçte, senkron
   (`uxplay.cpp:2362-2380`); ancak yama ile stdin'den yanıt bekletmek mümkün olurdu, bu da
   iPhone tarafında zaman aşımı riski taşıyan, test edilemeyen bir protokol değişikliği.
3. **Yansıtmayı kesmeden kaydı durdurma/başlatma.** Kaynak buna izin vermiyor (§1.1); taklit
   etmek (dosyayı sonradan kırpmak) yanlış beklenti yaratır.
4. **Görüntünün üstüne kayıt noktası / PIN / bildirim çizmek.** Yabancı swapchain bizim
   boyadığımızın üstünde (M2-SPEC §2b); üstte ayrı katmanlı pencere gerekir, tam ekranda
   odak/z-order sorunları getirir.
5. **Ses gecikmesi kaydırıcısı (`-vsync x` ms).** Ölçüm altyapısı yok; kullanıcı neyi
   ayarladığını göremeyeceği bir sayı olur. Sonraki sürüm, önce MANUEL ölçüm.
6. **`-scrsv`.** Windows'ta işlemiyor (`README.md:1242-1244`).
7. **Kurulumda sessizce firewall kuralı / ağ profilini sessizce Özel yapma.** Kurulum bilerek
   yöneticisiz (`airplay.iss:8-11`); ağ türünü kullanıcıya sormadan değiştirmek kafe ağında
   onu açık bırakır. Her zaman açık onay + UAC.
8. **Ayrı bir "Ayarlar" penceresi / sekmeler.** UX-SPEC kararı; yeni alanlar Gelişmiş'e
   sığıyor. Tek istisna geçici araç olan teşhis penceresi.
9. **Duruma göre değişen / animasyonlu tepsi simgesi.** Yeni ikon seti ve DPI varyantları
   ister; tooltip yeterli.
10. **Kayıt formatı/kalite seçimi.** `mp4mux` yeniden kodlamıyor, gelen H.264/H.265'i olduğu
    gibi kutuya koyuyor (`mux_renderer.c:115-135`); seçilecek bir şey yok.
11. **Sunum ve oyunu iki ayrı ön ayar yapmak.** Aynı argv'ye çıkıyor (§3.1).

---

## 11. Manuel doğrulama ekleri (`docs/MANUAL-VERIFY.md` için öneri)

- Kayıt açık → iPhone bağla → 30 sn → telefondan durdur → dosya oynatılıyor mu (VLC/Filmler).
- Aynısı ama PC'de Durdur'a bas → dosya oynatılıyor mu. (1.9-1)
- Kayıtta telefonu yan çevir → "Telefon döndü" uyarısı çıktı mı; dosyanın o noktası.
- PIN modu → ilk bağlantıda kod iPhone'da soruldu mu, pencere tepsideyken öne geldi mi.
- Liste modu → ikinci cihaz reddedildi, şerit çıktı, `İzin ver` sonrası bağlanabildi.
- Yansıtırken ekran koruyucu 1 dk → 2 dk sonra ekran açık mı; `powercfg /requests`'te satır var mı.
- Ağı Ortak yap → sihirbaz kırmızı gösterdi mi → Düzelt → yeşil oldu mu, iPhone listede mi.
- Temiz kullanıcı hesabında ilk açılış → kronometreyle iPhone'da görüntü kaç saniyede.
