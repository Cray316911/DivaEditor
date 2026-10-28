# DivaStudio — Ekran kartı uyumluluğu

Kısa cevap: **DivaStudio her DirectX 11 ekran kartında çalışır.** Kartın donanım
kodlayıcısı/çözücüsü bir codec'i desteklemiyorsa program o iş için otomatik olarak işlemciye
(CPU) geçer; hiçbir kartta "çalışmıyor" durumu yoktur, yalnızca bazı işler daha yavaştır.

| Durum | Anlamı |
|---|---|
| **Test edildi** | Gerçek donanımda ölçüldü (şimdilik yalnızca AMD AMF yolu) |
| **Destekleniyor** | Kod yolu var, üreticinin ve FFmpeg'in desteklediği özellik; o donanımda henüz denenmedi |
| **CPU yedeği** | Kartta o codec için donanım yok; işlemci yapar (export'ta yavaş, kayıtta işlemci yükü) |

## Amiral gemisi kartlar

Decode = önizleme/timeline için video çözme · Encode = kayıt ve dışa aktarma.

| Kart | Decode H.264 / HEVC / AV1 | Encode H.264 / HEVC / AV1 | Not |
|---|---|---|---|
| **NVIDIA RTX 50** (5090, 5080, 5070…) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | NVENC, D3D11'den doğrudan (kopyasız) |
| **NVIDIA RTX 40** (4090, 4080, 4070…) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | AV1 encode RTX 40 ile geldi |
| **NVIDIA RTX 30** (3090, 3080…) | ✅ / ✅ / ✅ | ✅ / ✅ / CPU yedeği | AV1 decode var, AV1 encode yok |
| **NVIDIA RTX 20 / GTX 16** | ✅ / ✅ / CPU yedeği | ✅ / ✅ / CPU yedeği | AV1 videolar CPU'da çözülür |
| **NVIDIA GTX 10** (1080 Ti…) | ✅ / ✅ / CPU yedeği | ✅ / ✅ / CPU yedeği | |
| **AMD RX 9000** (9070 XT, 9070) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | AMF |
| **AMD RX 7000** (7900 XTX…) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | AMF, D3D11'den doğrudan |
| **AMD RX 6000** (6900 XT…) | ✅ / ✅ / ✅ | ✅ / ✅ / CPU yedeği | ⚠ RX 6400 / 6500 XT'de **hiç donanım kodlayıcı yok** → kayıt/export CPU ile |
| **AMD RX 5000** (5700 XT…) | ✅ / ✅ / CPU yedeği | ✅ / ✅ / CPU yedeği | |
| **Intel Arc B / A** (B580, A770…) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | QSV; şimdilik sistem belleği üzerinden (aşağıya bakın) |
| **Intel Core Ultra iGPU** | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | QSV |
| **Intel 11–14. nesil iGPU** | ✅ / ✅ / ✅ | ✅ / ✅ / CPU yedeği | QSV |
| **AMD Ryzen iGPU** (780M, 890M) | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | AMF |

## Programın bunun için yaptıkları (kodda uygulandı)

1. **Doğru GPU seçimi.** Hem dahili hem harici kartı olan dizüstülerde Windows'tan "yüksek
   performanslı" GPU istenir (DXGI GPU tercihi) ve exe, NVIDIA Optimus / AMD PowerXpress'e
   harici kartı kullanmasını söyleyen işaretleri taşır. Gerekirse `DIVA_ADAPTER=1` ortam
   değişkeniyle başka kart seçilir.
2. **Encoder yedek zinciri.** `auto` modunda önce kartın kendi kodlayıcısı (NVENC / AMF / QSV)
   açılır; açılamazsa (eski kart, eski sürücü, codec yok) x264 / x265 / SVT-AV1 ile CPU'ya düşülür.
   Başlangıç ekranı ve Dışa Aktar penceresi o makinede hangisinin çalıştığını gösterir;
   `divabench probe` her kodlayıcının neden açılamadığını da yazar.
3. **Decode yedeği.** Kart bir videoyu (ör. eski kartta AV1, ya da ProRes) çözemiyorsa video CPU'da
   çözülüp GPU'ya aktarılır; önizleme ve export aynı şekilde çalışır (ProRes testinde 405 fps).
4. **Kopyasız yol.** NVENC ve AMF, kompozitörün GPU dokusunu doğrudan alır. QSV ve CPU
   kodlayıcıları sistem belleği istediği için kare bir kez GPU'dan okunur (1080p60'ta ~190 MB/s,
   modern bir PC için küçük bir yük).

## Gereksinimler

| | En az | Önerilen |
|---|---|---|
| Windows | Windows 10 2004 (derleme 19041) | **Windows 11 24H2** — kayıt hız sınırı (yüksek Hz'li ekranda bile yalnız seçilen fps yakalanır), kayıt çerçevesi gizleme |
| GPU | DirectX 11 (feature level 11.0) | Donanım H.264/HEVC kodlayıcısı olan herhangi bir kart |
| Sürücü | — | Güncel sürücü (NVIDIA Game Ready/Studio, AMD Adrenalin, Intel Arc). NVENC'in istediği sürücü sürümü FFmpeg sürümüne bağlıdır; eskiyse program CPU'ya düşer ve nedeni `divabench probe` ile görülür. |

## Bilinen sınırlar ve sıradaki işler

- **Test donanımı:** Ölçümler yalnızca AMD donanımında yapıldı. NVIDIA ve Intel yolları
  FFmpeg'in desteklediği arayüzlerle yazıldı ama gerçek kartta denenmedi; birer NVIDIA (RTX) ve
  Intel (Arc) makinede `divabench probe`, `divabench export` ve `divarec` testi gerekiyor.
- **Intel QSV** şu an sistem belleği üzerinden; QSV yüzeylerini doğrudan kullanan kopyasız yol planlandı.
- **HDR ekran:** Windows HDR açıkken kayıt SDR'ye eşlenmiş görüntüyü alır (renkler soluk
  görünebilir); HDR (10-bit) kayıt planlandı.
- **Kayıt sırasında çözünürlük değişirse** (oyun tam ekran modunu değiştirirse) o kareler atlanır;
  yeniden boyutlanan kayıt planlandı.
- Kayıt CPU kodlayıcıya düşerse (donanım kodlayıcısı olmayan kartlar) oyunda işlemci yükü artar;
  kayıt penceresi bunu "Kalite: Küçük dosya" ile hafifletebilir.
