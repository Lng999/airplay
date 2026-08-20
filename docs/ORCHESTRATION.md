# Orchestration model

| Rol | Model | Görev |
|---|---|---|
| Patron (ana döngü) | Claude Fable 5 | Prompt'u yorumlar, spec/karar/sentez, entegrasyon, commit disiplini |
| Keşif ajanları | Opus (sub-agent) | Ortam envanteri, upstream kaynak haritası, web araştırması, review/verify |
| İcra lane'leri | **Opus** (sub-agent) | Tam spec verilmiş implementasyon, betikler, testler, GUI/core kodu |
| ~~Codex lane'leri~~ | ~~`codex exec`~~ | **Kullanıcı kararı (2026-08-20): bu projede Codex kapalı, tüm icra Opus.** |

Kurallar:
- Patron spec yazmadan icra lane'i açılmaz; lane'in kalitesi spec'in kalitesiyle sınırlıdır.
- Her ajan çıktısı `docs/research/` altına yazılır ve patron tarafından SPEC'e işlenir.
- iPhone gerektiren doğrulama → `docs/MANUAL-VERIFY.md` checklist'ine gider; ajan "çalışıyor" diyemez.
- Her değişiklik ayrı commit (`git add <dosya>`; `git add -A` yasak).
