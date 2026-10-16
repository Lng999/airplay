# UPSTREAM SYNC — pin'i `4e95a9e`'e taşıma (Sprint 0, Lane E)

Kaynak: `docs/research/upstream-delta-2026-10.md` (2026-10-17). Neden şimdi:
- Pinli ağaçta (`a3c19cbc`) **GHSA-479c-ww7g-wgp8**: `raop_handlers.h:366` sınırsız `memcpy`
  (stack taşması) + iki NULL deref; `/pair-setup-pin` koşulsuz dağıtıldığı için LAN'daki herkes
  kimlik doğrulamasız tetikleyebilir. Bu ağaçla 0.4.0 **yayınlanmaz**.
- Pin artık upstream master'ın atası değil (master ~08-25'te yeniden yazıldı); hiçbir ref tutmuyor,
  GC riski.
- iOS 27 düzeltmeleri (TEARDOWN `546820c`, AES-CTR re-key `f0b042b`), Windows mDNS düzeltmesi
  (`8d62eba`, bizim 0001'in muadili), Windows ses regresyon düzeltmesi (`4c86094`).

## [KARAR] Hedef `4e95a9ec013c52d8555d88dbc55abe346e94d56e` (master HEAD 2026-10-16)
Plan B (cherry-pick köprüsü) reddedildi: pin orphan kalır, iki kere iş.

## Yapılacaklar (geliştirici; bu lane'de git yazma izni YALNIZ `third_party/UxPlay` içinde)
1. Submodule: `git -C third_party/UxPlay fetch origin && git -C third_party/UxPlay checkout 4e95a9e`.
   Superproject'te `git add`/`commit` YOK — patron atar.
2. `patches/0001-*.patch` silinir. Gerekçe `patches/README.md`'ye: upstream `mdnsd.c:348-414`
   `GetAdaptersAddresses` ile aynı kök sorunu çözüyor; fark: gateway'li ilk adaptör, "Virtual"/
   "vEthernet" açıklamalılar elenir, fallback yok → Hyper-V External switch / VPN makinelerinde A kaydı
   çıkmayabilir [okuyarak çıkarım]. Bu risk `docs/MANUAL-VERIFY.md`'ye madde olarak girer.
3. 0002–0005 yeniden üretilir: scratch'teki 3-way rebase sonuçları
   `C:\Users\pc\AppData\Local\Temp\claude\C--Users-pc\978c9d6b-37c5-46e8-85b9-340d01f09e50\scratchpad\uxplay-rebased-patches\`
   (dosya adları `000N-<eski ad>.patch.patch`) kaynak alınabilir, ama **doğrulama şart**: temiz
   `4e95a9e` üstüne sırayla `git apply --check` ve eklenen/silinen satırların eski yamalarla
   birebir aynı olduğu (`git diff --no-index` ile hunk gövdeleri) raporda gösterilir.
   Dosya adları ve numaralar (0002–0005) korunur; `0001` numarası boş kalır.
4. `scripts/build.sh` ile derle (`build/uxplay.exe`; çalışan süreç yok, patron kapattı). 0 uyarı
   beklenmez (upstream kodu), ama **yeni** uyarı/hata varsa listele. `scripts/smoke-test.ps1` koş.
5. Parser uyumu: `build-audit/tests/airplay_host_live.exe /c/Users/pc/Desktop/airplay/build/uxplay.exe`
   koş (port 7300); `Waiting`'e ulaşmalı, graceful durmalı (0002). Yeni log satırları
   (`AirPlay volume …`, kernel timestamp) parser'ı bozmamalı — `app/` koduna dokunma, bozuyorsa raporla.
6. Belgeler: `patches/README.md` (pin hash, 0001 bölümü "emekli", yeni satır numaraları),
   `docs/DESIGN.md` başlık pin satırı (+ "1.74" sürüm iddiası hâlâ doğru mu: `uxplay.cpp` VERSION),
   `docs/SPEC.md` §2b pin satırı ve "0001 [KAPANDI]" → "0001 emekli (upstream 8d62eba)",
   `docs/BUILD-NOTES.md` hash geçen yerler, `README.md` durum paragrafına bir cümle,
   `docs/MANUAL-VERIFY.md`'ye yeni bölüm "Upstream 4e95a9e" — delta raporu §8'deki 8 madde `[ ]`
   olarak + Hyper-V/VPN adaptör riski.
7. `.github/workflows/build.yml` pin'e göre bir şey yapıyorsa kontrol et (muhtemelen `submodule
   update --init` yeterli).

## Dokunulmaz
`app/**` (Lane B/C çalışıyor), `installer/**`, `scripts/publish-release.ps1`.

## Bilinen riskler (release notuna girecek, geliştirici yazmaz)
Taze ses yolu (`4c86094` bir günlük), NTP katmanı hâlâ değişiyor, `httpd` 60 sn ölü bağlantı
zaman aşımı (`e60d981`), `d29dfab` 64 bayt over-read (düşük; upstream'e bildirilecek).
