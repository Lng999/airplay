# UxPlay upstream farkı: pin `a3c19cbc` → HEAD `4e95a9e` (2026-10-17)

Kapsam: `third_party/UxPlay` pin'i (`a3c19cbc7fcc870d74a0960bc97817a2569b4808`, 2026-08-09) ile
`FDH2/UxPlay` master HEAD'i (`4e95a9ec013c52d8555d88dbc55abe346e94d56e`, 2026-10-16) arasındaki fark,
yamalarımızın (`patches/0001..0005`) HEAD'e uyup uymadığı ve iOS 27 durumu.

Tüm deneyler scratch klonda yapıldı. `C:\Users\pc\Desktop\airplay` içinde hiçbir git yazma komutu
çalıştırılmadı.

- Klon: `C:\Users\pc\AppData\Local\Temp\claude\C--Users-pc\978c9d6b-37c5-46e8-85b9-340d01f09e50\scratchpad\uxplay-upstream`
  (dallar: `patched` = pin + 5 yama, `rebased` = HEAD + 0002..0005, `planb` = pin + 5 yama + 5 backport)
- HEAD'e taşınmış yamalar (format-patch): `...\scratchpad\uxplay-rebased-patches\`

**Etiketler:** **[ölçüldü]** = komutla çalıştırılıp çıktısı görüldü. **[okuyarak]** = kaynak/diff
okunarak çıkarıldı, çalıştırılmadı. **[MANUEL DOĞRULAMA GEREKLİ]** = iPhone ile gerçek test lazım.

---

## 1. Özet

1. **Pin artık upstream master'ın atası değil.** [ölçüldü] `a3c19cb` (PR #544 merge) master'dan
   erişilemiyor; `git merge-base a3c19cb master` = `2ece579` (2026-08-08). Upstream 2026-08-25
   civarında master'ı yeniden yazmış. AWDL değişikliği `cc0afd1` olarak farklı bir patch-id ile
   yeniden girmiş. Pin'de olup master'da olmayan tek şey macOS'a özgü AWDL kodu (`lib/dns_sd`,
   `lib/dnssd.*`, `lib/netutils.*`, `uxplay.cpp`). Windows'u etkilemiyor. `a3c19cb` hâlâ SHA ile
   fetch edilebiliyor ama hiçbir ref onu tutmuyor (`git ls-remote` içinde yok). GitHub bir gün
   GC ederse `git submodule update --init` kırılır. Bu, pin'i taşımak için ayrı bir sebep.
2. **55 commit** (54'ü merge değil), commit tarihleri 2026-08-25 → 2026-10-16. Diff: 33 dosya,
   +2286 / −662. [ölçüldü]
3. **v1.74 henüz yayınlanmadı.** Son tag `v1.73.7` (2026-09-04). Master değil,
   `release/1.73.7` dalında, v1.73.6 üstüne cherry-pick ile çıkmış. Master'daki `VERSION` zaten
   `"1.74"` (pin'de de öyleydi). [ölçüldü]
4. **Pin'imizde yüksek önem dereceli, kimlik doğrulama öncesi (pre-auth) bir açık var:**
   GHSA-479c-ww7g-wgp8 (high). Pin `lib/raop_handlers.h:366`'da `memcpy(proof, client_proof,
   client_proof_len)` ile 64 baytlık stack buffer'a sınırsız kopyalama yapıyor. `:814` ve `:1188`'de
   NULL dereference var. `/pair-setup-pin` ve `/audioMode` `lib/raop.c:412-421`'de koşulsuz
   dağıtılıyor, yani `-pin` kullanmasak da LAN'daki herkes erişebilir. [okuyarak; exploit denenmedi]
5. **Analistin iddiaları:** TEARDOWN (iOS 27), Hyper-V/WSL mDNS, güvenlik, NTP yeniden yazımı ve
   MP4 artımlı yazma iddiaları **doğru**. "Bakımcı `-h265`'i varsayılan yapmayı tartışıyor"
   iddiasına #535 ve #607'de **kanıt bulamadım**. HEAD'de varsayılan hâlâ `h265_support = false`
   (`uxplay.cpp:181`).
6. **Yama uygunluğu:** [ölçüldü] `git apply --check` ile HEAD'e: 0002 ✓, 0003 ✓, 0001 ✗,
   0004 ✗, 0005 ✗. Rebase ile (3-way): **0002–0005 çakışmasız taşınıyor**, ±satırlar birebir
   aynı kalıyor. **Gerçek çakışma yalnız 0001'de**, çünkü upstream aynı fonksiyonu kendi
   yöntemiyle yeniden yazmış.
7. **Derleme:** [ölçüldü] HEAD, yamasız ve 0002–0005 yamalıyken MSYS2 UCRT64'te (gcc 16.2.0,
   GStreamer 1.28.6) uyarısız derleniyor. Tek yeni DLL importu `libgstvideo-1.0-0.dll`.
8. **Öneri:** pin'i HEAD `4e95a9e`'e taşı, 0001'i emekliye ayır, 0002–0005'i yeniden üret.
   Ama HEAD'in Windows ses yolu daha 1 günlük bir regresyon düzeltmesi (#605). Bu yüzden önce
   manuel iPhone testi gerekiyor. Plan B (pin + 5 backport) da ölçüldü ve derleniyor (§8).

---

## 2. Commit listesi (`git log a3c19cbc..master`, eskiden yeniye)

İşaretler: **[iOS27] [WIN] [MDNS] [MIRROR] [NTP] [SEC] [H265] [TEARDOWN] [HLS] [MP4] [MAC] [DOC]**.
"Ne yaptı" sütunu diff'e bakılarak yazıldı. Yalnız ★ işaretlileri tek tek diff olarak açtım.
Diğerlerini `--stat` ve mesajla sınıflandırdım. [okuyarak]

| Commit | Tarih | İşaret | Ne yaptı |
|---|---|---|---|
| `cc0afd1` | 08-25 | MAC | AWDL/P2P keşfi (macOS). Pin'deki `5dc4910`'in yeniden yazılmış hâli. Yalnız `__APPLE__ && UXPLAY_HAVE_APPLE_P2P` altında. |
| `e405fe1` ★ | 08-25 | MIRROR SEC | `raop_rtp_mirror.c:463`: NAL uzunluğu (`nc_len`) payload'un kalanını aşarsa veri geçersiz sayılıyor. Önceden çözülemeyen paket `utils_data_to_string` içinde SIGSEGV veriyordu. |
| `f0b042b` ★ | 08-25 | MIRROR | `mirror_buffer.c:66-74`: yeniden anahtarlamada (TEARDOWN 110 → yeni stream SETUP) AES-CTR artık durumu (`nextDecryptCount`, `og[]`) sıfırlanıyor. Önceden ikinci mirroring oturumu tamamen bozuk görüntü veriyordu. |
| `d1e5621` | 08-25 | MP4 | `mux_renderer.c`: `fragment-mode=first-moov-then-finalise`. Çökmede kayıt kaybolmuyor (yazar tarihi 08-12). |
| `c0725b9` | 08-25 | MAC | PR #561 merge (applep2p). |
| `ed67ead` | 08-26 | DOC | `-p2p` dokümantasyonu. |
| `f0dd27d` | 08-27 | HLS | Eksik HLS playlist'te `assert` yerine guard. |
| `ed5ab0a` | 08-27 | MAC | macOS p2p düzeltmeleri. `-p2p` artık macOS dışında reddediliyor. |
| `575c562` | 08-29 | HLS | `adjust_yt_condensed_playlist` yeniden yazımı. |
| `546820c` ★ | 08-29 | **iOS27 TEARDOWN** | `raop_handlers.h:1317`: tipsiz TEARDOWN'da mirror yok edildikten sonra `video_reset(RESET_TYPE_RTP_SHUTDOWN)` çağrılıyor. iOS 27 TEARDOWN 110 göndermiyor, pencere son karede donuk kalıyordu (#540). |
| `aec205d` | 08-31 | NTP | `raop_rtp.c`'ye kernel timestamp'li recv eklendi. |
| `2792afc` ★ | 09-02 | WIN MDNS | `lib/compat.h:25`: `#include <iphlpapi.h>`. |
| `8d62eba` ★ | 09-02 | **WIN MDNS** | `mdnsd.c:348-414`: Windows'ta `mdns_get_default_ipv4()` artık `GetAdaptersAddresses` kullanıyor (§3.1). |
| `9e402cc` | 09-02 | HLS | Debug kodu temizliği. |
| `746ec0e` ★ | 09-03 | **SEC** | GHSA-479c-ww7g-wgp8: proof uzunluk kontrolü + `memcpy` 64 bayta sınırlandı. `user_agent` ve `audiomode` NULL kontrolleri eklendi. |
| `d29dfab` ★ | 09-04 | SEC(−) | `srp_validate_proof` argüman temizliği. **Yan etki:** 746ec0e'nin eklediği `client_proof_len != sizeof(proof)` kontrolünü **siliyor**. Bkz. §3.3. |
| `f524d8a` ★ | 09-06 | build | CMake C/C++ standardı. `CMakeLists.txt:9` `CMAKE_C_STARDARD` (yazım hatası), yani C99 ayarı etkisiz. |
| `9f3c2bb` | 09-06 | NTP | Kernel timestamp düzeltmeleri. |
| `366dd5e` | 09-09 | DOC | README. |
| `eb80e6f` ★ | 09-11 | CLI | Yedi argüman çökmesi/sessiz kabul düzeltildi (`-p tcp/udp`, `-db`, `-vol`, `-reset` değer zorunlu, `-hls n` kontrolü, config dosyası argv'si NULL ile bitiriliyor). |
| `2d7bf40` | 09-11 | DOC | Yazım hatası. |
| `c3137b6` | 09-11 | audio | `audio_renderer`: sistem saati tek sefer unref ediliyor (teardown'da ref eksiği vardı). |
| `44024fe` | 09-11 | — | Yorum/hata metni. |
| `4bfa594` ★ | 09-14 | **NTP** | NTP alt sisteminin büyük yeniden yazımı (`raop_ntp.c` +526/−256). Hesaplar double ile yapılıyor. Windows kernel timestamp'i `WSARecvMsg` + `SIO_TIMESTAMPING` ile (yalnız UCRT64; `raop_ntp.c:152`). |
| `3f7b96e` | 09-14 | DOC NTP | README: Windows'ta `Set-NetAdapterAdvancedProperty ... "Software Timestamp" "RxAll"`. |
| `00c53fb` | 09-16 | NTP | Kernel timestamp kapalı mesajına adaptör adı eklendi. |
| `942d7b2` ★ | 09-16 | — | `raop_handlers.h:555`: 0x03 dışındaki FairPlay tipinde 501 + açık hata mesajı. |
| `e3e7f06` | 09-16 | video | `SRGB_FIX` caps'i `RGB` → `RGBx` (#573, wlroots hizalama). |
| `2c7b63e` | 09-16 | NTP | Kozmetik. |
| `57ea834` ★ | 09-17 | WIN video | `video_renderer.c:26-48`: bus sync handler `prepare-window-handle`'ı yakalıyor. `video_renderer_set_window_handle()` API'si eklendi ama hiçbir opsiyona bağlı değil. Yeni import `libgstvideo-1.0`. |
| `ba2bd19` | 09-27 | MIRROR | `raop_rtp_mirror.c`: `plist_free` sızıntısı (#585). |
| `1c06bcb` | 09-27 | dns_sd | Avahi/Bonjour socket'i boşaltılıyor (yalnız `USE_DNS_SD`). Bilerek `lib/mdnsd`'ye dokunmuyor. |
| `e017675` | 09-27 | WIN dns_sd | Yukarıdakinin Windows sürümü (`EXTERNAL_DNS_SD` altında; bizim varsayılan build'imizde derlenmiyor). |
| `87e004c` | 09-27 | HLS | Bellek sızıntısı. |
| `35bc295` | 09-27 | HLS | Hostla uyumsuz HLS akışlarını eleyen filtre (`-custom`, `-rpi n`). |
| `45607fe` | 09-27 | HLS | Saniye → ns dönüşümü. |
| `5b76d02` | 09-27 | HLS X11 | `waiting_for_x11` yeniden yazımı. |
| `412c5b7` | 09-28 | audio | Ses düzeyi istemciyle tutarlı (`current_volume`). Her değişimde yeni bir `LOGI("AirPlay volume ...")` satırı basılıyor. |
| `801f2cb` | 09-28 | video | (`fc8c388` ile geri alındı) |
| `8f40118` | 09-28 | HLS | (`db5f830` ile geri alındı) |
| `2b5ff5a` | 09-28 | HLS | Seek bayrağı `ACCURATE`. |
| `db5f830` | 09-29 | HLS | Revert. |
| `fc8c388` | 09-29 | video | Revert. |
| `b3202df` | 09-29 | video | Yok edilen renderer'lar NULL'lanıyor ve kullanılmadan önce kontrol ediliyor. |
| `69ed4af` | 09-30 | SEC? HTTP | `http_request.c` llhttp callback'leri yeniden yazıldı. Standart dışı "peek ahead" kaldırıldı. |
| `a01ffcf` | 09-30 | — | Yazım hatası. |
| `d7a088d` | 10-01 | HTTP | `httpd.c`: ilk 8 baytı bekleyen bloklayan while-loop kaldırıldı. |
| `e60d981` ★ | 10-01 | HTTP | `httpd.c:377,473`: 60 sn sessiz kalan bağlantı kapatılıyor. |
| `4c6e017` | 10-03 | NTP | `sync_delay` + yardımcılar. |
| `660e1ad` | 10-03 | NTP | `recv_time_kernel = NULL` ile çağrılabiliyor. |
| `8ef6776` | 10-04 | NTP | `raop_rtp`'de recvfrom'a dönüş… |
| `3dbf7ce` | 10-05 | NTP | …ve onun revert'ü (hâlâ değişiyor). |
| `ec20312` | 10-11 | audio | Veri taşımayan ses paketleri de buffer'a giriyor (seqnum boşluğu yüzünden gereksiz resend isteniyordu, #597). |
| `4c86094` ★ | 10-16 | **WIN NTP** | **#605 regresyon düzeltmesi:** `kernel_timestamp_session_recv` fallback'i `&addrlen` (int**) geçiriyordu. Windows'ta mirroring sesi tamamen yoktu ve `-d` ile assert'le çöküyordu. csock düz `recvfrom`'a döndü. |
| `4e95a9e` ★ | 10-16 | iOS | `raop_handlers.h:151`: `keepAliveLowPower` uint yerine bool. |

## 3. Bizi ilgilendiren değişiklikler (dosya bazında)

İstenen dosyaların `a3c19cbc..master` diff'i okundu. [okuyarak]

### 3.1 `lib/mdnsd/mdnsd.c` ve yama 0001: aynı sorun, farklı çözüm

Upstream `8d62eba` (`mdnsd.c:348-414` @HEAD), Windows dalında `connect()+getsockname()` probunu
tamamen bırakıyor:

- `GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, …)` ile adaptörleri geziyor.
- Koşullar: `OperStatus == Up`, loopback değil, `FirstGatewayAddress != NULL`, açıklamasında
  `Hyper-V`, `Virtual`, `WSL` ya da `vEthernet` **geçmeyen** (`:377-380`) **ilk** adaptör.
- O adaptörün `FirstUnicastAddress`'ini alıyor (`:384-386`) ve `#else`'teki eski probu yalnız
  POSIX'te çalıştırıyor (`:392-413`).
- Sonuç değeri eskisi gibi `mdnsd->ipv4_addr`'a gidiyor (`:1038`). Oradan A kaydına (`:246`),
  `IP_MULTICAST_IF`'e (`:503`) ve `IP_ADD_MEMBERSHIP`'e (`:518-526`) dağılıyor.

**Sonuç: aynı kök sorunu çözüyor** (Windows'ta multicast hedefe `connect()` ile adres bulma).
Commit başlığı "WSL Hyper-V kuruluysa" diyor, ama kod `connect()` probunu Windows'ta hiç
çağırmadığı için bizim 127.0.0.1 durumumuzu da kapsıyor. [okuyarak]

Bizim 0001 ile farkları (`patches/0001-mdnsd-windows-iface.patch`) [okuyarak]:

| Konu | Upstream 8d62eba | Bizim 0001 |
|---|---|---|
| Adaptör seçimi | Gateway'i olan **ilk** adaptör (enumerasyon sırası) | Gateway'i olan + en düşük `Ipv4Metric` |
| Sanal adaptör | Açıklamada `Virtual`/`vEthernet`/`Hyper-V`/`WSL` geçeni eliyor | Elemiyor (gateway + metric'e güveniyor) |
| APIPA 169.254/16 | Elemiyor (`FirstUnicastAddress` ne ise onu alıyor) | Eliyor |
| Hiçbir şey bulunamazsa | `addr = 0`: A kaydı yok, multicast `INADDR_ANY` | `connect` probu → 127/8 reddi → `gethostname()+getaddrinfo` |
| POSIX'te 127.x guard | Yok | Var |

**Riskli kenar durum** [çıkarım]: Hyper-V **External** switch kurulu bir makinede gateway'li
adaptör `vEthernet (…)` olur. Upstream onu eler, `addr = 0` olur ve A kaydı yayınlanmaz. Bizim
0001 onu seçerdi. Bu durumda iPhone'un SRV hedefini çözüp çözemeyeceğini ölçmedim.

**Bu makinede ölçüm** [ölçüldü, `scratchpad\ipv4probe\probe.c`]: Upstream mantığının birebir
kopyası `192.168.1.106` (Realtek, tek gateway'li adaptör) döndürdü. **Dikkat:** pin'deki eski
`connect(224.0.0.251)` probu da bugün `192.168.1.106` döndürdü. `mdnsd-windows-rca.md`'deki
127.0.0.1 ölçümü (IP .107 iken) bugün tekrarlanmadı. Bug ağ durumuna bağlı görünüyor. Hangi
koşulda tetiklendiğini bilmiyorum.

### 3.2 `lib/raop_handlers.h`

- `:151` `keepAliveLowPower` → bool (`4e95a9e`).
- `:355-370` pair-setup-pin. Bkz. §3.3.
- `:555-560` FairPlay tipi ≠ 0x03 ise 501 (`942d7b2`).
- `:815-823` `User-Agent` NULL guard (`746ec0e`).
- `:1144-1165` `SET_PARAMETER volume` artık `raop_rtp` olmadan da (HLS) işleniyor (`412c5b7`).
- `:1197-1202` `audioMode` NULL guard (`746ec0e`).
- `:1317` iOS 27 TEARDOWN düzeltmesi (`546820c`).

Bizim yamalardan hiçbiri bu dosyaya dokunmuyor.

### 3.3 Güvenlik: pin açık, HEAD'de daha hafif bir kalıntı

- **Pin** [okuyarak]: `third_party/UxPlay/lib/raop_handlers.h:366`
  `memcpy(proof, client_proof, (int) client_proof_len);` → stack taşması (CWE-121, advisory'de
  "GDB confirms saved return address overwritten"). `:814` `strstr(user_agent, …)` ve `:1188`
  `strstr(audiomode, …)` NULL deref. Üçü de pre-auth.
- **HEAD**: `memcpy(proof, client_proof, sizeof(proof));` (`raop_handlers.h:366`), ama
  `d29dfab` uzunluk kontrolünü sildi. İstemci 64 bayttan **kısa** bir proof gönderirse heap'ten
  64 bayta kadar **okuma taşması** (over-read) olur. Yazma taşması yok, etkisi çok daha düşük.
  Upstream'e bildirilmeye değer. [okuyarak; denenmedi]
- `v1.73.7` (release dalı) kontrolü koruyor, çünkü `d29dfab` orada yok.

### 3.4 `lib/raop_rtp_mirror.c`

- `:330-332` NTP yeniden yazımına uyum: `raop_ntp_adjust_remote_timestamp_offset`.
- `:460-463` NAL sınır kontrolü (`e405fe1`).
- `:807-816` plist sızıntısı (`ba2bd19`).
- `:616-617` "type 0x01 packet with no payload … bit 42 (SupportsScreenMultiCodec) is not set"
  mesajı değişmedi. iOS 27 + `-h265` yokken görülen hata bu.

### 3.5 `lib/raop_ntp.c` (+526/−256)

- Yeniden yazıldı (`4bfa594`). Kernel timestamp oturumu eklendi:
  `kernel_timestamp_session_create/recv` (`:113-370`).
- Windows'ta `SIO_TIMESTAMPING` yalnız UCRT64 başlıklarında var (`:152`, `:212`). Bizim
  toolchain UCRT64 olduğu için kod derleniyor ve çalışacak.
- Timestamp yoksa recvfrom'a düşüyor. **Bu fallback Windows'ta `4c86094`'e kadar (10-16)
  bozuktu (#605):** sesin tamamen gitmesi ve assert ile çökme.
- Churn devam ediyor: `8ef6776`/`3dbf7ce` revert çifti (10-04/05). dsock hâlâ
  `kernel_timestamp_session_recv` ile okunuyor.

### 3.6 `renderers/video_renderer.c`

- `57ea834`: `prepare-window-handle` sync handler'ı. Handle ayarlanmadıysa mesajı düşürüyor, sink
  kendi penceresini açıyor; yani eski davranış korunuyor [okuyarak]. Bizim `SetParent` ile
  pencere evlat edinmemizi (`app/src/ui/video_embed.cpp:72`) etkilememeli. İleride
  `video_renderer_set_window_handle()` daha temiz bir yol olabilir (şimdilik hiçbir CLI
  opsiyonuna bağlı değil).
- HLS ses düzeyi/seek değişiklikleri ve `renderer` NULL guard'ları.

### 3.7 `uxplay.cpp`

Argüman tablosu:
- `-rpi n` ve `-custom s` (HLS profil kısıtlama) eklendi.
- Eski `-rpi*` red mesajı kaldırıldı.
- `-p2p` macOS dışında `exit(1)`.
- `-reset` ve `-vol` artık değer zorunlu (`eb80e6f`); `uxplay_host.cpp:332` hep değer
  verdiği için sorun yok.
- `-db` / `-p tcp|udp` doğrulaması sıkılaştırıldı.

Diğer bölümler:
- **`video_process`**: değişmedi (bizim 0004/0005 kancaları buraya giriyor ve temiz oturuyor).
- **`CtrlHandler`**: değişmedi, upstream hâlâ `CTRL_BREAK_EVENT`'i yok sayıyor
  (`uxplay.cpp:630-632` @HEAD). **0002 hâlâ gerekli.**
- `main_loop`: yalnız `EXTERNAL_DNS_SD` altında socket watch eklendi (bizim varsayılan
  mdnsd build'imizde derlenmiyor; cmake log: "using internal minimal mDNSResponder").

Line parser [ölçüldü]: `app/src/host/line_parser.cpp`'nin dayandığı 14 log format string'inin
hepsi HEAD'de pin'dekiyle aynı sayıda ve birebir duruyor (`git grep -F`).

### 3.8 `CMakeLists.txt`

- C/C++ standart ayarları eklendi (`CMAKE_C_STARDARD` yazım hatası yüzünden C kısmı etkisiz).
- `USE_DNS_SD`'de `-DEXTERNAL_DNS_SD` tanımı eklendi.
- `0003`'ün eklediği `uxplay.rc` satırıyla çakışmıyor.

## 4. Yama uygunluk tablosu

Komutlar [ölçüldü]:
- `git apply --check <mutlak yol>`: HEAD'e karşı tek tek.
- Sıralı `git apply`.
- `--3way`: yama `index` satırlarındaki blob'lar repoda olmadığı için 3-way'e düşemedi
  ("repository lacks the necessary blob").
- Bu yüzden gerçek 3-way testi şöyle yapıldı: yamalar pin üstünde commit'lendi, sonra
  `git rebase --onto master a3c19cbc`.

Kontrol: pin'e beşi de temiz uyuyor (rc=0).

| Yama | `apply --check` @HEAD | 3-way rebase | Çakışma | Öneri |
|---|---|---|---|---|
| 0001 mdnsd-windows-iface | ✗ `mdnsd.c:345` | ✗ CONFLICT | Upstream `8d62eba` aynı fonksiyonun Windows dalını yeniden yazdı. Hunk'ımız (yeni `mdns_get_ipv4_from_adapters` + `mdns_get_default_ipv4` içi çağrı + 127 guard) upstream'in `#ifdef _WIN32 … GetAdaptersAddresses … #else` bloğuyla aynı bölgede. | **Emekli et.** Upstream kök nedeni çözüyor. Hyper-V External switch kenar durumu çıkarsa küçük bir "addr==0 ise fallback" yaması olarak **yeniden yaz**. |
| 0002 ctrl-break | ✓ | ✓ | Yok | **Koru** (upstream hâlâ yok sayıyor). |
| 0003 utf8-argv-manifest | ✓ | ✓ | Yok | **Koru.** |
| 0004 report-mirror-idle | ✗ `uxplay.cpp:720` (bağlam kaydı) | ✓ temiz | Yok. `main_loop`'a `EXTERNAL_DNS_SD` bloğu girdiği için bağlam satırları değişti. | **Koru, yeniden üret** (rebase çıktısı). |
| 0005 report-video-stats | ✗ `uxplay.cpp:564` (0004'e bağımlı + bağlam) | ✓ temiz | Yok | **Koru, yeniden üret.** |

Rebase sonrası `git diff master rebased`'in ±satırları pin'deki yama diff'iyle (0001 hariç)
**birebir aynı** [ölçüldü, `diff` boş çıktı].

- Hunk yerleri: `uxplay.cpp:572` (callback'ler), `:704` (`CTRL_BREAK_EVENT`), `:864-867` ve
  `:943-944` (`main_loop` timer'ları), `:2811-2813` (`video_process`).
- Yeniden üretilen yamalar `scratchpad\uxplay-rebased-patches\` altında; HEAD'e sırayla
  `git apply` ile uyuyor.
- `build.sh` "reset + sırayla apply" yaptığı için 0005'in tek başına `--check`'te düşmesi
  normal (README'de belgelenmiş 0004 bağımlılığı).

## 5. iOS 27 durumu

| Issue | Durum | Semptom | Düzeltme |
|---|---|---|---|
| #540 | Kapalı (08-29) | iOS 27 TEARDOWN 110 göndermiyor; mirroring bitince pencere son karede donuk kalıyor. Reconnect'te bayat görüntü. | `546820c` (HEAD'de var, **pin'de yok**). v1.73.7'de `24aa7e2`. |
| #535 | **Açık** | iOS 27 beta: SETUP'ta `combinedGetInfoWithControlSetup`; istemci veri bağlantısını hiç açmıyor (Linux, 1.73.6). Bakımcı (08-29): iPadOS 27 beta 5 ile "seems to work fine". LukeProducts (Windows/UCRT64, 1.74): "type 0x01 packet with no payload … bit 42" ile düşüyor, **`-h265` ile çözülüyor**. | Kod düzeltmesi yok. Geçici çözüm `-h265`. |
| #607 | **Açık** (10-15) | macOS 27 istemci + `-h265` + waylandsink: donuk kare. h265 kapalıyken düzeliyor. Bakımcı xvimagesink ile üretemedi. | Yok. |
| #539 | **Açık** | Pause/resume sonrası ses kalıcı olarak ölüyor (aynı codec ile yeniden SETUP). | Yok. |
| #605 | Kapalı (10-16) | Windows: master `ec20312`'de mirroring sesi yok, `-d` ile assert çökmesi (iPhone15,4, AirPlay/980.77.1). | `4c86094`. |
| — | — | `keepAliveLowPower` tipi (iOS istemcileri bool bekliyor). | `4e95a9e` (issue numarası yok). |

Çıkarım: iOS 27'de "**`-h265` olmadan düşme**" ile "**`-h265` ile donuk kare**" ikisi birden
raporlanmış ve birbiriyle çelişiyor. Akıllı varsayılan (F2) yazmadan önce bizim iPhone'umuzla
ölçmek şart. **[MANUEL DOĞRULAMA GEREKLİ]**

## 6. Son tag ve sürüm notları

- `v1.73.7`: 2026-09-04, hedef `release/1.73.7`. Release notu: "Fix Stack-based Buffer Overflow
  and NULL Pointer Dereference … (GHSA-479c-ww7g-wgp8); fix for changed TEARDOWN behavior in
  iOS 27; other minor fixes."
- Dalda `e0b5309` (güvenlik), `24aa7e2` (TEARDOWN), `5564da7` (AES-CTR), `dd978bf` (NAL), ses
  düzeltmeleri ve `RAOP_BUFFER_LENGTH` 32→256 var.
- **v1.74 yok.** README "1.74 2026-09-14" girdisi (NTP + macOS mDNSResponder) var, tag yok.
  [ölçüldü: `gh api …/releases`, `git tag`]
- Advisory GHSA-479c-ww7g-wgp8: severity high, 2026-09-04. [ölçüldü: `gh api …/security-advisories`]

## 7. Derleme sonucu [ölçüldü]

MSYS2 UCRT64, gcc 16.2.0 (Rev3), GStreamer 1.28.6, `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
-DNO_MARCH_NATIVE=ON` (`build.sh` ile aynı bayraklar). Çalıştırılmadı.

| Ağaç | cmake | ninja | uxplay.exe | Uyarı |
|---|---|---|---|---|
| HEAD `4e95a9e`, yamasız | ✓ | ✓ | 963 200 B | 0 |
| HEAD + rebased 0002–0005 | ✓ | ✓ (RC objesi derlendi) | 964 573 B | 0 |
| Plan B: pin + 0001–0005 + 5 backport | ✓ | ✓ | 935 398 B | 0 |

- **Yeni bağımlılık:** doğrudan importlarda yalnız `libgstvideo-1.0-0.dll` eklendi
  (`57ea834`). Bunu `objdump -p` ile mevcut `build\uxplay.exe` (pin) ile karşılaştırarak ölçtüm.
- `make-portable.ps1` DLL listesini closure'dan hesapladığı için elle bir şey eklemek
  gerekmemeli [okuyarak]. MSYS2'de yeni paket gerekmedi.

## 8. Pin yükseltme önerisi

**Hedef: `4e95a9ec013c52d8555d88dbc55abe346e94d56e`** (master HEAD, 2026-10-16). Gerekçeler:

- Güvenlik açığını (§3.3) kapatıyor.
- iOS 27 TEARDOWN, NAL ve AES-CTR re-key düzeltmelerini getiriyor.
- Upstream'in Windows mDNS düzeltmesini getiriyor (0001 emekli olur).
- Pin'i yeniden master'ın atası yapıyor (orphan riski biter).
- Mid-September ile 10-16 arasındaki hiçbir commit seçilmemeli: o aralıkta Windows mirroring
  sesi bozuk (#605).

Riskler:
1. **Taze regresyon düzeltmesi.** Windows ses yolu (`4c86094`) bir günlük. NTP katmanı hâlâ
   değişiyor (`8ef6776`/`3dbf7ce`).
2. **Kernel timestamp.** Windows'ta yeni kod yolu (`WSARecvMsg`). Adaptör "Software Timestamp"
   desteklemiyorsa fallback'e düşüyor; fallback tam da #605'te bozulan yoldu.
3. **0001'in emekliye ayrılması.** Hyper-V External switch / VPN / açıklamasında "Virtual"
   geçen gerçek NIC'lerde upstream `addr = 0` verebilir (§3.1).
4. **`httpd` 60 sn ölü bağlantı zaman aşımı** (`e60d981`). Ekranı kilitli/boşta bir istemcide
   bağlantının kopup kopmadığına bakılmalı.
5. **`d29dfab` over-read** (düşük etki, §3.3).
6. Log çıktısında yeni satırlar var (`AirPlay volume …`, kernel timestamp bildirimi). Parser
   bilinmeyen satırları yutuyorsa sorun yok, ama kontrol edilmeli.

**Plan B** (HEAD fazla tazeyse): pin + mevcut 5 yama üstüne `e405fe1`, `f0b042b`, `546820c`,
`746ec0e`, `4e95a9e` cherry-pick'i çakışmasız uyuyor ve derleniyor [ölçüldü, dal `planb`].
Ama pin orphan kaldığı için bu yalnız geçici bir köprü olur. Yamalar ayrı `.patch` dosyaları
olarak tutulmalı.

**Uygulama adımları** (bunları ben yapmadım, geliştirici rolüne):
- submodule'ü `4e95a9e`'e çek.
- `patches/0001` dosyasını sil.
- 0002–0005'i `scratchpad\uxplay-rebased-patches\`'ten (ya da submodule'de aynı rebase'i yaparak)
  yeniden üret.
- `patches/README.md`'deki pin hash'ini ve 0001 bölümünü güncelle.

**Manuel iPhone doğrulama maddeleri [MANUEL DOĞRULAMA GEREKLİ]:**
1. Keşif: iPhone Denetim Merkezi → Ekran Yansıtma listesinde alıcı görünüyor mu (0001 olmadan).
   Mümkünse VMware/Hyper-V adaptörü açıkken de dene.
2. Mirroring + **ses**: iPhone'dan video/müzik oynatırken ses geliyor mu (#605 yolu). `-d` ile
   kernel timestamp `(kernel)`/`(clock)` satırına bak.
3. Mirroring'i iPhone'dan durdur: pencere kapanıyor/sıfırlanıyor mu (TEARDOWN, #540). Hemen
   yeniden başlat: görüntü bozuk mu (AES-CTR re-key, `f0b042b`).
4. `-h265` kapalı ve açık: iOS sürümünü not ederek bağlantı kuruluyor mu, donuk kare var mı
   (#535/#607).
5. Mirror idle/active ve video stats satırları GUI'de hâlâ geliyor mu (0004/0005).
6. Ekran kilitli 2+ dakika: oturum ve `-reset` davranışı (`httpd` 60 sn timeout).
7. Ctrl-Break ile GUI'den durdurma temiz mi (0002). Türkçe karakterli `-n` adı (0003).
8. Pause/resume sonrası ses (#539, upstream'de açık; regresyon olup olmadığını görmek için).

---

## Açık noktalar / doğrulayamadıklarım

- `a3c19cb`'nin GitHub'da ne zaman GC edileceği bilinmiyor; bugün SHA ile fetch çalışıyor.
- Upstream'in master'ı neden yeniden yazdığını bilmiyorum. Push event'leri API'den gelmedi.
- 127.0.0.1 bug'ı bugün bu makinede tetiklenmedi (§3.1). Upstream düzeltmesinin bizim RCA
  koşulunda doğru adresi seçtiğini yalnız okuyarak söylüyorum.
- Commit listesindeki ★ işaretsiz satırları `--stat` ve mesajla sınıflandırdım, diff'lerini tek
  tek okumadım.
