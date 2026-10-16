# Kod tabanı durum raporu — 2026-10-17

Denetçi: Opus alt ajanı (salt okuma; kod değiştirilmedi, git'e yazılmadı).
HEAD: `05f0505` (main, origin/main'den 1 commit ileride). Submodule `third_party/UxPlay` `a3c19cbc-dirty` (yamalar uygulanmış hâli).

**Etiketler:** **[Ö]** = bu oturumda komut çalıştırılarak ölçüldü · **[Ç]** = kaynak okunarak çıkarıldı, çalıştırılarak doğrulanmadı · **[M]** = iPhone ile manuel doğrulama gerekir.

---

## 1. Özet

- **Build sağlıklı [Ö].** `app/` MSYS2 UCRT64 + GCC 16.2.0 ile `build-audit/` altında 37/37 adımda, `-Wall -Wextra` ile **0 uyarı** derlendi. Daha sıkı bir bayrak setinde (`-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast ...`) **7 uyarı** çıktı, hepsi kozmetik. Stub host varyantı (`-DAIRPLAY_HOST_STUB=ON`) da 0 uyarıyla derlendi.
- **Testlerin hepsi yeşil [Ö].** ctest 2/2 (`airplay_host_tests` 32 test/146 kontrol, `airplay_frame_tests` 8 test/67 kontrol). Canlı testler: `airplay_embed_live` 34/34 PASS, `airplay_release_flash` 9/9 PASS, `airplay_host_live` PASS (graceful CTRL_BREAK ile durdu, çalışan GUI ile port çakışması olmadı).
- **Kod hacmi [Ö]:** `app/src` + `include` 5719 satır (yalnız `.cpp` 4613), testler 1582 satır. En büyük dosya `main_window.cpp` (1414 satır), en büyük fonksiyon `parseUxplayLineDetailed` (~271 satır).
- **2 kritik bulgu:** (K1) güncelleyici indirdiği kurulum dosyasını hiçbir bütünlük/imza kontrolü olmadan `/SILENT` çalıştırıyor; (K2) "Windows açılışında başlat" kutusu kurulumun koyduğu Başlangıç kısayolunu yanlış adla arıyor, yani v0.4.0 sürüm notunun vaat ettiği temizlik çalışmıyor.
- **8 önemli, 14 küçük bulgu.** Önemlilerin çoğu belge kayması (sürüm notu dahil), UI thread'ini bloklayan `stop()` ve state geçişindeki yarış.
- **0.4.0 yayına hazır değil:** etiket yok, HEAD push edilmemiş, sürüm notu kodla çelişiyor, M2 + çerçeve için iPhone kontrol listesinin tamamı boş.

---

## 2. Build ve test sonuçları (ham çıktılar)

### 2.1 GUI build [Ö]

```
$ C:/msys64/usr/bin/env MSYSTEM=UCRT64 C:/msys64/usr/bin/bash.exe -lc \
    "cd /c/Users/pc/Desktop/airplay && cmake -S app -B build-audit -G Ninja && ninja -C build-audit"
-- The CXX compiler identification is GNU 16.2.0
-- airplay: using the real host from src/host
-- Build files have been written to: C:/Users/pc/Desktop/airplay/build-audit
[1/37] Building CXX object CMakeFiles/airplay-gui.dir/src/ui/single_instance.cpp.obj
...
[37/37] Linking CXX executable tests\airplay_release_flash.exe
EXIT=0       grep -c "warning:" build.log  ->  0
```

Yalnızca MSYS2'nin her `bash -lc`'de bastığı üç `cp: cannot stat ...\etc\protocol|services|networks` satırı var (`docs/BUILD-NOTES.md` §7.1'de kozmetik olarak belgelenmiş).

`build-audit/airplay-gui.exe` ile çalışan `build-app/airplay-gui.exe` aynı boyutta (2 393 841 bayt) [Ö]. Bu, çalışan exe'nin HEAD'den derlendiğine işaret ediyor ama ikili karşılaştırma yapmadım [Ç].

### 2.2 Ek varyantlar [Ö]

```
build-audit-stub   (-DAIRPLAY_HOST_STUB=ON)   16/16 adım, EXIT=0, 0 uyarı
build-audit-strict (-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
                    -Wuseless-cast -Wduplicated-cond -Wlogical-op
                    -Wnull-dereference -Wformat=2)            EXIT=0, 7 tekil uyarı
```

### 2.3 Unit testler (ctest) [Ö]

```
$ ctest --output-on-failure           (build-audit)
1/2 Test #1: airplay_host_tests ...............   Passed    0.01 sec
2/2 Test #2: airplay_frame_tests ..............   Passed    0.01 sec
100% tests passed out of 2
$ ./tests/airplay_host_tests.exe   ->  OK: 32 tests, 146 checks, 0 failed
$ ./tests/airplay_frame_tests.exe  ->  OK: 8 tests, 67 checks, 0 failed
```

### 2.4 Canlı testler (ctest'e kayıtlı değil) [Ö]

`airplay_embed_live.exe` (gerçek `gst-launch videotestsrc ! d3d11videosink`):
```
findReceiverVideoWindow finds the sink window        PASS
guest 0000000000040860, client 1280x720
...
closing the picture hides it instead of releasing    PASS
...
the receiver process is still alive at the end       PASS
OK (0 failures)          -> 34 PASS satırı, EMBED_EXIT=0
```

`airplay_release_flash.exe`:
```
guest on the desktop wearing child styles : 0.000 ms
guest on the desktop at all, after adopt : 0.000 ms
our window on screen with no picture in it : 0.000 ms
guest on screen after we went away         : 0.000 ms
OK (0 failures)          -> 9 PASS, FLASH_EXIT=0
```

`airplay_host_live.exe /c/Users/pc/Desktop/airplay/build/uxplay.exe` (port 7300; çalışan GUI'nin alıcısı PID 1460 port 7100'de, çakışma olmadı):
```
HOME         : \airplay
GST_REGISTRY : \airplay\gst-registry.bin
started, pid = 13480
reached Waiting within 8 s : yes
  StateChanged Starting
  LogLine      start: C:\...\uxplay.exe -n AirPlay-LiveTest -nh -p 7300 -vs d3d11videosink -as autoaudiosink -nohold -fps 60 -FPSdata -reset 15
  Ports        UDP 7300 7301 7302  TCP 7300 7301 7302
  StateChanged Waiting
  StateChanged Stopping
  LogLine      (uxplay.exe:13480): GStreamer-CRITICAL **: ... gst_bus_add_watch_full: assertion 'GST_IS_BUS (bus)' failed   (x3)
  StateChanged Stopped
  LogLine      stop: graceful
RESULT        : PASS     HOST_EXIT=0
```

İki yan gözlem:
1. **Testin yan etkisi [Ö]:** MSYS2 login shell'inde `APPDATA`/`LOCALAPPDATA` boş geliyor. `test_host_live.cpp:151-152` bunları korumasız birleştirdiği için HOME `\airplay` oldu ve test **`C:\airplay\gst-registry.bin`** (1,5 MB) dosyasını yarattı. Dosyayı silmedim, elle silinebilir. Ürün kodu bu durumdan etkilenmiyor: `config_store.cpp` `SHGetKnownFolderPath` kullanıyor.
2. Alıcı başlatma sırasında durdurulunca 3 adet `GStreamer-CRITICAL gst_bus_add_watch_full` satırı basıyor [Ö]. Zararsız görünüyor, çünkü durdurma yine graceful tamamlandı. Kaynağı UxPlay'in yarım kalmış init yolu olabilir ama bunu doğrulamadım [Ç].

### 2.5 CI [Ö]

`gh run list`: son 5 koşu `success`, en sonuncusu 2026-08-27 (`8ddd3a9`). `05f0505` henüz push edilmediği için CI'da yok. CI (`.github/workflows/build.yml`) uxplay'i ve GUI'yi derleyip ctest koşuyor; canlı testleri ve `make-portable`/`make-installer`'ı koşmuyor.

### 2.6 `build-audit*` dizinleri

`build-audit/`, `build-audit-stub/` ve `build-audit-strict/` `.gitignore:2` (`build-*/`) kapsamında [Ö], silinmedi.

---

## 3. Derleyici uyarıları

| Bayrak seti | Sayı | Dağılım |
|---|---|---|
| Projenin kendi seti (`-Wall -Wextra`) | **0** | — |
| Sıkı set | **7** | `-Wsign-conversion` 4, `-Wconversion` 2, `-Wuseless-cast` 1 |

Sıkı set uyarıları [Ö]:
```
app/src/ui/tray.cpp:91:33 / :92:33   conversion to 'UINT' from 'long int' may change the sign   (MF_STRING | (running ? MF_GRAYED : 0))
app/src/ui/tray.cpp:103:15           useless cast to type 'int'                                  (TrackPopupMenu dönüşü)
app/tests/test_embed_live.cpp:72:44, :83:32   sign-conversion
app/tests/test_release_flash.cpp:47:24, :47:59  LONGLONG -> double
```
Bunların hiçbiri hata adayı değil. Yine de `tray.cpp` uyarıları tek satırlık düzeltmelerle sıfırlanabilir.

---

## 4. Kalite bulguları

### 4.1 Kritik

**K1. Güncelleyici, indirdiği kurulum dosyasını bütünlük ve imza kontrolü yapmadan sessiz kipte çalıştırıyor. [Ç]**
- `updater.cpp:235-270` `downloadFile` dosyayı `.part`'a yazıp yeniden adlandırıyor. Ne hash, ne boyut (`Content-Length`), ne de Authenticode kontrolü var.
- `main_window.cpp:617-619` dosyayı `ShellExecuteW(..., L"/SILENT /SUPPRESSMSGBOXES /NOCANCEL")` ile çalıştırıyor.
- Güven kökü yalnızca TLS (`updater.cpp:124` `WINHTTP_FLAG_SECURE`) ve GitHub hesabı. Repo **PUBLIC** [Ö: `gh repo view` → `"visibility":"PUBLIC"`]. Release'e `.exe` yükleyebilen biri (çalınmış token, ele geçirilmiş hesap) kullanıcı onayı tek "Evet" olan sessiz bir kod çalıştırma yolu elde ediyor. Kurulum dosyası imzasız (`installer/release-notes/v0.4.0.md` "Bilinen sınırlar").
- `downloadUrl` için şema/host doğrulaması da yok (`updater.cpp:226-231`): `.exe` ile biten her URL kabul ediliyor, `http://` dahil.
- Hafifletici etken [Ç]: Inno Setup kendi sıkıştırılmış verisini CRC ile doğruluyor, yani yarım inmiş dosya muhtemelen "bozuk kurulum" diye düşer. Bu, kasıtlı bir değişikliğe karşı koruma sağlamaz.

**K2. "Windows açılışında başlat" kutusu kurulumun Başlangıç kısayolunu bulamıyor. [Ç, makinede tetiklenmedi]**
- `autostart.cpp:25` `%APPDATA%\...\Startup\airplay.lnk` arıyor/siliyor (yorumu da `autostart.cpp:17` aynı adı iddia ediyor).
- `installer/airplay.iss:20,82` kısayolu `{userstartup}\{#MyAppName}` = **`AirPlay Alicisi.lnk`** adıyla yaratıyor.
- Sonuç: kurulumda "Windows açılışında başlat" seçildiyse kutu işaretsiz görünüyor (`isLaunchAtLogon()`, Run değeri yoksa yanlış döner). Kutu kaldırılınca kısayol silinmiyor ve uygulama girişte açılmaya devam ediyor. Bu, `docs/PHASE2-M2-SPEC.md:204-208` ile ve `installer/release-notes/v0.4.0.md:14` ("Kutuyu kaldırmak ... Başlangıç kısayolunu da temizliyor") ile çelişiyor.
- Ek olarak `airplay.iss:82` kısayolunda `Parameters: "-minimized"` yok: kurulum yolundan girişte açılan uygulama tepsiye değil pencereyle açılıyor. Bu da M2-SPEC §4'teki "-minimized" kararıyla çelişiyor.
- Bu makinede ne Run değeri ne de Başlangıç kısayolu var [Ö: `reg query ...\Run`, `Startup\` listesi], yani hata burada tetiklenmedi.

### 4.2 Önemli

**Ö1. `stop()` UI thread'ini blokluyor. [Ç]** `main_window.cpp:958` (`doStop`), `:990` (`doExit`), `:1394` (`WM_ENDSESSION`), `:1401` (`WM_DESTROY`) içinde `host_.stop()` UI thread'inde çağrılıyor. `uxplay_host.cpp:503` en fazla `graceMs`=3000 ms, ardından `:508` `WaitForSingleObject(INFINITE)` ve `reader.join()` bekliyor. Graceful yolda bekleme kısa (`host_live`'da anında kapandı [Ö]). Graceful olmayan yolda ise pencere 3 saniyeden fazla "Yanıt vermiyor" durumuna düşebilir.

**Ö2. Durum geçişinde yarış. [Ç]** `Impl::setState` (`uxplay_host.cpp:137-145`) "oku → karşılaştır → yaz" yapıyor ve atomik değil. Okuyucu thread `handleLine` içinde `cur`'u okuyup (`:217`) sonra `setState(Waiting/Connected)` diyor. Aynı anda UI thread `stop()` içinde `setState(Stopping)` diyebiliyor (`:478`). Kötü sıralamada `Stopping`'in ardından `Waiting` gelir, sonra `Stopped` gelir: UI kısa süre yanlış durum gösterir ve `StateChanged` olayları iki thread'den sırasız yayınlanır. Nihai durum doğru kalıyor (`:509-511`). Gerçekte gözlenmedi.

**Ö3. Kayıtlı pencere konumu ekran kontrolünden geçmiyor. [Ç]** `main_window.cpp:236-244` ve `video_window.cpp:230-235` `config.ini`'deki `x/y`'yi doğrudan kullanıyor. `MonitorFromRect(MONITOR_DEFAULTTONULL)` kontrolü yok. Kullanıcının iki monitörlü düzeninde bir ekran çıkarılırsa (geçmişte "yalnız Samsung" kaydı var) pencere ekran dışında açılır. Video penceresi `sizeToSource` içinde yalnız `rcWork` boyutuna sığdırılıyor, konum doğrulanmıyor.

**Ö4. `killStaleReceivers`, config'deki herhangi bir exe yolunun tüm örneklerini öldürebilir. [Ç]** `doStart` → `killStaleReceivers(cfg_.uxplayPath)` (`main_window.cpp:912`). Elle yazılmış `[app] uxplay_path` yalnızca `fileExists` ile kabul ediliyor (`config_store.cpp:229`). `stale_receivers.cpp:51-56` dosya adı ve tam yol eşleşmesine bakıp `TerminateProcess` çağırıyor. `uxplay_path=C:\Windows\explorer.exe` gibi bir hata Başlat'a basınca o programın tüm örneklerini kapatır (aynı kullanıcı, yani güvenlik değil bir tuzak). Dosya adının `uxplay.exe` olması şartı eklenmeli.

**Ö5. Job object ataması kontrol edilmiyor ve log'a yazılmıyor. [Ç]** `uxplay_host.cpp:436-443`: `SetInformationJobObject` ve `AssignProcessToJobObject` dönüşleri yok sayılıyor. Atama başarısız olursa "GUI ölünce alıcı da ölür" garantisi sessizce kayboluyor (`app/README.md:105-107` bu garantiye dayanıyor). Yorumdaki "Assign before the first resume-equivalent work" ifadesi de yanıltıcı: süreç `CREATE_SUSPENDED` ile başlatılmıyor (`:419`).

**Ö6. Sözleşme başlığı koda aykırı. [Ö+Ç]** `uxplay_host.h:116` "`buildArgs` Does NOT include argv[0]" diyor. `uxplay_host.cpp:308` ise argv[0]'ı ekliyor ve `test_line_parser.cpp` `testBuildArgsDefault` exe yolunun ilk eleman olmasını **doğruluyor** (test yeşil [Ö]). `main_window.cpp:970` iki durumu da tahmin ederek idare ediyor. Orkestratörün sahip olduğu sözleşmede yanlış bilgi var.

**Ö7. v0.4.0 sürüm notu ve kullanıcı belgeleri X davranışında yanlış. [Ç]** `c4fe3e9` ile görüntü penceresinin X'i artık pencereyi **gizliyor**, alıcıya geri vermiyor (`video_window.cpp:541-548`, `hide()` `:123-134`; `airplay_embed_live` "closing the picture hides it instead of releasing" PASS [Ö]). Şu belgeler hâlâ eski davranışı anlatıyor:
- `installer/release-notes/v0.4.0.md:10-11`: "X'e basmanız ... görüntü alıcının kendi penceresine döner".
- `app/README.md:131-132`: "hands the receiver's window back ... not adopted again until the next Start".
- `docs/MANUAL-VERIFY.md:57-58`: "görüntü alıcının kendi penceresine dönüyor (o oturumda tekrar içeri alınmıyor)".
- `video_window.h:62-63` yorumu: "the guest is already back on the desktop".

`docs/PHASE2-M2-SPEC.md:93-95` güncel. (Ayrıntı: §6.)

**Ö8. Sürüm sayfası URL'si doğrulanmadan `ShellExecute`'a gidiyor. [Ç]** `main_window.cpp:589` `ShellExecuteW(hwnd_, L"open", updatePageUrl_)`. Değer GitHub API yanıtındaki `html_url` (`updater.cpp:208`) ve şema kontrolü yok. API yanıtı ele geçirilirse `file:`/yerel yol açılabilir. `https://github.com/` ön eki şartı yeterli.

### 4.3 Küçük

1. **DPI değişiminde bir kontrolün fontu güncellenmiyor [Ç].** `main_window.cpp:1356-1362` `WM_DPICHANGED` listesinde `chkFrame_` yok. `createControls` listesinde (`:375-381`) var. İkinci monitöre geçince "Telefon çerçevesi çiz" kutusu eski DPI fontunda kalır.
2. **`VideoWindow` `WM_DPICHANGED`'i işlemiyor [Ç]** (`video_window.cpp:481-558`). Uygulama PerMonitorV2 (`app.manifest:28`), dolayısıyla farklı DPI'lı monitöre taşınan görüntü penceresi önerilen boyuta geçmez.
3. **Tekrarlanan yardımcılar [Ö: grep]:**
   - `narrow` ×3 (`uxplay_host.cpp:34`, `child_env.cpp:58`, `config_store.cpp:61`), `widen` ×2 (`uxplay_host.cpp:26`, `config_store.cpp:71`).
   - `ensureDir`/`ensureDirectoryW` ×2 (`config_store.cpp:102`, `child_env.cpp:72`).
   - `exeDir` ×2 (`config_store.cpp:117`, `test_host_live.cpp:61`).
   - **`quoteArg` ×2 ve farklı kurallarla:** `uxplay_host.cpp:48-69` MSVC kurallarına uyuyor, `main_window.cpp:129-138` tırnak öncesi ters bölü çiftlemiyor. "Komutu kopyala" (`:964-981`) bu yüzden bazı yollarda gerçekte çalıştırılan komut satırından farklı metin üretebilir.
4. **Kullanıcıya görünen ama `strings.h` dışında kalan İngilizce metinler [Ö: grep]:**
   - `main.cpp:62` "Failed to create the main window.".
   - `main_window.cpp:242` başlık "airplay - AirPlay Receiver" (hemen `updateStatus` ile eziliyor).
   - Host hata metinleri durum satırına ve MessageBox'a olduğu gibi gidiyor: `uxplay_host.cpp:362` "uxplay.exe not found: …", `:271-276` "uxplay.exe exited unexpectedly (exit code …)" → `main_window.cpp:769-770`, `:932-937`.
   - Updater hataları ("HTTP 404", "WinHttpSendRequest failed (…)") `kUpdateFailPre` arkasında gösteriliyor (`main_window.cpp:555-558`).
   - `strings.h:106-108` `kErrNoUxplay` son kullanıcıya `scripts/build.sh` öneriyor; kurulu sürümde anlamsız.
5. **Fonksiyon boyutları [Ö: awk]:** `parseUxplayLineDetailed` ~271 satır (`line_parser.cpp:119`), `MainWindow::wndProc` ~217 (`main_window.cpp:1195`), `onHostEvent` ~117 (`:997`), `UxplayHost::start` ~117 (`uxplay_host.cpp:337`), `createControls` ~88, `layout` ~78, `VideoWindow::wndProc` ~77, `onCommand` ~75. Parser'ın tablo güdümlü hâle getirilmesi en kolay ve en faydalı adım.
6. **Log döndürme dönüşü kontrol edilmiyor [Ç].** `ui_log.cpp:37` `MoveFileExW` başarısız olursa dosya döndürülmeden büyümeye devam eder (`size_` sıfırlandığı için 5 MB'ta bir yeniden denenir).
7. **`WM_ENDSESSION` config'i kaydetmiyor [Ç]** (`main_window.cpp:1390-1396`). Uygulama tepsideyken oturum kapanırsa görüntü penceresi konumu ve `picture_hint_shown` gibi değişiklikler yazılmaz (`doExit` çağrılmıyor).
8. **Kapanışta kuyruktaki `WM_HOST_EVENT`'ler sızıyor [Ç].** `PostQuitMessage`'dan sonra kuyrukta kalan `new HostEvent` kopyaları silinmiyor (`main_window.cpp:1240-1242`). Süreç zaten bittiği için zararsız.
9. **Ölü sinyal yolu [Ç].** `MirrorActivity` ayrıştırılıyor (`line_parser.cpp:139-152`) ama tüketicisi yok (`main_window.cpp:1083-1084`). Buna rağmen `patches/0004` her build'de bakımı süren bir yama olarak duruyor.
10. **Eskimiş kod yorumları [Ç]:**
    - `uxplay_host.cpp:483-486` "CtrlHandler ... returns FALSE for CTRL_BREAK ... without running cleanup()": `patches/0002` bunu düzeltti.
    - `uxplay_host.cpp:329-330` ve `test_line_parser.cpp` `testBuildArgsSmoothness` "stall detection depends on those reports": uyku/duraklatma kaldırıldı.
    - `main_window.h:130` `resolutionText_` "only ever filled when debug=true": evlat edinmede de dolduruluyor (`main_window.cpp:854`).
    - `autostart.cpp:17` (K2).
11. **Varsayılan tutarsızlığı [Ç].** `HostConfig::resetSeconds{15}` (`uxplay_host.h:85`) ile `AppConfig::resetSeconds = 0` (`config_store.h:49`) farklı. Ürün yolu config'ten geldiği için etkisi yok, ama sözleşmeyi okuyan biri 15 sanar.
12. **Test yan etkisi [Ö].** `test_host_live.cpp:151-152` `APPDATA` boşsa sürücü kökünde `\airplay` yaratıyor (§2.4).
13. **Mutex adı gerçek bir GUID değil [Ç]** (`single_instance.h` `"...-9d7e-airplay"`). Kozmetik: işlevsel olarak benzersiz.
14. **`config.ini`'de artık kullanılmayan anahtar [Ö].** Kullanıcının `%APPDATA%\airplay\config.ini` dosyasında `fps_data=1` var ve kod bunu okumuyor. Zararsız. (Kodlama sorun değil: `app.manifest:30` `activeCodePage=UTF-8`, INI UTF-8 olarak yazılıyor [Ö].)

### 4.4 Sızıntı ve thread incelemesi: temiz çıkanlar [Ç]

- **HANDLE:** pipe/NUL/proses/thread/job (`uxplay_host.cpp:147-153, 426-431`), snapshot ve proses (`stale_receivers.cpp:57,60`), updater thread (`main_window.cpp:536,547`), WinHTTP (RAII, `updater.cpp`) düzgün kapatılıyor.
- **GDI:** `WM_PAINT` fırça/kalemleri, `paintFrame`, fontlar (`~MainWindow`), bölge sahipliği (`video_window.cpp:318`) doğru. `LoadImageW(LR_SHARED)` doğru kullanılıyor.
- **Host → UI:** geri çağrı yalnızca `PostMessageW` yapıyor, olay kopyası heap'te ve sahipliği pencere prosedüründe (`main_window.cpp:1239-1243, 1285-1291`). Updater de aynı deseni izliyor (`:499-525`). HWND'ye başka thread'den dokunan yer bulmadım.
- **`stop()`'un geri çağrı içinden çağrılması:** yasak, belgelenmiş ve UI buna uyuyor.

---

## 5. Test kapsamı

| Modül | Satır [Ö] | Test | Tür | ctest/CI'da |
|---|---:|---|---|---|
| `host/line_parser.*` | 479 | `test_line_parser.cpp` (DESIGN §6.1'in her satırı + plist/stats/feedback) | unit | evet |
| `UxplayHost::buildArgs` | — | aynı dosyada 6 test | unit | evet |
| `UxplayHost::start/stop`, durum makinesi | ~430 | `test_host_live.cpp` | canlı, elle | **hayır** |
| `quoteArg` (host, anonim namespace) | 22 | yok (`testBuildArgsNameWithSpaces` yalnız vektörü test ediyor) | — | — |
| `host/child_env.*` | 195 | yalnızca `host_live` üzerinden dolaylı | — | hayır |
| `ui/device_frames.*` | 308 | `test_device_frames.cpp` (8 test, 67 kontrol) | unit | evet |
| `ui/video_embed.*`, `ui/video_window.*` | 156 + 666 | `test_embed_live` (34), `test_release_flash` (9) | canlı, GPU | **hayır** |
| `ui/config_store.*` | 434 | test binary'lerine link ediliyor ama load/save/portable mantığı test edilmiyor | — | — |
| `ui/updater.*` (`compareVersions`, JSON tarayıcı) | 326 | **yok**: saf fonksiyonlar, test etmek kolay | — | — |
| `ui/stale_receivers`, `autostart`, `single_instance`, `tray`, `ui_log` | 84+99+69+166+138 | yok | — | — |
| `ui/main_window.*` | 1554 | yok (yalnız elle/iPhone) | — | — |
| `ui/host_stub.cpp` | 178 | yalnız derleniyor (bu denetimde [Ö]) | — | hayır |

Okuyarak çıkarılan özet: saf mantığın yarısı test edilmiş (parser, argv, çerçeve geometrisi). Diğer yarısı test edilmemiş; özellikle updater sürüm/JSON, config portable kararı, komut satırı quoting ve autostart yolu. K2 tam olarak bu boşluktan kaçtı: kısayol adını installer ile karşılaştıran bir test olsaydı yakalardı.

---

## 6. Spec ile gerçek arasındaki farklar

| # | Belge | Ne diyor | Gerçek | Kaynak |
|---|---|---|---|---|
| 1 | `PHASE2-M2-SPEC.md:204-208`, `release-notes/v0.4.0.md:14` | Kutu kapatılınca kurulumun `Startup\airplay.lnk`'i silinir | Kurulum `AirPlay Alicisi.lnk` yaratıyor, silinmiyor (K2) | `airplay.iss:20,82`; `autostart.cpp:25` |
| 2 | `PHASE2-M2-SPEC.md:210-211` | Girişte `-minimized` ile tepsiye | Installer kısayolunda parametre yok | `airplay.iss:82` |
| 3 | `release-notes/v0.4.0.md:10-11`, `app/README.md:131-132`, `MANUAL-VERIFY.md:57-58` | X → görüntü alıcının penceresine döner | X → gizlenir, evlat edinme sürer | `video_window.cpp:541-548` |
| 4 | `PHASE2-M2-SPEC.md:194` | Satır: `mirror bitrate: <kbps> kbps <fps> fps` | Satır: `mirror stats: ...` | `line_parser.cpp:157`; `patches/README.md` doğru |
| 5 | `patches/README.md:136` (0004 bölümü) | "the GUI hides the frozen picture and mutes the receiver" | Kaldırıldı; `MirrorActivity` işlenmiyor | `main_window.cpp:1083-1084`; `app/README.md:175-186` |
| 6 | `patches/README.md:176` | "`scripts/build.sh` currently builds the submodule as-is" | build.sh sıfırlayıp yamalıyor (aynı bölümün devamı ve BUILD-NOTES §7.6 da böyle diyor) | — |
| 7 | `MANUAL-VERIFY.md:13-15` | Telefon uykusu → durum "Duraklatıldı", görüntü+ses kesilir | Böyle bir durum yok; GUI bilerek hiçbir şey yapmıyor | `app/README.md:169-184`; `strings.h` "Duraklatıldı" içermiyor |
| 8 | `MANUAL-VERIFY.md:17` | Günlükte `mirror stalled / mirror resumed` | Satırlar `mirror idle:` / `mirror active:` ve bunlar da log'a gidiyor ama UI'da işlenmiyor | `line_parser.cpp:139-152` |
| 9 | `MANUAL-VERIFY.md:44`, `SPEC.md:64` | `test_embed_live` "15/15", "15 kontrol" | Artık 34 kontrol (çerçeve + gizleme eklendi) | §2.4 [Ö] |
| 10 | `uxplay_host.h:116` | `buildArgs` argv[0] içermez | İçeriyor | Ö6 |
| 11 | `app/README.md:52-59` "Tests" | Yalnız `airplay_host_tests` + `host_live` | Ayrıca `airplay_frame_tests` (ctest), `embed_live`, `release_flash` | `app/tests/CMakeLists.txt` |
| 12 | `app/README.md:9-20` dizin ağacı | — | `updater`, `stale_receivers`, `single_instance`, `ui_log`, `tray` listede yok | — |
| 13 | `.github/workflows/build.yml` "BEST-EFFORT BUNDLING (Phase 3 work is not done here)" | Phase 3 yapılmadı | Phase 3 `make-portable.ps1` ile yapıldı; CI artefaktı ise hâlâ eksik kapanışlı | — |
| 14 | `PHASE2-UX-SPEC.md:39` | Çözünürlük yalnız Ayrıntılı günlük açıkken | M2 bunu aştı (tarihsel spec, M2-SPEC §2 bunu söylüyor) | bilgi amaçlı |

**`[x]` deyip kodda olmayan madde:** SPEC/M2/UX'teki `[x]` kutularının hepsi kodda karşılığını buldu. Tek istisna Phase 3 "Autostart" maddesinin "kurulumun bıraktığı Başlangıç kısayolu da siliniyor" kısmı (#1, K2).

**`MANUAL-VERIFY.md`'de hâlâ boş olanlar [Ö: dosya]:**
- Phase 0: 11 maddenin 4'ü doğrulanmış. Boş olanlar: döndürme, durdur/yeniden bağlan, uyku (#7 ile zaten eskimiş), FPS görünümü, `mirror stalled` (#8), ikon, 1 dakika kapalı tutma, gecikme, 30/60 FPS, çözücü karşılaştırması, FPSdata XML'inin görünmemesi.
- Phase 2: 8 maddenin **hepsi** boş.
- Phase 2 M2: 12 maddenin **hepsi** boş.
- Cihaz çerçevesi: 7 maddenin **hepsi** boş.
- Phase 3: 4 maddenin **hepsi** boş (temiz makinede self-contained, autostart, stale kill, zorla kapatmada yetim kalmama).

Toplam ~42 madde [M]. 0.4.0'ın ana özellikleri (görüntü penceresi, çerçeve) hiçbir zaman gerçek iPhone ile işaretlenmemiş.

---

## 7. Sürüm durumu

| Öğe | Değer | Kaynak |
|---|---|---|
| `project(... VERSION)` | **0.4.0** | `app/CMakeLists.txt:2` [Ö] |
| Son tag | **v0.3.0** (`c0710ab`, 2026-08-21) | `git tag` [Ö] |
| GitHub release | yalnız "AirPlay Alicisi 0.3.0", Latest, v0.3.0 | `gh release list` [Ö] |
| v0.3.0'dan beri commit | 42 | `git log v0.3.0..HEAD` [Ö] |
| Sürüm notu | `installer/release-notes/v0.4.0.md` var | [Ö] |
| HEAD push edildi mi | **hayır**, main origin'den 1 ileride (`05f0505`) | `git status -sb` [Ö] |
| Repo görünürlüğü | PUBLIC (0.3.0 kullanıcılarının güncelleme kontrolü çalışır) | [Ö] |

**0.4.0 yayına hazır mı? Hayır.** Engeller:
1. `publish-release.ps1` push edilmemiş HEAD'i reddediyor (`scripts/publish-release.ps1:59`), yani önce push gerekli. Kirli submodule sorun değil, çünkü `--ignore-submodules=all` kullanılıyor (`:54`).
2. **K2** düzeltilmeden sürüm notunun 14. satırı yanlış bir vaatte bulunuyor.
3. Sürüm notunun X davranışı satırları (Ö7) yanlış.
4. M2 ve çerçeve için iPhone kontrol listesi tamamen boş [M]. 0.4.0'ın iki başlık özelliği hiç elle doğrulanmamış.
5. (Güçlü öneri) K1: 0.4.0'ı otomatik güncelleme ile dağıtmadan önce indirme bütünlüğü.
6. `make-installer.ps1` Inno Setup gerektiriyor. Bu makinede kurulu olup olmadığını kontrol etmedim.

---

## 8. Güvenlik yüzeyi

| Yüzey | Durum | Değerlendirme |
|---|---|---|
| **Updater: indirme** | TLS var (`updater.cpp:124`). Hash, boyut ve imza yok. Şema/host allowlist yok. `/SILENT` ile çalıştırılıyor | **K1**, en büyük risk |
| **Updater: sayfa URL'si** | `html_url` doğrulanmadan `ShellExecute`'a gidiyor | **Ö8** |
| **Updater: JSON** | El yazması tarayıcı (`updater.cpp:31-89`). İlk eşleşen `tag_name`/`html_url`/`body` alınıyor. `\u` yalnız BMP. Gövde 4 MB'ta kesiliyor | Sağlam; bellek sınırlı. API yanıtı güvenilir kaynak sayıldığı için kabul edilebilir [Ç] |
| **Config parsing** | `GetPrivateProfileStringW` 1024 karakter. Sink/çözücü/fps/reset başlatma anında combobox üzerinden beyaz listeye zorlanıyor (`main_window.cpp:694-710`, `doStart` → `configFromControls`) | İyi. Yalnız `name`, `port` ve `uxplay_path` serbest geçiyor. `uxplay_path` aynı kullanıcı sınırında, ama bkz. Ö4 [Ç] |
| **Child süreç argümanları** | `CreateProcessW(lpApplicationName = tam yol, ...)`, shell yok. `quoteArg` MSVC kurallarına uygun (`uxplay_host.cpp:48-69`). Ad tek argv elemanı (`testBuildArgsNameWithSpaces` [Ö]) | Komut satırı enjeksiyonu yolu bulmadım [Ç]. `bInheritHandles=TRUE` GUI'nin tüm kalıtılabilir handle'larını aktarıyor (`:420-421`); `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` ile daraltılabilir (küçük) |
| **Ortam** | Üst ortam devralınıyor, `PATH` başına `<root>\ucrt64\bin` ekleniyor (`child_env.cpp:104-118`) | Portable kurulumda root = exe dizini, kullanıcı yazılabilir `%LOCALAPPDATA%\Programs`. Per-user kurulum için normal [Ç] |
| **stale_receivers** | Ad + tam yol eşleşmesi, `TerminateProcess`. Başka kullanıcının süreçleri `OpenProcess` reddiyle atlanıyor | Ö4: config yoluna bağlı tuzak. Ad kontrolü eklenmeli |
| **Ağdan gelen metin** | İstemci adı (iPhone'un kullanıcı adı) ve modeli alıcının stdout'una ve oradan parser'a, log'a, durum satırına gidiyor (`line_parser.cpp:272-295`) | UxPlay adı basmadan önce temizliyor mu doğrulamadım. Adında `\n` olan bir LAN istemcisi sahte bir satır (ör. `*** ERROR: ...`) enjekte edebilir. Etkisi yalnızca UI/log'da yanıltma [Ç] |
| **Single instance** | `Local\` mutex + `FindWindowW` | Oturum kapsamlı, uygun |
| **Yönetici izni** | `asInvoker` (`app.manifest:19`), installer `PrivilegesRequired=lowest` | Yükseltme yüzeyi yok |

---

## 9. Önerilen ilk 10 iş

Her biri 1-2 saatlik, bağımsız commit edilebilir. Sıra önem sırasına göre.

1. **Başlangıç kısayolu adını hizala (K2).** `installer/airplay.iss:82` kısayolu `{userstartup}\airplay` olarak yaratsın ve `Parameters: "-minimized"` eklensin. Ya da `autostart.cpp:25` her iki adı (`airplay.lnk`, `AirPlay Alicisi.lnk`) tanısın; önceki kurulumlar için ikincisi daha güvenli. `autostart.cpp:17` yorumunu düzelt. Kabul kriteri: kısayol varken kutu işaretli görünür, kaldırınca kısayol silinir.
2. **Updater URL doğrulaması (K1 ilk adım, Ö8).** `downloadUrl` ve `pageUrl` için `https://` + `github.com` / `objects.githubusercontent.com` allowlist'i (redirect sonrası host da WinHTTP'den okunarak). Uymazsa hata. `compareVersions` ve `findString` ile birlikte unit test edilebilir hâle getirilsin.
3. **Updater SHA-256 doğrulaması (K1).** `publish-release.ps1` release gövdesine (ya da `.sha256` asset'ine) installer hash'ini yazsın. `updater.cpp` indirdikten sonra BCrypt ile doğrulasın, eşleşmezse çalıştırmasın. (Authenticode bir sertifika gerektirir, sonraya bırakılabilir.)
4. **Belge kaymalarını tek commit'te düzelt (Ö6, Ö7, §6 #3-#13).** Kapsam: `release-notes/v0.4.0.md` X satırı, `app/README.md` X paragrafı ve Tests bölümü, `MANUAL-VERIFY.md` 13-17 ve 44, 57-58, `patches/README.md` 0004 son paragrafı ve "currently builds as-is", `PHASE2-M2-SPEC.md:194`, `uxplay_host.h:116`, eskimiş kod yorumları (§4.3-10).
5. **`stop()`'u UI thread'inden çıkar (Ö1).** `doStop` bir worker thread'de `host_.stop()` çağırsın ve bitince `WM_APP` post etsin; UI bu arada `Stopping` durumunda kalsın. `doExit` ve `WM_ENDSESSION` senkron kalabilir ama `WM_ENDSESSION` config'i kaydetsin (§4.3-7).
6. **Durum makinesini serileştir (Ö2).** `handleLine` içindeki "oku-karar ver-yaz"ı ve `stop()`'un `Stopping` yazımını bir state mutex'i ile koru (ya da `compare_exchange` kullan). `stopRequested` iken reader `Waiting`/`Connected`'a geçmesin. Mevcut `host_live` regresyon testi olarak yeterli.
7. **Pencere konumu doğrulaması (Ö3).** `main_window.cpp:236` ve `video_window.cpp:230` için `MonitorFromRect(..., MONITOR_DEFAULTTONULL)` kontrolü; null ise varsayılana dön. `device_frames` gibi saf bir yardımcı olarak yazılırsa unit test edilebilir.
8. **`killStaleReceivers` ve job güvenceleri (Ö4, Ö5).** Dosya adı `uxplay.exe` değilse hiçbir şey öldürme. `AssignProcessToJobObject` başarısız olursa `Warning` olayı yayınla. Yanıltıcı "resume" yorumunu düzelt.
9. **Yardımcıları birleştir ve quoting'i tekleştir (§4.3-3).** `narrow`/`widen`/`ensureDir` tek bir `app/src/common/text_util.*`'e taşınsın. Host'un `quoteArg`'ı (ya da tam `buildCommandLine`) public yapılsın ve "Komutu kopyala" onu kullansın. Ters bölü + tırnak vakaları için unit test eklensin.
10. **Küçük UI düzeltmeleri.** `WM_DPICHANGED` listesine `chkFrame_` (ideal olarak `createControls` ile aynı diziyi paylaşan tek bir liste), `VideoWindow` için `WM_DPICHANGED`, `main.cpp:62` ve host hata metinlerinin Türkçe karşılıkları `strings.h`'e, `kErrNoUxplay`'in son kullanıcı sürümü. Ayrıca `test_host_live.cpp:151-152`'te boş `APPDATA` koruması.

Sonraya kalanlar: `config_store` load/save + portable karar testleri, `parseUxplayLineDetailed`'in tablo güdümlü yapılması, `patches/0004`'ün gerekip gerekmediğine karar verilmesi, CI'da `make-portable` kuru koşusu, ~42 maddelik iPhone kontrol listesi [M].
