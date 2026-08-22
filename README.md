# airplay

Windows 11 için kişisel kullanımlık AirPlay Screen Mirroring alıcısı. Üst proje: [UxPlay](https://github.com/FDH2/UxPlay) (GPLv3).

Durum (2026-08-22): Phase 0 tamam (MSYS2/UCRT64 build + duman testi yeşil), **iPhone 13 → Windows 10 mirroring doğrulandı (2026-08-21)** — upstream dahili mDNS'in Windows'ta 127.0.0.1 ilan etme hatası `patches/0001` ile düzeltildi. Phase 2 **M2** bitti: görüntü artık uygulamanın kendi penceresinde (alıcının penceresi `SetParent` ile evlat ediniliyor), en-boy oranı kilitli boyutlandırma, F11 tam ekran, durum satırında çözünürlük + bit hızı + kare hızı. Phase 3'ten Windows açılışında başlatma da eklendi. Bkz. `docs/SPEC.md`, `docs/PHASE2-M2-SPEC.md`, `docs/BUILD-NOTES.md`, `docs/MANUAL-VERIFY.md`.

Tek tıkla çalıştırma: **`AirPlay.bat`** (çift tıkla; parametre geçirilebilir: `AirPlay.bat -Name "Salon-PC" -Debug`).

Hızlı başlangıç: `scripts/setup-msys2.ps1` → `scripts/build.sh` (UCRT64) → `scripts/build-app.sh` → `build-app/airplay-gui.exe` (veya `scripts/run-uxplay.ps1`).

## Kurulum (son kullanıcı)

[Releases](https://github.com/Lng999/airplay/releases/latest) sayfasından `AirPlay-Setup-x.y.z.exe` indirilip çalıştırılır. Yönetici izni istemez, `%LOCALAPPDATA%\Programs\AirPlay` altına kurulur, MSYS2/GStreamer gerektirmez. Uygulama açılışta yeni sürüm var mı diye bakar, varsa sorup indirip kurar (`[app] auto_update=0` ile kapatılır).

## Sürüm çıkarmak

1. `app/CMakeLists.txt` içindeki `project(airplay_gui VERSION x.y.z)` satırını yükseltin — sürüm sadece orada yazılıdır.
2. `scripts/build.sh` (gerekiyorsa) → `scripts/build-app.sh`
3. `installer/release-notes/vx.y.z.md` yazın (uygulama içindeki güncelleme penceresinde bu metnin ilk satırları gösterilir).
4. Commit + push.
5. `pwsh -File scripts/publish-release.ps1` — paketi ve kurulum dosyasını üretir, etiketi atar, release'i yayınlar.

## Başka bir bilgisayara taşımak

Depoyu kopyalamak yetmez: `uxplay.exe` bir MinGW ikilisidir, çalışmak için `C:\msys64\ucrt64` altındaki DLL'lere ve GStreamer eklentilerine ihtiyaç duyar. Karşı makinede MSYS2 olmadığı için uygulama **"uxplay.exe bulunamadı"** der. Çözüm, çalışma zamanını yanına alan bir paket üretmektir:

```powershell
pwsh -File scripts/make-portable.ps1 -Archive
```

Çıktı `dist\airplay-portable\` (ve `.zip`): iki exe + `ucrt64\` ağacı, ~232 MB. Klasörü olduğu gibi kopyalayın, `Baslat.bat` ile açın — kurulum gerekmez. Detay: `scripts/README.md`.
