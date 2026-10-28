# DivaStudio mimarisi

## Seçim: C++20 + Qt 6 (Qt Quick) + FFmpeg + doğrudan platform API'leri

Belirleyici soru: **4K kare nerede yaşıyor?** 4K RGBA bir kare ~33 MB, 60 fps'te ~2 GB/s.
Akıcılığın tek yolu karenin decode → compositing → ekran / encoder boyunca VRAM'den çıkmaması.

- **Electron / Tauri:** UI bir web görünümünde; kareyi motor ile UI arasında taşımak 4K60'ta
  pratik değil. Electron'un `desktopCapturer`'ı imleci ayrı yakalayamaz.
- **Qt 6:** Qt Quick'in D3D11 arka ucu uygulamanın kendi D3D11 cihazını kullanır
  (`QQuickGraphicsDevice::fromDeviceAndContext`). Decoder, compositor, encoder ve UI aynı cihazı
  paylaşır, kare hiç kopyalanmaz. macOS'ta aynı yapı Metal ile çalışır.

## Katmanlar

```
DivaStudio.exe (Qt Quick UI)          src/app
 ├─ başlangıç ekranı · editör (kategori şeridi, varlık paneli, canvas, özellikler, timeline)
 ├─ kayıt penceresi · bölge seçici · geri sayım · kayıt çubuğu (kayda girmez) · export
 ├─ ProjectController (proje, undo, önizleme/export render) · RecorderController (F9/F10)
 └─ AgentController: DivaAI, Claude API döngüsü (kullanıcının API anahtarı)
divaedit                              src/edit
 ├─ Project: klipler, geçişler, ripple silme, undo, .divaproj, düzenleme tarifleri
 ├─ TimelineRenderer: geçişli video + karışık ses, GPU'da (TransitionRenderer, 9 geçiş)
 ├─ AudioReader (tüm ses izlerini 48 kHz stereo karıştırır), FrameSource
 └─ AgentTools: DivaAI araçları (tara, kare incele, ses seviyesi, kes, geçiş, referans proje, export)
divacore (UI'dan bağımsız)            src/core
 ├─ gpu/         paylaşılan D3D11 cihazı (yüksek performanslı GPU seçimi), SurfacePool
 ├─ media/       HwDecoder (D3D11VA + CPU yedeği), HwEncoder (AMF/NVENC/QSV + CPU yedeği), Muxer
 └─ compositor/  Nv12Renderer (YUV→RGB + ölçek), RgbToNv12 (video processor, bölge kırpma)
divacapture                           src/capture
 ├─ ScreenRecorder: WGC → video processor → donanım encoder, duraklatma, bölge
 └─ AudioCapture: WASAPI sistem sesi (loopback) ve mikrofon, ayrı izler
araçlar                               tools/
 └─ divabench, divarec, divamcp (Claude Code için MCP sunucusu), gpustress
```

## DivaAI

- Model videoyu **kontak sayfalarıyla** görür (4×4 küçük kare + zaman etiketi; uzun videolar
  anahtar karelerle hızlı taranır, sınırlar kare kare incelenir). Kararları model verir, düzenlemeyi
  `AgentTools` uygular; her düzenleme projeye bir geri al adımı olarak düşer.
- **Uygulamada:** Claude API, `claude-opus-5-5`, adaptive thinking (özetler panelde görünür),
  `fallbacks: "default"`, istek önbelleği. Anahtar Windows Kimlik Bilgileri'nde.
- **Claude Code ile:** `divamcp` aynı araçları MCP (stdio) üzerinden sunar; projeyi diske yazar,
  editör dosya değişikliğini izleyip yeniden yükler. Kullanıcının kendi Claude Code oturumu
  kullanıldığı için API anahtarı gerekmez.
- **Düzenleme kopyalama:** `Project::editRecipe()` bir projenin tutulan/kesilen aralıklarını ve
  geçişlerini çıkarır; birebir ya da orantılı uygulanır, ya da DivaAI kesilen sahnelerin
  görüntülerine bakıp yeni videoda benzer anları bulur.

## Veri yolları

- **Önizleme:** dosya → FFmpeg demux → D3D11VA decode (NV12 doku, shader okunabilir) →
  `Nv12Renderer` → Qt Quick sahnesindeki doku. Decode thread'i 8 kare önde gider; render thread'i
  vsync'te sırası gelen kareyi çizer. Saat ilk kare ekrana geldiğinde başlar.
- **Export:** aynı decode → `Nv12Renderer` ile çıktı boyutunda BGRA tuval → `RgbToNv12`
  (D3D11 video processor) → encoder havuzundan NV12 yüzey → AMF/NVENC/QSV → MP4/MKV/MOV.
  Kare hızı dönüşümü (30→60→120) zaman tabanlı kare seçimiyle yapılır.
- **Kayıt:** WGC monitör yakalama, `MinUpdateInterval` ile hedef fps'e sınırlı (yüksek Hz'li ekran
  60 fps'ten fazla iş üretmez) → yakalanan yüzey doğrudan video processor'a → NV12 encoder yüzeyi.
  Ayrı bir thread yüksek çözünürlüklü zamanlayıcıyla sabit kare hızında (CFR) yazar; ekran
  değişmediyse son yüzeyi tekrar gönderir. Çıktı MKV (kayıt yarıda kesilse de oynatılabilir).

## Oyunlarda FPS'i korumak için alınan önlemler

1. Oyuna kod enjekte edilmez (WGC), anti-cheat ile çakışmaz.
2. Yakalama hızı Windows tarafından hedef fps'e sınırlanır.
3. Kare CPU'ya hiç inmez; renk dönüşümü video processor'da, sıkıştırma GPU'nun ayrı video
   bloğunda (VCN/NVENC/QSV) yapılır, oyunun kullandığı shader çekirdeklerinde değil.
4. Kaydedicinin GPU önceliği oyunun altında (`gpuPriority = -7`, ayarlanabilir).
5. Ölçüm: GPU'yu %100 kullanan sahnede ortalama FPS'te ~%1–2; 1% low gürültü içinde;
   kaydedici CPU'nun %0,5'ini kullanır. FPS limitli/GPU'yu doyurmayan oyunlarda etki ~0.

## Önemli kararlar

- Zaman birimi (timeline'da): tamsayı "flicks" (1/705.600.000 sn) planlanıyor.
- Renk: etiketsiz HD video BT.709 kabul edilir (mpv/tarayıcılarla aynı); kendi çıktılarımız
  daima etiketli yazılır (BT.709, limited range).
- Encoder yüzeyleri tek tek doku olarak ayrılır; sürücüler NV12 render-target dizilerini reddediyor.
- AV1 için FFmpeg'in varsayılan yazılım decoder'ı (libdav1d) atlanıp D3D11VA destekli olan seçilir.

## Faz 1 yol haritası

1. ~~Teknik prototip: paylaşılan D3D11, 4K60 oynatma, sıfır kopya export~~ ✅
2. ~~Kayıt motoru: WGC, 60/120 fps CFR, bölge, duraklatma, F9/F10~~ ✅ (imleç olay kaydı eksik)
3. ~~Ses kaydı: WASAPI mikrofon + sistem sesi, QPC ile senkron, AAC~~ ✅
4. ~~Timeline çekirdeği: model, undo/redo, böl / ripple sil, geçişler, `.divaproj`~~ ✅
   (snapping, çoklu parça ve otomatik kayıt eksik)
5. ~~DivaAI: araçlar, uygulama içi agent, Claude Code (MCP), düzenleme kopyalama~~ ✅
6. Editör etkileşimi: canvas'ta taşı/boyutla/döndür, gerçek zamanlı timeline önizlemesi
7. Görsel katman: callout/metin/şekil, keyframe animasyon, Odak Zoom, imleç efektleri
8. Ses düzenleme: seviye eğrileri, fade, RNNoise gürültü azaltma, EBU R128
9. Render kuyruğu arayüzü, proxy dosyaları, QSV kopyasız yol, HDR kayıt
