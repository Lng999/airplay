# SPRINT 0 — Hijyen (0.4.0'ı yayınlanabilir yapmak)

Kaynak: `docs/research/codebase-state-2026-10.md` (denetçi raporu, 2026-10-17). Bu sprint
upstream senkronundan **bağımsız**; yeni özellik yok, yalnızca raporun K1/K2/Ö1-Ö8 bulguları.
Her madde ayrı commit. Patron commit atar; geliştirici git'in yazma komutlarını çalıştırmaz.

Build/test: `build-audit/` dizininde (`build-app/airplay-gui.exe` çalışıyor olabilir, ona
dokunulmaz). Her maddeden sonra `ninja -C build-audit` 0 uyarı + `ctest` yeşil şart.

## Lane A — kısayol, stale kill, job object (küçük, bağımsız)

### A1. Başlangıç kısayolu adı (K2)
- `installer/airplay.iss` kısayolu `{userstartup}\airplay` adıyla ve `Parameters: "-minimized"`
  ile yaratsın.
- `autostart.cpp` hem `airplay.lnk` hem eski `AirPlay Alicisi.lnk` adını tanısın: `isLaunchAtLogon()`
  ikisinden biri varsa da true; kutu kaldırılınca ikisi de silinsin. `autostart.cpp:17` yorumu düzelsin.
- Kabul: kısayol hangi adla olursa olsun kutu işaretli görünür; kaldırınca ikisi de gider.

### A2. `killStaleReceivers` ad kilidi (Ö4)
- `stale_receivers.cpp`: hedef yolun dosya adı (büyük/küçük harf duyarsız) `uxplay.exe` değilse
  hiçbir süreci öldürme, 0 döndür. Kabul: `uxplay_path=C:\Windows\explorer.exe` ile Başlat
  explorer'ı kapatmaz.

### A3. Job object güvencesi (Ö5)
- `uxplay_host.cpp:436-443`: `SetInformationJobObject` ve `AssignProcessToJobObject` dönüşleri
  kontrol edilsin; başarısızsa `GetLastError` ile bir `Warning` olayı yayınlansın (mevcut olay
  türlerine bak; yoksa `LogLine` ile "warning: ..." yeterli). Süreç yine başlatılır (job yalnız
  güvence). Yanıltıcı "resume-equivalent" yorumu düzelsin.
- Aynı dosyadaki eskimiş yorumlar da bu maddede: `:483-486` (CtrlHandler, 0002 düzeltti),
  `:329-330` (stall detection kalktı).

## Lane B — updater sertleştirme (K1, Ö8)

### B1. URL allowlist
- `updater.cpp`: `downloadUrl` ve `pageUrl` yalnız `https://` + host ∈ {`github.com`,
  `objects.githubusercontent.com`, `release-assets.githubusercontent.com`} ise kabul; aksi
  hâlde güncelleme "atlandı" (hata değil, log satırı). Yönlendirme sonrası nihai host'u da
  WinHTTP'den (`WINHTTP_OPTION_URL`) okuyup aynı listeye karşı kontrol et.
- Doğrulama fonksiyonu saf olsun (`bool isAllowedUpdateUrl(const std::wstring&)`) ve
  `app/tests/test_updater.cpp` ile unit test edilsin (ctest'e ekle): http, file:, başka host,
  büyük/küçük harf, port'lu host, userinfo (`https://github.com@evil/`) vakaları.
- `main_window.cpp:589` `ShellExecuteW(... updatePageUrl_)` öncesi aynı kontrol.

### B2. SHA-256 doğrulaması
- `scripts/publish-release.ps1`: installer'ın SHA-256'sını hesaplayıp release'e
  `AirPlay-Setup-x.y.z.exe.sha256` adlı asset olarak yüklesin (içerik: `<hex>  <dosyaadı>`),
  ayrıca release gövdesinin sonuna `SHA-256: <hex>` satırı eklesin.
- `updater.cpp`: release asset listesinden `.sha256` asset'ini bulup indir; `.exe`'yi indirdikten
  sonra BCrypt (`BCRYPT_SHA256_ALGORITHM`) ile hash'le; eşleşmezse `.part` dosyasını sil,
  "güncelleme bütünlük kontrolünden geçmedi" hatası ver, ASLA çalıştırma. `.sha256` asset'i yoksa
  (eski release) → güncellemeyi **atla** ve log'a yaz (geriye uyum için çalıştırma değil, atlama).
- Hash karşılaştırması ve `.sha256` içerik ayrıştırması saf fonksiyon + unit test.
- `compareVersions` ve JSON `findString` için de aynı test dosyasında birkaç test (ucuz, kapsama
  boşluğunu kapatır).
- `installer/release-notes/v0.4.0.md` "Bilinen sınırlar"a bir satır: indirme SHA-256 ile doğrulanıyor,
  Authenticode imzası hâlâ yok.

## Lane C — host thread ve durum (Ö1, Ö2, Ö3, küçük-7)

### C1. `stop()` UI thread dışına
- `doStop`: `host_.stop()` bir worker thread'de koşsun, bitince `WM_APP+N` post edilsin; UI o
  sırada `Stopping` gösterir, Başlat/Durdur düğmesi gri. `doExit` ve `WM_DESTROY` senkron
  kalabilir. İkinci bir Durdur tıklaması yoksayılır.
### C2. Durum geçişi serileştirme
- `uxplay_host.cpp` `setState` oku-karşılaştır-yaz'ı mutex altına al; `stopRequested` true iken
  reader `Waiting`/`Connected`'a geçemesin. `host_live` regresyon testi yeşil kalmalı.
### C3. Pencere konumu doğrulaması
- Saf yardımcı `bool rectOnSomeMonitor(const RECT&)` (`MonitorFromRect` `MONITOR_DEFAULTTONULL`);
  `main_window.cpp:236` ve `video_window.cpp:230` kayıtlı konumu kullanmadan önce kontrol etsin,
  değilse varsayılan konum.
### C4. `WM_ENDSESSION` config kaydetsin (küçük-7).

## Lane D — belge kaymaları (rapor §6, Ö6, Ö7) — yalnız .md ve .h yorumları
- `installer/release-notes/v0.4.0.md:10-11` X davranışı (gizlenir, alıcıya dönmez).
- `app/README.md:131-132` aynı; Tests bölümü (frame_tests, embed_live 34, release_flash);
  dizin ağacına eksik dosyalar.
- `docs/MANUAL-VERIFY.md:13-17` (Duraklatıldı durumu yok; `mirror idle/active`), `:44` (34 kontrol),
  `:57-58` X davranışı.
- `patches/README.md` 0004 son paragrafı (GUI artık sinyali kullanmıyor) ve "builds as-is".
- `docs/PHASE2-M2-SPEC.md:194` `mirror stats:`.
- `app/include/airplay/uxplay_host.h:116` argv[0] dahil.
- `app/src/ui/video_window.h:62-63`, `main_window.h:130` yorumları.
- `docs/SPEC.md:64` "15 kontrol" → 34.
- Dokunulmaz: `uxplay_host.cpp` (Lane A'nın), `updater.cpp`, `main_window.cpp`.

## Sonraya (bu sprintte değil)
Yardımcıları birleştirme (§4.3-3), DPI küçükleri, strings.h'e kaçak İngilizce metinler, parser
tablo güdümlü, `patches/0004` kararı, iPhone kontrol listesi [M].
