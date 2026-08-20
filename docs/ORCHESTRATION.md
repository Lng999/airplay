# Orchestration model

| Rol | Model | Görev |
|---|---|---|
| Patron (ana döngü) | Claude Fable 5 | Prompt'u yorumlar, spec/karar/sentez, entegrasyon, commit disiplini |
| Keşif ajanları | Opus (sub-agent) | Ortam envanteri, upstream kaynak haritası, web araştırması, review/verify |
| İcra lane'leri | Codex `gpt-5.6-sol` xhigh (`codex exec`) | Tam spec verilmiş implementasyon, betikler, testler |
| Hafif icra | Opus | Şablon işler, doküman derleme, basit wrapper'lar |

Kurallar:
- Patron spec yazmadan Codex lane açılmaz; lane'in kalitesi spec'in kalitesiyle sınırlıdır.
- Her ajan çıktısı `docs/research/` altına yazılır ve patron tarafından SPEC'e işlenir.
- iPhone gerektiren doğrulama → `docs/MANUAL-VERIFY.md` checklist'ine gider; ajan "çalışıyor" diyemez.
- Her değişiklik ayrı commit (`git add <dosya>`; `git add -A` yasak).
