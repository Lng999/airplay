# airplay — Windows AirPlay Receiver (UxPlay tabanlı)

## Çalışma kuralları
- Kullanıcıya her zaman Türkçe yanıt ver; kod/komut/commit mesajları İngilizce.
- **Her değişiklik commit edilir.** En ufak düzenleme bile ayrı commit. `git add -A` YASAK — dosyaları tek tek ekle.
- Push ayrıca istenmedikçe yapılmaz.
- Orkestrasyon: Fable ana döngü (spec/karar/sentez), Opus ajanlar (keşif/araştırma/review), Codex lane'leri (spec'e göre icra). Bkz. docs/ORCHESTRATION.md
- Doğrulanmış vs tahmin edilmiş bilgiyi ayır. Protokol detayını UxPlay kaynağına `file:line` ile atıfla; uydurma yok.
- iPhone ile gerçek mirroring testi ajanlar tarafından yapılamaz → "MANUEL DOĞRULAMA GEREKLİ" olarak işaretle.

## Dizin düzeni
- docs/            — spec, tasarım, araştırma notları
- docs/research/   — ajan çıktıları (ham keşif)
- scripts/         — MSYS2 kurulum/build betikleri
- patches/         — upstream UxPlay'e karşı tutulan yamalar
- third_party/     — UxPlay (submodule, pinned)
- app/             — kendi GUI/wrapper kodumuz
