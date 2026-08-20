# airplay

Windows 11 için kişisel kullanımlık AirPlay Screen Mirroring alıcısı. Üst proje: [UxPlay](https://github.com/FDH2/UxPlay) (GPLv3).

Durum (2026-08-20): Phase 0 tamam (MSYS2/UCRT64 build + duman testi yeşil), Phase 2 M1 GUI derleniyor ve çalışıyor; **iPhone 13 → Windows 10 mirroring doğrulandı (2026-08-21)** — upstream dahili mDNS'in Windows'ta 127.0.0.1 ilan etme hatası `patches/0001` ile düzeltildi. Bkz. `docs/SPEC.md`, `docs/BUILD-NOTES.md`, `docs/MANUAL-VERIFY.md`.

Hızlı başlangıç: `scripts/setup-msys2.ps1` → `scripts/build.sh` (UCRT64) → `scripts/build-app.sh` → `build-app/airplay-gui.exe` (veya `scripts/run-uxplay.ps1`).
