# Denetim — Sprint 0, Lane A ve Lane D (`41b1f8f..3bb0f97`)

Denetçi: Opus (denetçi rolü), 2026-10-17. Kod değiştirilmedi, git'e yazılmadı.
Etiketler: **[Ö]** ölçüldü/çalıştırıldı, **[Ç]** kod/belge okunarak çıkarıldı, **[M]** manuel doğrulama gerekli.
Satır numaraları aksi yazılmadıkça `3bb0f97` ağacına göre.

## 1. Özet

**Karar: kabul, bir önemli düzeltmeyle (engelleyici yok).**

- Engelleyici: **0** · Önemli: **1** · Küçük: **8** · Övgü: 5
- Spec'teki A1, A2 ve A3 kabul kriterleri ile Lane D maddelerinin hepsi kodda ve belgede karşılığını buluyor.
- Derleme [Ö]: `3bb0f97` ağacı `git archive` ile scratchpad'e çıkarıldı (worktree açmadım, git'e yazmamak için) ve temiz bir dizinde derlendi: `ninja` 37/37 adım, **0 uyarı**, `ctest` 2/2 yeşil. Çalışma ağacındaki `build-audit` "no work to do" dedi; Lane B'nin commit edilmemiş `updater` değişiklikleriyle birlikte `ctest` 3/3 yeşil (`airplay_updater_tests` dahil). Lane B derlemeyi bozmuyor.
- Önemli bulgu (§3): Inno, sessiz yükseltmede önceki görev seçimini hatırlıyor. Bunu bu makinede deneyle doğruladım [Ö]. Görevin hatırlanması, istenen "kısayol kaybolmasın" sonucunu veriyor. Ama yeni bir sorun çıkıyor: kullanıcı uygulamadaki kutuyu kaldırıp kısayolu sildirse bile, bir sonraki otomatik güncelleme `airplay.lnk`'i **geri yaratıyor**. Kullanıcı kutuyu kaldırıp yeniden işaretlediyse de Run değeri ile kısayol birlikte kalıyor ve oturum açılışında uygulama iki kez başlıyor. İkinci örnek ilk pencereyi öne getirdiği için `-minimized` boşa çıkıyor.

## 2. Commit başına bulgular

### `f023aff` installer+autostart (A1, K2)

- **Övgü.** `autostart.cpp:20` iki adı tek bir tabloda topluyor. Okuma (`:45-52`) ve silme (`:54-63`) aynı tabloyu kullanıyor. `SHGetKnownFolderPath` başarısız olursa `startupDir()` boş dönüyor; `isLaunchAtLogon` false veriyor, `setLaunchAtLogon` kısayol adımını atlayıp yalnız registry'yi yazıyor. Bu durumlar doğru ele alınmış [Ç]. `CoTaskMemFree` her yolda çağrılıyor, sızıntı yok.
- **Övgü.** `airplay.iss:85` kısayolu `airplay` adıyla ve `Parameters: "-minimized"` ile yaratıyor (K2'nin iki yarısı da kapandı). `[InstallDelete]` (`:91-94`) Inno'nun kurulum sırasında `[Icons]`'tan önce işleniyor (Inno "Installation Order", aşağıda kaynak). Yani eski adı silmek yeni adı yaratmadan önce oluyor ve ikisi asla aynı anda kalmıyor.
- **ÖNEMLİ — kullanıcı kutuyu kaldırsa da güncelleme kısayolu geri getiriyor; Run değeriyle birlikte çift başlatma oluşabiliyor.** `airplay.iss:72` + `:85`. Ayrıntı §3'te. Kabul kriteri ("kaldırınca ikisi de gider") ilk anda sağlanıyor, ama bir sonraki `/SILENT` güncellemeye kadar.
  *Düzeltme önerisi:* Sessiz yolda kısayolu ancak zaten varsa yeniden yarat. `[Icons]` satırına `Check: WantStartupShortcut` ekle ve `[Code]` içinde şunu yaz:
  ```pascal
  var HadStartupShortcut: Boolean;
  function InitializeSetup(): Boolean;
  begin
    HadStartupShortcut := FileExists(ExpandConstant('{userstartup}\airplay.lnk')) or
                          FileExists(ExpandConstant('{userstartup}\AirPlay Alicisi.lnk'));
    Result := True;
  end;
  function WantStartupShortcut(): Boolean;
  begin
    // Interactive: honour the ticked task. Silent (the update path): only keep what exists,
    // and never add a second logon entry next to the app's own Run value.
    Result := (not WizardSilent() or HadStartupShortcut) and
              not RegValueExists(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'airplay');
  end;
  ```
  Uyarı: Bu kodu derleyip denemedim. `{userstartup}`'ın `InitializeSetup` içinde `ExpandConstant` ile çözülebildiğini Inno belgesinden ayrıca doğrulamak gerekiyor [Ç]. Alternatif bir çözüm de var: kurulum görevi kısayol yerine aynı `HKCU\...\Run\airplay` değerini yazar (`[Registry]` + `Tasks: startup`). Böylece tek mekanizma kalır. Fakat sessiz yükseltmede o değer de yeniden yazılır, yani aynı `Check` yine gerekir.
- **Küçük — "up to v0.4.0" yanlış.** `autostart.cpp:18`, `airplay.iss:92` ve commit mesajında geçiyor. v0.4.0 henüz yayınlanmadı (son tag `v0.3.0`, `git tag` [Ö]) ve düzeltme v0.4.0'ın içinde. Eski adı kullanan yayınlanmış tek kurulum v0.3.0 (`git show v0.3.0:installer/airplay.iss:82` [Ö]). *Öneri:* "Installers up to v0.3.0" ya da "before v0.4.0" yazılsın.
- **Küçük.** `autostart.h:3-5,14,18` hâlâ tekil "the installer's Startup shortcut" diyor. Artık iki ad tanınıyor; bir cümle eklenebilir.
- **Küçük (bu commit'ten önce de vardı).** `setLaunchAtLogon(true)` kısayolları registry yazımından **önce** siliyor (`:57-63`). `RegCreateKeyExW`/`RegSetValueExW` başarısız olursa kullanıcı iki mekanizmayı da kaybediyor. UI kutuyu `isLaunchAtLogon()` ile yeniden okuduğu için (`main_window.cpp:1162`) yanlış bir görüntü oluşmuyor. *Öneri:* `on` iken silme işlemi registry yazımı başarılı olduktan sonra yapılsın.
- **Not.** Kaldırmada Run değeri silinmiyor (`[Registry]`/`[UninstallRun]` yok). Uygulamanın kutusu işaretliyse kaldırmadan sonra var olmayan bir exe'yi gösteren bir Run değeri kalıyor. Bu bu aralığın konusu değil, sonraki iş olarak öneriyorum.
- **Test boşluğu.** Rapor §5, K2'nin "installer adı ile kod adını karşılaştıran bir test olsaydı yakalanırdı" boşluğundan kaçtığını söylüyor. Bu commit de böyle bir test eklemiyor. Spec istemediği için bulgu saymadım.

### `37439cb` stale_receivers (A2, Ö4)

- **Övgü.** Kilit, snapshot alınmadan önce ve büyük/küçük harf duyarsız (`stale_receivers.cpp:41`, `samePath`). Sondaki `\` ile biten bir yolda dosya adı boş kalıyor ve kilit reddediyor. `uxplay_path=C:\Windows\explorer.exe` için fonksiyon 0 dönüyor [Ç; çalıştırmadım].
- **Küçük.** `:46-48`'deki "Match on the file name first" yorumu, `leaf` hesabı yukarı taşındıktan sonra açıkladığı koddan bir boş satır ve snapshot çağrısıyla ayrılmış durumda. *Öneri:* Yorum `Process32FirstW` döngüsünün hemen üstüne alınsın ve boş satır silinsin.
- **Küçük.** Reddetme sessiz. Kullanıcı `explorer.exe` yazdıysa hiçbir öldürme olmuyor, ama ardından host o exe'yi "alıcı" olarak başlatıyor ve günlükte bunun izi kalmıyor. *Öneri:* Dönüşü `-1` gibi ayırt edilebilir bir değer yapıp `doStart` içinde `logUi("stale kill skipped: not uxplay.exe")` yazılsın, ya da `config_store.cpp:229` aynı ad kontrolünü uygulasın.

### `4ec2b2a` host job object (A3, Ö5)

- **Övgü.** `GetLastError` çağrı sırası doğru. Üç yerde de (`uxplay_host.cpp:443, 449, 451`) `GetLastError()` doğrudan `lastErrorText`'in argümanı olarak çağrılıyor ve araya başka bir Win32 çağrısı girmiyor. `lastErrorText` (`:71-76`) yalnız `snprintf` yapıyor. Başarısızlıkta job handle'ı kapatılıp `nullptr` yapılıyor (`:452-456`). Kapatılan handle bir daha kullanılmıyor, çünkü `hJob` zaten `CreateJobObjectW` başarısız olduğunda da `nullptr` olabiliyordu ve `stop()`/temizlik bunu önceden tolere ediyordu. Süreç yine başlatılıyor ve Warning olayı `setState(Starting)` ile `start:` logundan sonra yayınlanıyor (`:465-471`). Bu, `envWarn` deseniyle (`:372-377`) aynı. UI Warning olayını `WARNING: ...` olarak günlüğe yazıyor (`main_window.cpp:1104-1106`).
- **Övgü.** `:505-508` CtrlHandler yorumu doğrulandı. `patches/0002` `CTRL_BREAK_EVENT` durumunu ekliyor ve yamalı alt modülde bu satır `uxplay.cpp:665` [Ö: `grep`].
- **Küçük — yorum fazla iddialı.** `:436-437`: "the job only has to be in place before we could die" ve "that is fine". `CreateProcessW` ile `AssignProcessToJobObject` arasında küçük bir pencere var: GUI bu arada ölürse alıcı yetim kalır. Kısa bir pencere, ama "her durumda" garantisi tam değil. *Öneri:* `CREATE_SUSPENDED` + atamadan sonra `ResumeThread` kullanılsın (gerçek garanti), ya da yorum "a narrow window remains between CreateProcessW and the assignment" diye yumuşatılsın.
- **Küçük.** `:329-331` "The reports feed the fps figure ... (MirrorFps)" doğru ama eksik. Durum satırındaki fps `MirrorStats` (patches/0005) ile de güncelleniyor (`main_window.cpp:1093`) ve 0005 varken asıl kaynak o. *Öneri:* "... the fps figure (MirrorFps; MirrorStats from patches/0005 updates it too)".
- **Küçük, kapsam dışında kalan.** Rapor §4.3-10'un saydığı `test_line_parser.cpp:529` "the stall detection depends on those reports" yorumu ve `:412` "the fast stall signal" hâlâ eskimiş. Spec A3 yalnız `uxplay_host.cpp`'yi saydığı için bulgu değil, not olarak yazıyorum.

### `e4b6aa3` belgeler: X davranışı, README ağacı, testler, uyku maddesi (Lane D, Ö7, §6 #3/#7/#8/#11/#12)

- **Övgü.** X davranışı üç belgede de koda uygun (bkz. §4 tablosu).
- **Küçük.** `app/README.md:216-217` uzun bir satır (Tray menüsü listesi). Dosyanın geri kalanı ~95 sütunda kırılıyor. *Öneri:* Satır kırılsın.
- **Küçük.** `app/README.md:17` "Start kills any leftover uxplay.exe" fazla geniş. Kod yalnızca **aynı tam yoldaki** `uxplay.exe`'leri öldürüyor (`stale_receivers.cpp:62`); başka dizindeki bir `uxplay.exe`'ye dokunmuyor. *Öneri:* "any leftover uxplay.exe at our receiver's path".
- **Not.** `app/README.md:148` "ticking *Görüntüyü uygulamada göster* again" doğru (`main_window.cpp:864-868`). Ancak kutu zaten işaretli olduğu için önce kaldırmak gerekiyor ve kaldırmak `releaseVideo(true)` ile görüntüyü bir anlığına masaüstüne veriyor. Belge bu ara adımı söylemiyor. Hata değil.

### `85abe04` belgeler: spec ve patches (Lane D, §6 #4/#5/#6/#9)

- **Övgü.** `patches/README.md` "build.sh re-patches" ifadesi `scripts/build.sh:50-60, 82-83` ile birebir uyuşuyor (koşulsuz `checkout -- .` + `clean -qfd` + sıralı `apply`). 0004 için "no consumer" (`main_window.cpp:1083-1084`) ve "still reach the log" (`line_parser.cpp:123` her satırı `LogLine` yapıyor) doğru.
- Bulgu yok.

### `3bb0f97` başlık yorumları (Lane D, Ö6, §4.3-10)

- Üç yorum da kodla uyuşuyor. `uxplay_host.h:116` ↔ `uxplay_host.cpp:308`. `video_window.h:62-63` ↔ `video_window.cpp:541-548`. `main_window.h:130` ↔ `main_window.cpp:854` (evlat edinme) ve `:1065` (`-d` çözünürlük satırı). Bulgu yok.

## 3. Installer sessiz yükseltme analizi

**Soru:** Güncelleme yolunda (`main_window.cpp:617-619` → `/SILENT /SUPPRESSMSGBOXES /NOCANCEL`, `/TASKS` yok) Inno önceki görev seçimini hatırlıyor mu? Hatırlamıyorsa `[InstallDelete]` eski kısayolu siler, `unchecked` görev de yenisini yaratmaz ve kullanıcı otomatik başlatmayı kaybeder.

**`.iss [Setup]` [Ç]:** `UsePreviousTasks` yazılmamış, varsayılanı geçerli. `Uninstallable` yazılmamış (varsayılan yes). `AppId` sabit (`airplay.iss:27`). Görevde `Flags: unchecked` var, `checkedonce` yok (`:72`).

**Belge [kaynaklı]:**
- `UsePreviousTasks` — "When this directive is yes, the default, at startup Setup will look in the registry to see if the same application is already installed, and if so, it will use the task settings of the previous installation as the default settings presented to the user in the wizard." Varsayılan: yes. `Uninstallable=no` ise çalışmaz. <https://jrsoftware.org/ishelp/topic_setup_useprevioustasks.htm>
- `/MERGETASKS` — "If UsePreviousTasks is set to yes, the specified tasks will be selected/deselected after any previous tasks are restored." Bu, komut satırı (tipik olarak sessiz) kurulumlarda da önceki görevlerin geri yüklendiğini dolaylı olarak gösteriyor. <https://jrsoftware.org/ishelp/topic_setupcmdline.htm>
- Kurulum sırası: "[InstallDelete] is processed." → … → "[Files] is processed." → "[Icons] is processed." <https://jrsoftware.org/ishelp/topic_installorder.htm>
- Belgede `/SILENT` altında görevlerin geri yüklendiğine dair **açık** bir cümle bulamadım. Bu yüzden deneye başvurdum.

**Bu makinede durum [Ö]:** v0.3.0 kurulu ve `HKCU\...\Uninstall\{7C2F4A61-...}_is1` altında `Inno Setup: Selected Tasks = desktopicon`, `Inno Setup: Deselected Tasks = startup` değerleri var.

**Deney [Ö]:** Scratchpad'de küçük bir `.iss` hazırladım (ayrı AppId, `PrivilegesRequired=lowest`, `startup` görevi `unchecked`, `{app}` altında görevli bir kısayol, `[InstallDelete]` var). Bu makinedeki Inno Setup 6 ISCC ile derledim.
1. `/VERYSILENT /TASKS=startup` → kısayol yaratıldı.
2. Kısayolu elle sildim; bu, uygulamanın kutu kaldırılınca yaptığı işi taklit ediyor.
3. `/SILENT /SUPPRESSMSGBOXES /NOCANCEL` (güncelleme yolunun birebir argümanları, `/TASKS` yok) → **kısayol yeniden yaratıldı**. Registry: `Selected Tasks = startup`.
4. `unins000.exe /VERYSILENT` ile kaldırdım; dizin ve Uninstall anahtarı gitti (doğrulandı).

**Sonuç:**
- Endişe edilen kayıp **olmuyor**. v0.3.0'da `startup` seçmiş bir kullanıcı sessizce yükseltildiğinde eski `AirPlay Alicisi.lnk` silinir, `airplay.lnk -minimized` yaratılır. Görevi seçmemiş bir kullanıcıda iki adımda da hiçbir şey olmaz.
- Asıl sorun bunun tersi. Inno "kullanıcı bu görevi istedi" bilgisini kendi registry'sinde tutuyor ve uygulamanın kutusu bu bilgiyi değiştirmiyor. Bunun iki sonucu var:
  1. Kutuyu kaldıran kullanıcı, sonraki her otomatik güncellemede `airplay.lnk`'i geri alıyor. Uygulama yeniden oturum açılışında başlıyor ve kutu yeniden işaretli görünüyor.
  2. Kutuyu kaldırıp yeniden işaretleyen kullanıcıda Run değeri (`autostart.cpp:76`) ile güncellemenin yarattığı `airplay.lnk` birlikte bulunuyor. Oturum açılışında iki örnek başlıyor. İkincisi `activateExistingInstance` çağırıyor (`main.cpp`, `single_instance.cpp:25-33`: `ShowWindow(SW_SHOW)` + `SetForegroundWindow`) ve tepside duran pencere öne geliyor. Hangi örneğin önce başladığına bağlı olarak bu her açılışta olmayabilir [Ç].
- Bu sorun 0.3.0→0.4.0 geçişinde ortaya çıkmıyor (0.3.0'da kutu yok). İlk kez **0.4.0'dan sonraki ilk güncellemede** görülür ve düzeltmesi o güncellemeyi yapan kurulum dosyasının `.iss`'inde olmalı. Bu yüzden 0.4.0 için engelleyici saymadım. Önerilen düzeltme §2 `f023aff` altında.

## 4. Belge doğrulama tablosu

| Belge:satır | İddia | Kaynak | Sonuç |
|---|---|---|---|
| `release-notes/v0.4.0.md:10` | X yansıtmayı bitirmez, pencereyi gizler, ses sürer; geri getirmek için tepside `Görüntüyü göster` | `video_window.cpp:541-548`, `hide()` `:122-134`; `tray.cpp:89`; `strings.h:19` | Doğru |
| `release-notes/v0.4.0.md:11` | Kutuyu kaldırınca alıcının kendi penceresi (eski davranış) | `main_window.cpp:869-873` `releaseVideo(true)` | Doğru |
| `release-notes/v0.4.0.md:14` | Kutuyu kaldırmak kurulumun Başlangıç kısayolunu da temizliyor | `autostart.cpp:57-63` + `airplay.iss:85` | Artık doğru. Bir sonraki güncellemede geri gelir (§3) |
| `app/README.md:144-149` | Gizleme; konuk evlat edinilmiş kalır ve "goes dark with it"; tepside menü öğesi yalnız gizliyken; kutu yeniden işaretlenirse geri gelir; ilk seferde balon `picture_hint_shown` | `video_window.cpp:128-130`; `tray.cpp:89`; `main_window.cpp:866-868, 1250-1252` | Doğru (kutu notu §2'de) |
| `app/README.md:157-158` | embed_live gizlemeyi test ediyor | rapor §2.4 "closing the picture hides it instead of releasing PASS" | Doğru |
| `app/README.md:13-26` dizin ağacı | updater, stale_receivers, host_stub (`-DAIRPLAY_HOST_STUB=ON`), tests açıklaması | `app/CMakeLists.txt:15,30-41`; `tests/CMakeLists.txt` | Doğru; `:17` "any leftover" geniş (küçük) |
| `app/README.md:64-72` Tests | ctest = host_tests + frame_tests; üç canlı test, argümanları; 34 ve 9 kontrol; son ikisi GPU istiyor | `tests/CMakeLists.txt` (add_test ×2, yorumlar); rapor §2.3-2.4 | Doğru |
| `app/README.md:215-217` | Tray menüsü: Göster / Görüntüyü göster / Başlat / Durdur / Güncellemeleri kontrol et / Çıkış | `tray.cpp:88-96`; `strings.h:23` | Doğru |
| `app/README.md:279` | `picture_hint_shown` ini anahtarı | `main_window.cpp:1250-1252` | Doğru (`config_store` yazımını ayrıca açmadım) |
| `MANUAL-VERIFY.md:13-16` | Uykuda GUI hiçbir şey yapmaz; `Bağlandı`, yeşil nokta; son kare donuk | `strings.h:41`; `main_window.cpp:115`; `:1083-1084`; `app/README.md:187-196` | Kodla tutarlı; "son kare donuk" [M] |
| `MANUAL-VERIFY.md:18-19` | `mirror idle:`/`mirror active:` günlüğe gidiyor, arayüz tepki vermiyor | `line_parser.cpp:123` (LogLine her zaman), `:139-152`; 0004 `LOGI`; `uxplay.cpp:153` varsayılan `LOGGER_INFO` | Doğru [Ç] |
| `MANUAL-VERIFY.md:45-48` | ilk 15/15, sonra 34/34 PASS (2026-10-17) | rapor §2.4 "34 PASS satırı" | Doğru |
| `MANUAL-VERIFY.md:61-63` | X → oturum + ses sürer, gizlenir, ilk seferde balon, tepside `Görüntüyü göster` | yukarıdakilerle aynı | Doğru |
| `PHASE2-M2-SPEC.md:194-196` | `mirror stats: <kbps> kbps <fps> fps` → `MirrorStats`; fps de buradan | `patches/0005:47`; `line_parser.cpp:157-169`; `main_window.cpp:1091-1095` | Doğru |
| `SPEC.md:64-65` | 34 kontrol (ilk 15) | rapor §2.4 | Doğru |
| `patches/README.md:135-140` | ~yarım saniye; GUI tepki vermiyor; tüketici yok; satırlar günlüğe gidiyor; karar açık | patch metni (400 ms); `main_window.cpp:1083-1084`; `line_parser.cpp:123`; SPRINT0 "Sonraya" | Doğru |
| `patches/README.md:180-183` | build.sh her build'den önce sıfırlayıp tüm yamaları uyguluyor (`apply_patches()`) | `scripts/build.sh:50-60, 82-83` | Doğru |
| `uxplay_host.h:116` | Eleman 0 = argv[0] | `uxplay_host.cpp:308` | Doğru |
| `video_window.h:62-63` | Gizli; konuk evlat edinilmiş; `showPicture()` geri getirir | `video_window.cpp:541-548, 136-` | Doğru |
| `main_window.h:130` | Evlat edinmede ya da `-d` çözünürlük satırından dolar | `main_window.cpp:854, 1065` | Doğru |
| `uxplay_host.cpp:505-508` | 0002 `CTRL_BREAK`'i ekliyor, yamalı `uxplay.cpp:665` | patch 0002; yamalı alt modülde `uxplay.cpp:665` [Ö] | Doğru |
| `uxplay_host.cpp:329-331` | `-FPSdata` fps göstergesini besliyor (`MirrorFps`), artık hiçbir şey durmuyor | `uxplay_host.cpp:175-185`; `main_window.cpp:1097-1100`; upstream `uxplay.cpp:1450-1451` | Doğru ama eksik (MirrorStats) |

Görev metnindeki `main_window.cpp:1080-1084` aralığı [Ç]: `case K::MirrorActivity: break;` ve üstünde "Nothing acts on it" yorumu var. Belgelerdeki "tüketici yok" iddiasını destekliyor. Hemen altında (`:1086-1090`) MirrorFps'e ait bir yorum MirrorStats `case`'inin üstünde duruyor. Bu yer değiştirmiş yorum bu aralıkta eklenmedi, sonraki temizliğe not.

## 5. Commit hijyeni

- Altı commit'te de `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` var. Yazar `Lng999`.
- Her commit spec'te tek bir maddeye karşılık geliyor (A1, A2, A3, D×3). A1'de installer ile kodun aynı commit'te olması doğru, çünkü değişiklik birbirine bağlı.
- `4ec2b2a` job object düzeltmesiyle iki eski yorumu birlikte taşıyor. Spec A3 bunu açıkça istiyor ve mesaj söylüyor. Uygun.
- `e4b6aa3` geniş: X davranışı, README ağacı, testler, uyku maddesi, `mirror idle` satırı, tray menüsü, ini anahtarı. CLAUDE.md "en ufak düzenleme bile ayrı commit" diyor, SPRINT0 "her madde ayrı commit" diyor. Lane D tek madde sayıldığı için kabul edilebilir, ama CLAUDE.md'nin lafzına göre sınırda.
- **Mesaj/içerik uyuşmazlığı (küçük):** `e4b6aa3` "the four test binaries" diyor, README ise beş binary listeliyor (2 unit + 3 canlı) ve bunlardan üçü yeni. "34 kontrol" düzeltmesi iki commit'e bölünmüş: `MANUAL-VERIFY.md` `e4b6aa3`'te, `SPEC.md` `85abe04`'te. `85abe04`'ün başlığı yalnız onu anıyor. Tray menüsü ve `picture_hint_shown` eklemeleri `e4b6aa3` mesajında geçmiyor.
- `f023aff` mesajındaki "installs up to 0.4.0" yanlış (§2).
- Commit'lerde alakasız dosya yok. Lane B'nin çalışma ağacındaki `updater.*` değişiklikleri ve alt modül bu aralığa girmemiş.
- Üç commit 02:12:25'te, üçü 02:12:38'de: toplu atılmış. Her commit tek başına derleniyor mu, ayrı ayrı denemedim. Yalnız uç noktayı (`3bb0f97`) derledim.

## Doğrulayamadıklarım

- Gerçek AirPlay kurulumunun sessiz yükseltmesini (0.3.0 → 0.4.0) çalıştırmadım. §3'teki sonuç aynı Inno mekanizmasını kullanan küçük bir test kurulumuyla [Ö]. Gerçek kısayolun `-minimized` ile tepsiye açıldığı [M].
- A1 ve A2 kabul kriterlerini çalıştırarak denemedim (kutunun iki adla işaretli görünmesi, `explorer.exe` yolu). Sonuçlar kod okumasına dayanıyor [Ç].
- Job object başarısızlık yolu tetiklenmedi [Ç].
- §2'deki önerilen `[Code]` parçası derlenmedi.
