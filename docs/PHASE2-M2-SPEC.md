# PHASE 2 SPEC — Milestone 2: görüntü uygulamanın içinde

Kaynaklar: `docs/SPEC.md` §3 Phase 2 (son açık madde), `docs/PHASE2-SPEC.md` §M2,
`docs/research/gstreamer-msys2-windows.md` (sink kararları).
Durum etiketleri: **[KARAR]** kesinleşti · **[ÖLÇÜLDÜ]** bu makinede deneyle doğrulandı ·
**[MANUEL]** iPhone ile elle doğrulanacak.

## 1. Hedef

M1'de video penceresi `uxplay.exe`'nin kendi penceresidir: masaüstünde ayrı durur, başlığı
"Direct3D11 renderer"dır, boyutu kaynak çözünürlüğüdür ve GUI'nin onun üzerinde hiçbir
söz hakkı yoktur. M2 bu pencereyi **bizim sahip olduğumuz bir pencerenin içine** alır:

- görüntü kendi pencere çerçevemizde, siyah zeminde, en-boy oranı korunarak durur,
- pencere yeniden boyutlandırılabilir; oran kilitlidir,
- tam ekran (F11 / Alt+Enter / sistem menüsü) bizim penceremizin işidir,
- "Her zaman üstte" artık görüntü penceresine de uygulanır,
- kaynak çözünürlüğü `-d` olmadan da bilinir.

## 2. Kararlar

### [KARAR] Embed yöntemi: pencereyi evlat edinme (reparent), `GstVideoOverlay` değil

`docs/PHASE2-SPEC.md` M2'yi "kendi `uxplay_core`'umuz + `GstVideoOverlay`" olarak tarif
ediyordu. Bu, `uxplay.cpp` monolitinin (static'ler, `exit()` çağrıları) `lib/` +
`renderers/` üzerine yeniden yazılması demek — çok büyük bir iş ve çocuk süreç mimarisini
tümden çöpe atıyor.

Bunun yerine: **çocuk süreç kendi penceresini yaratmaya devam eder, biz onu `SetParent` ile
kendi pencereremizin çocuğu yaparız.** Kaynak ağacına tek satır yama gerekmez.

`GstVideoOverlay` yolu zaten süreç sınırında çalışmazdı: `d3d11videosink` dışarıdan verilen
HWND'nin WNDPROC'unu `SetWindowLongPtr(GWLP_WNDPROC)` ile subclass eder ve bu çağrı
**başka bir sürecin penceresinde başarısız olur** (Win32'nin tek kesin süreçler-arası
yasağı). Evlat edinmede ise pencerenin sahibi baştan sona çocuk süreçtir; biz yalnızca
ebeveynini ve konumunu değiştiririz.

### [ÖLÇÜLDÜ] Süreçler arası evlat edinme çalışıyor

`gst-launch-1.0 videotestsrc ! d3d11videosink` ayrı süreçte çalıştırılıp penceresi bir
Win32 test programının paneline alındı (probe kaynağı geçici, çıktısı aşağıda):

```
candidate hwnd=... rect=0,0 1296x759 parent=0 style=16CF0000
SetWindowLongPtr(GWL_STYLE) ret=382664704 err=0  style now=56000000
SetParent ret=... err=0  GetParent now=<panel>
SetWindowPos ret=1 err=0
video rect after = 118,141 864x581 visible=1
== resize test ==
after resize video rect = 118,141 604x341 alive=1
child still running = 1
```

Ekran görüntüsüyle doğrulandı: SMPTE deseni panelin içinde, canlı çiziliyor. Yani

- `SetWindowLongPtr(GWL_STYLE)` süreçler arası **çalışır** (yalnız `GWLP_WNDPROC` yasak),
- `SetParent` süreçler arası **çalışır**,
- `SetWindowPos`/`MoveWindow` ile boyutlandırma çocuk sürece `WM_SIZE` olarak ulaşır ve
  sink swapchain'i yeniden boyutlandırır — çizim kesintisiz sürer.

### [ÖLÇÜLDÜ] Pencere sınıf adı güvenilir bir ölçüt değil

`GetClassNameW` sink penceresi için tek harf (`G`) döndürüyor, `GetWindowTextW` de (`D`).
Bu yüzden pencere **sınıf adıyla değil**, çocuk sürecin PID'i ile aranır:
`EnumWindows` → `GetWindowThreadProcessId(h) == pid` && görünür && üst düzey (ebeveyni yok)
&& `ConsoleWindowClass` değil. `uxplay.exe` `CREATE_NO_WINDOW` ile başlatıldığından
konsol penceresi zaten yok; oturum boyunca yarattığı tek üst düzey pencere sink'inkidir.

### [KARAR] Kaynak çözünürlüğü evlat edinme anında okunur

Sink penceresini kaynak videonun boyutunda yaratır (ölçüldü: 1280×720 kaynak → 1280×720
istemci alanı). Evlat edinmeden **önce** `GetClientRect` ile okunan bu boyut hem en-boy
oranını hem de durum satırındaki çözünürlüğü verir. Böylece M1'in "çözünürlük yalnız
Ayrıntılı günlük açıkken görünür" kısıtı embed açıkken ortadan kalkar.

### [KARAR] Ayrı bir üst düzey pencere, ana pencerenin içi değil

Görüntü ana pencerenin (kontrol paneli) içine değil, **kendi üst düzey penceremize** alınır
(`AirplayVideoWindow`). Gerekçe: M1.5'in sade kontrol paneli bozulmaz, tam ekran ve
"her zaman üstte" doğal olarak çalışır, kullanıcı görüntüyü ikinci ekrana taşıyabilir.
`docs/PHASE2-SPEC.md`'nin "always-on-top M2'de video penceresine uygulanır" maddesi de
tam olarak bunu tarif ediyor.

### [KARAR] Yaşam döngüsü

| Olay | Davranış |
|---|---|
| Alıcı çalışıyor, `embed_video=1` | 300 ms'lik zamanlayıcı çocuğun penceresini arar |
| Pencere bulundu | Evlat edinilir, görüntü penceresi gösterilir, çözünürlük durum satırına yazılır |
| Yansıtma bitti (çocuk pencereyi yok etti) | `IsWindow` false → görüntü penceresi gizlenir, arama sürer |
| Durdur / Çıkış / güncelleme | **Önce bırakılır** (`SetParent(nullptr)` + eski stiller), sonra çocuk durdurulur |
| Görüntü penceresi kapatılırsa | Pencere çocuğa geri verilir (masaüstünde kendi penceresi olarak kalır), o oturumda tekrar alınmaz |

Bırakma sırası önemli: bizim pencereyi yok etmemiz, çocuk sürecin penceresini de yok eder
(Windows çocuk pencereleri ebeveynle birlikte yıkar) ve sink bunu beklemez.

### [KARAR] `-fs` embed açıkken argv'ye eklenmez

Tam ekran artık bizim penceremizin özelliği. `Tam ekran` kutusu işaretliyken görüntü
penceresi tam ekran açılır; `uxplay.exe`'ye `-fs` geçilmez (sink kendi tam ekranını
açsa evlat edinme onu zaten normal pencereye çevirirdi).

### [KARAR] Yeni ayarlar

```ini
[app]
embed_video=1       ; görüntüyü uygulamanın penceresinde göster
launch_at_logon     ; config.ini'de değil: HKCU\...\Run\airplay (aşağıya bakın)

[video]
x= y= w= h=         ; görüntü penceresinin son konumu
fullscreen=0        ; görüntü penceresi tam ekran mı kapandı
```

## 3. FPS ve bit hızı

- **FPS** zaten var: istemcinin kendi `-FPSdata` raporundan (`MirrorFps`), saniyede bir.
- **Bit hızı** upstream'de hiçbir yerde raporlanmıyor. `patches/0005` `video_process()`
  içinde gelen bayt sayısını toplayıp saniyede bir satır basar:
  `mirror bitrate: <kbps> kbps <fps> fps`. `line_parser` bunu `MirrorBitrate` olayına
  çevirir, durum satırında `· 12.4 Mbps` olarak görünür.
  Bu, 0004 gibi bize özel bir kanca; upstream PR adayı değil.

## 4. Phase 3 — Windows açılışında başlatma

Kurulum paketi zaten isteğe bağlı bir Başlangıç kısayolu koyabiliyor
(`installer/airplay.iss` `[Tasks] startup`). GUI tarafındaki kutu bunu **ikinci bir
mekanizma yaratmadan** yönetmeli:

- **açık** → `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\airplay` =
  `"<exe>" -minimized`
- **kapalı** → hem bu değer hem de kurulumun bıraktığı
  `%APPDATA%\...\Startup\airplay.lnk` silinir
- **okuma** → ikisinden biri varsa kutu işaretli

`-minimized`: açılışta pencere açılmaz, uygulama doğrudan bildirim alanına yerleşir
(`[app] start_minimized` ile aynı etki, ama yalnız o başlatma için).

## 5. Kabul kriterleri

- `airplay-gui.exe` uyarısız derlenir; `airplay_host_tests` yeşil (yeni bitrate testi dahil).
- Embed kapalıyken davranış M1 ile birebir aynı.
- **[MANUEL]** iPhone bağlanınca görüntü uygulamanın penceresinde çıkar; pencere
  boyutlandırılınca oran korunur, siyah bantlar dışında bozulma olmaz.
- **[MANUEL]** F11 tam ekran açar/kapatır; Esc tam ekrandan çıkar.
- **[MANUEL]** Durdur → görüntü penceresi kapanır, `uxplay.exe` arkada yetim pencere
  bırakmaz; tekrar Başlat → yeniden evlat edinilir.
- **[MANUEL]** Çözünürlük durum satırında `Ayrıntılı günlük` kapalıyken de görünür.
