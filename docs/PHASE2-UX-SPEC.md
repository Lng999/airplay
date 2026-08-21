# PHASE 2 UX SPEC — sade, Türkçe arayüz (M1.5)

Kaynak: kullanıcı geri bildirimi (2026-08-21, ekran görüntüsü). M1 arayüzü **çalışıyor ama
geliştirici paneli gibi görünüyor**: `d3d11videosink`, `H.265`, `Copy cmdline`, ham log ve
dosya yolları ilk ekranda. Bu spec M1'in davranışını değiştirmeden **sunumunu** sadeleştirir.

Kapsam dışı: video embed, FPS/bitrate (M2), paketleme (Phase 3). Host katmanı
(`app/src/host/*`) ve `uxplay_host.h` sözleşmesi **değişmez** — bu tamamen `app/src/ui/` işi.

## Kararlar

- **[KARAR] Arayüz dili Türkçe.** Kullanıcıya görünen her metin Türkçe. Kod, yorum, commit
  mesajı, log dosyası ve `config.ini` anahtarları **İngilizce kalır**. Tüm görünür metinler
  tek yerde: `app/src/ui/strings.h` (derleme zamanı sabitleri, i18n altyapısı yok — tek dil).
- **[KARAR] Teknik ayarlar katlanır "Gelişmiş" bölümünde.** Ayrı dialog yok. Kapalıyken
  pencere küçük; açılınca pencere kendiliğinden büyür, kapanınca küçülür.
- **[KARAR] Ham log varsayılan gizli**, "Ayrıntılar" ile açılır (aynı katlanma mekaniği).
- **[KARAR] Tek toggle düğmesi**: Başlat ⇄ Durdur. İki düğmeden biri sürekli gri durmaz.
- **[KARAR] Durum satırı ne yapılacağını söyler**, sadece durumu değil.

## Ekran

Kapalı (varsayılan, ~420×300 @96dpi):

```
airplay
────────────────────────────────────────
  ●  Kapalı
     Alıcıyı başlatmak için Başlat'a basın

        [        Başlat        ]

  Bu bilgisayarın adı:  [ AirPlay-PC     ]

  ▸ Gelişmiş        ▸ Ayrıntılar
```

Hazır:  `● Hazır` / `iPhone'unda Denetim Merkezi → Ekran Yansıtma → AirPlay-PC (192.168.1.107)`
Bağlı:  `● Bağlandı` / `Mustafa'nın iPhone'u · 1920×1080` (çözünürlük yalnız Ayrıntılı günlük açıkken)
Hata:   `● Hata` / `<mesaj>`

## Durum noktası renkleri

| Durum | Renk | Metin | Alt satır |
|---|---|---|---|
| Stopped | gri | Kapalı | Alıcıyı başlatmak için Başlat'a basın |
| Starting | turuncu | Başlatılıyor… | — |
| Waiting | mavi | Hazır | iPhone'unda Denetim Merkezi → Ekran Yansıtma → `<ad>` (`<ip>`) |
| Connected | yeşil | Bağlandı | `<cihaz adı>` [· `<en×boy>`] |
| Stopping | turuncu | Durduruluyor… | — |
| Error | kırmızı | Hata | `<mesaj>` |

Nokta owner-draw değil: sahibi olan pencere `WM_PAINT` içinde `Ellipse` ile çizer.

## Gelişmiş bölümü (katlanır, varsayılan kapalı)

Port · Görüntü çıkışı · Ses çıkışı · Tam ekran · H.265 · Ayrıntılı günlük ·
Her zaman üstte · Açılışta alıcıyı başlat · **Komutu kopyala** düğmesi.

Etiketler Türkçe, değerler teknik kalır (`d3d11videosink` bir GStreamer eleman adıdır,
çevrilirse `config.ini` ile ilişkisi kopar).

M1 kısıtı korunur: alıcı çalışırken bu alanlar gri (UxPlay argv'yi yalnız açılışta okur).

## Ayrıntılar bölümü (katlanır, varsayılan kapalı)

Mevcut log listbox'ı + log dosyası yolu. Açılış mesajları (`config:`, `log:`, `uxplay.exe:`)
artık ilk ekranda görünmez ama dosyaya ve listeye yazılmaya devam eder.

## Bildirim alanı davranışı

Kapat düğmesi bugün olduğu gibi pencereyi gizler. **Yeni:** ilk gizlemede bir balon bildirim
— "airplay bildirim alanında çalışmaya devam ediyor. Çıkmak için sağ tık → Çıkış."
`[app] tray_hint_shown=1` ile bir kez gösterilir. Tray menüsü de Türkçeleşir
(Göster / Başlat / Durdur / Çıkış).

## Kabul kriterleri

- `airplay-gui.exe` uyarısız derlenir; Türkçe karakterler (ı, ş, ğ, ç, ö, ü) doğru görünür.
- Kapalı haldeki pencerede **hiçbir GStreamer eleman adı, port numarası veya dosya yolu görünmez.**
- Gelişmiş ve Ayrıntılar açılıp kapanınca pencere yüksekliği buna göre değişir; kullanıcı
  pencereyi elle boyutlandırdıysa genişliği korunur.
- Start → durum "Hazır" olur ve alt satır cihaz adı + IP gösterir; Stop → "Kapalı".
- `config.ini`'deki anahtarlar ve host'a giden argv **değişmez** (regresyon: `airplay_host_tests`).
- Katlanma durumları `config.ini` `[app] show_advanced` / `show_details` altında saklanır.

## Manuel doğrulama (kullanıcı)

`docs/MANUAL-VERIFY.md` Phase 2 listesine eklenir: sade ekranda Başlat → iPhone'dan bağlan →
"Bağlandı" + cihaz adı görünüyor mu; Gelişmiş/Ayrıntılar katlanması pencereyi bozuyor mu.
