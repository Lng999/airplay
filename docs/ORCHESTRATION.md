# Orchestration model

Güncelleme 2026-10-17: uzun soluklu geliştirme döngüsü başladı. Dört sabit rol, hepsi Opus
alt ajanı; patron Fable. Kullanıcı kararı: **Codex bu projede kapalı**, delege katmanı tek.

| Rol | Model | Görev | Çıktı |
|---|---|---|---|
| Patron (ana döngü) | Claude Fable 5.1 | Prompt'u yorumlar, spec/karar/sentez, entegrasyon, **her değişikliği commit eder** | `docs/*-SPEC.md`, commit'ler |
| Analist | Opus (sub-agent) | Sektör/rakip analizi, kullanıcı şikâyet kalıpları, fikir üretimi | `docs/research/sector-analysis-*.md` |
| Tasarımcı | Opus (sub-agent) | Arayüz tasarımı: ekran akışı, yerleşim, metinler, durumlar; kod yazmaz | `docs/UI-*.md` + ASCII/ölçülü mockup |
| Geliştirici | Opus (sub-agent) | Patron'un spec'ini uygular, derler, testleri koşar; git çalıştırmaz | kaynak kodu, testler |
| Denetçi | Opus (sub-agent) | Yazılan kodu ve commit'leri inceler: doğruluk, sızıntı, spec uyumu, commit hijyeni | `docs/research/review-*.md` |

Döngü: Analist + Denetçi (durum ölçümü) → Patron yol haritası + spec → Tasarımcı spec'i →
Geliştirici → Denetçi → Patron düzeltme/commit → sonraki parça.

Kurallar:
- Patron spec yazmadan icra lane'i açılmaz; lane'in kalitesi spec'in kalitesiyle sınırlıdır.
- Her ajan çıktısı `docs/research/` veya `docs/` altına yazılır ve patron tarafından SPEC'e işlenir.
- iPhone gerektiren doğrulama → `docs/MANUAL-VERIFY.md` checklist'ine gider; ajan "çalışıyor" diyemez.
- **Her değişiklik ayrı commit** (`git add <dosya>`; `git add -A` yasak). Ajanlar git'in yazma
  komutlarını çalıştırmaz (index.lock yarışı) — commit'i patron atar. Push ayrıca istenir.
- Her `Agent` çağrısında `model: 'opus'` açıkça yazılır; model miras bırakılmaz.
