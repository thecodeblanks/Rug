# Rug for Windows

Sweep your desktop under the rug. A heavy wool rug lies on top of your desktop icons, the icons make bumps in it, and you can grab any edge to peek at the mess underneath.

[Türkçe için aşağı inin](#türkçe)

![Rug demo](docs/demo.gif)

Thanks to [Terkel](https://x.com/terkelg) for the inspiration.

This is a Windows port of [Rug for macOS](https://t.me/prompt_design) by Artem. The original app is the idea and the reference for the physics, lighting and settings. The Windows version is a new implementation, see [Credits](#credits).

## Download

Download `release-windows.zip` from [Releases](https://github.com/thecodeblanks/Rug/releases/latest), unzip it anywhere and run `Rug.exe`. Keep `rug_native.dll` next to it.

## Requirements

- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 11 support
- [.NET 8 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/8.0) if you run `Rug.exe` outside Visual Studio

```cmd
winget install --id Microsoft.DotNet.DesktopRuntime.8 --architecture x64 -e
```

## Build

You need Visual Studio 2022 (or newer) with the **.NET desktop development** and **Desktop development with C++** workloads, plus the [.NET 8 SDK](https://dotnet.microsoft.com/download).

1. Open `Rug.slnx`.
2. Set the configuration to **Release | x64**.
3. Build and run the `Rug` project.

The build puts `Rug.exe` and `rug_native.dll` in `Rug\bin\Release\net8.0-windows\`. Keep the two files together. The rug and floor pictures are inside `Rug.exe`, so the `Resources` folder does not need to be copied anywhere.

`rug_native.dll` links the C++ runtime statically and has its shaders compiled in, so it needs neither the Visual C++ Redistributable nor `d3dcompiler_47.dll`.

### Tests

`tests\native-equivalence\run.cmd <reference rug_native.dll>` builds `rug_native.dll` from this checkout and runs it next to a reference build of the original code on the same scripted input. The reference is the original code with two read-only probes added (`rug_point_count`, `rug_get_points`) so the test can read positions. It compares the cloth's vertex positions, not the rendered pixels, so the drop shadow cannot mask a physics change: without the sleep threshold every vertex must be bit-identical to the reference; with it, a rug resting on icon bumps must freeze within a tenth of a pixel of the range the reference still settles through.

`tests\clickthrough-test` checks that dropping the overlay's click-through style makes the window hit-testable at once, which is what lets the first click on a sleeping rug land. `tests\restore-test` saves a drape, restores it onto a fresh start and checks the exact folds come back and rest on their bumps.

## Use

- Drag the rug by any edge to peek at the mess underneath.
- Scroll over the rug to turn it. Hold **Alt** and scroll to resize it. While **Alt** is held, corner handles appear that you can drag to turn or resize the rug.
- Right-click the tray icon (near the clock) for the quick menu. Double-click it to open the settings.
- Settings has the rugs, the size, the desktop floors and the icon buttons. Closing the window keeps the rug on the desktop.
- **Sweep icons under the rug** piles all desktop icons in a heap under the rug. **Put icons back** restores the old layout, even after you restart Rug. **Cover icons with the rug** hides the bumps under the rug.
- Sweeping needs Explorer to stop arranging the icons by itself. Right-click an empty spot on the desktop, choose **View** and clear **Auto arrange icons**.
- **Rug from your image…** turns any top-down picture into a rug. It looks best when the picture fills the whole frame.
- **Desktop floor** changes the wallpaper. **Restore previous wallpaper** puts the old one back.
- **Start Rug when I sign in to Windows** in the settings adds Rug to the Windows startup programs, which Task Manager lists under **Startup apps**. Rug then starts in the tray without opening the settings. Clearing the box removes the entry.
- To quit, use **Quit Rug** in the tray menu. Starting Rug again while it is running just opens the settings.

Outside the rug every click goes straight to the desktop.

## Rugs and floors

![Rugs](docs/rugs.jpg)

Five rugs: Persian, Neural with circuit-board ornaments, Soviet, Pixel and Turkish. The Persian, Neural, Soviet and Pixel rugs are the same pictures as the macOS version.

## How it works

- A transparent window sits above the desktop icons and below all other windows. It ignores the mouse everywhere except over the rug.
- The icon positions come from the Shell's `IFolderView`, so every icon becomes a bump in the right place. Rug does not open Explorer's process or read its memory.
- The cloth is a physics simulation with friction and self-collisions, so it folds and settles instead of stretching like rubber.
- Once the cloth stops drifting it sleeps: its average pose over two seconds differs by less than 0.005 px from the two seconds before. Nothing is simulated or drawn until something touches it. The simulation never comes to a full stop and keeps trembling by a fraction of a pixel; the sleeping rug does not tremble, and its frozen vertices stay within a tenth of a pixel of where the moving one still settles. A moving rug sends only the part of the screen it changed to the compositor.
- The rug is drawn with Direct3D 11, with soft shadows and the wool relief from the picture.
- **Desktop floor** sets the wallpaper through `IDesktopWallpaper`, so Explorer stores the setting.

## Security software

`Rug.exe` is not signed and few people run it, so reputation checks may stop it the first time it starts. Trend Micro shows this as "Newly Encountered Program Detected". Choose **Allow Once**, or ask your IT team to allow the file.

Security software may also ask before Rug adds itself to the startup programs, because malware uses the same place. Rug writes that entry only when you tick the box, and the box shows whether Windows kept it.

## Known limitations

- Only the main monitor is covered.
- The floor shadow is not drawn yet.
- If Explorer restarts, restart Rug as well.
- Sweeping does not work while **Auto arrange icons** is on.
- Each bump's place is worked out from the icon's position and size. The offsets were measured at 125 % scaling with medium icons; at other settings a bump may sit a pixel or two away from its icon.

## Credits

- Original macOS app **Rug** by Artem, MIT licensed. The idea of sweeping the desktop under a rug is inspired by [Terkel](https://x.com/terkelg).
- Windows port: CodeBlanks ([codeblanks](https://codeblanks.com)).

## License

MIT. See [LICENSE](LICENSE).

---

## Türkçe

Masaüstünüzü halının altına süpürün. Ağır bir yün halı masaüstü simgelerinizin üzerine serilir, simgeler halıda kabarcıklar yapar, halının bir kenarını tutup altındaki karmaşaya bakabilirsiniz.

Bu proje, Artem'in macOS için yaptığı [Rug](https://t.me/prompt_design) uygulamasının Windows sürümüdür. Orijinal uygulama fikrin ve fizik, ışık ile ayarların referansıdır. Windows sürümü baştan yazılmış yeni bir uygulamadır, bkz. [Teşekkür](#teşekkür).

Fikir için [Terkel](https://x.com/terkelg)'e teşekkür ederiz.

### İndir

[Releases](https://github.com/thecodeblanks/Rug/releases/latest) sayfasından `release-windows.zip` dosyasını indir, istediğin yere açıp `Rug.exe` dosyasını çalıştır. `rug_native.dll` dosyası yanında kalmalı.

### Gereksinimler

- Windows 10 veya 11, 64 bit
- Direct3D 11 destekleyen bir ekran kartı
- Visual Studio dışında `Rug.exe` çalıştıracaksanız [.NET 8 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/8.0)

```cmd
winget install --id Microsoft.DotNet.DesktopRuntime.8 --architecture x64 -e
```

### Derleme

Visual Studio 2022 (veya daha yeni), **.NET desktop development** ve **Desktop development with C++** iş yüklerine ihtiyacınız var. Ayrıca [.NET 8 SDK](https://dotnet.microsoft.com/download) gerekir.

1. `Rug.slnx` dosyasını açın.
2. Yapılandırmayı **Release | x64** yapın.
3. `Rug` projesini derleyip çalıştırın.

Derleme `Rug.exe` ile `rug_native.dll` dosyasını `Rug\bin\Release\net8.0-windows\` klasörüne koyar. İkisini yan yana tutun. Halı ve zemin resimleri `Rug.exe` içine gömülüdür, ayrıca bir klasör kopyalamanız gerekmez.

`rug_native.dll` C++ runtime'ı kendi içinde taşıyor ve shader'ları derlenmiş halde içeriyor. Bu yüzden Visual C++ Redistributable ya da `d3dcompiler_47.dll` gerekmiyor.

#### Testler

`tests\native-equivalence\run.cmd <referans rug_native.dll>` komutu bu kodun `rug_native.dll` dosyasını derler. Sonra onu orijinal kodun bir derlemesiyle aynı girdilerle yan yana çalıştırır. Referans, orijinal koddur; konumları okumak için iki salt-okunur prob (`rug_point_count`, `rug_get_points`) eklenmiştir. Test, pikselleri değil kumaşın vertex konumlarını karşılaştırır. Böylece gölge, fizik değişikliğini gizleyemez. Uyku eşiği kapalıyken her vertex referansla bit bit aynı olmalı. Eşik açıkken ikon tümseklerinde duran halı, referansın oturduğu aralığın onda bir piksel içinde donmalı.

`tests\clickthrough-test`, overlay'in click-through stilini kaldırınca pencerenin hemen hit-test edilebilir olduğunu kontrol eder. Bu, uyuyan halıya ilk tıklamanın işlemesini sağlar. `tests\restore-test`, bir drape kaydeder, taze başlangıçta geri yükler ve kırışıkların aynen dönüp tümseklerinde durduğunu kontrol eder.

### Kullanım

- Halıyı herhangi bir kenarından sürükleyip altındaki dağınıklığa bakın.
- Halının üzerinde fareyi kaydırarak döndürün. **Alt** basılıyken kaydırarak boyutunu değiştirin. **Alt** basılıyken beliren köşe tutamaçlarını sürükleyerek de döndürüp boyutlandırabilirsiniz.
- Saatin yanındaki tepsi simgesine sağ tıklayarak hızlı menüyü açın, çift tıklayarak ayarları açın.
- Ayarlarda halılar, boyut, masaüstü zemini ve simge düğmeleri bulunur. Pencereyi kapatmak halıyı masaüstünde bırakır.
- **Sweep icons under the rug** (simgeleri halının altına süpür) tüm masaüstü simgelerini halının altında bir yığın yapar. **Put icons back** eski düzeni geri getirir, Rug'ı yeniden başlatsanız bile. **Cover icons with the rug** kabarcıkları halının altında gizler.
- Süpürme için Explorer'ın simgeleri kendi kendine dizmesi kapalı olmalı. Masaüstünde boş bir yere sağ tıklayın, **Görünüm**'ü seçin ve **Simgeleri otomatik hizala** seçeneğini kaldırın.
- **Rug from your image…** herhangi bir üstten çekilmiş resmi halıya çevirir. Resim tüm kareyi kapladığında en iyi görünür.
- **Desktop floor** duvar kağıdını değiştirir. **Restore previous wallpaper** eskisini geri koyar.
- Ayarlardaki **Start Rug when I sign in to Windows** kutusu Rug'ı Windows'un başlangıç programlarına ekler. Görev Yöneticisi onu **Başlangıç uygulamaları** altında gösterir. Rug açılışta ayar penceresini açmadan tepside başlar. Kutuyu kaldırırsanız kayıt silinir.
- Çıkmak için tepsi menüsünden **Quit Rug**'ı seçin. Rug açıkken tekrar başlatmak sadece ayarları açar.

Halının dışındaki tıklamalar doğrudan masaüstüne gider.

### Halılar ve zeminler

![Halılar](docs/rugs.jpg)

Beş halı: Persian, Neural (devre kartı süslemeli), Soviet, Pixel ve Turkish. Persian, Neural, Soviet ve Pixel halıları macOS sürümüyle aynı resimlerdir.

### Nasıl çalışır

- Şeffaf bir pencere masaüstü simgelerinin üstünde, diğer pencerelerin altında durur. Halının dışındaki her fare tıklamasını görmezden gelir.
- Simgelerin konumları Shell'in `IFolderView` arayüzünden okunur, böylece her simge halıda doğru yere kabarcık olur. Rug, Explorer'ın process'ini açmıyor ve belleğini okumuyor.
- Halı, sürtünme ve kendi kendine çarpışmalarla çalışan bir fizik simülasyonudur. Gerilmek yerine kıvrılır ve oturur.
- Halının iki saniyelik ortalama konumu, önceki iki saniyeninkinden 0,005 pikselden az farklıysa simülasyon uyuyor. Halıya bir şey dokunana kadar ne hesap yapılıyor ne de çizim. Simülasyon hiç tam durmuyor ve pikselin çok küçük bir kısmı kadar titremeye devam ediyor. Uyuyan halı titremiyor, resmi de titreyen halınınkinden en fazla 2 seviye (255 üzerinden) farklı. Hareket eden halı, ekranın yalnızca değişen kısmını bileşiciye gönderiyor.
- Halı Direct3D 11 ile çizilir, yumuşak gölgeler ve resimdeki yün dokusu ile birlikte.
- **Desktop floor** duvar kağıdını `IDesktopWallpaper` ile ayarlıyor; ayarı Explorer kaydediyor.

### Güvenlik yazılımları

`Rug.exe` imzalı değil ve az kişi çalıştırıyor. Bu yüzden dosya itibarına bakan kontroller onu ilk açılışta durdurabiliyor. Trend Micro bunu "Newly Encountered Program Detected" uyarısıyla gösteriyor. **Allow Once**'ı seçin ya da IT ekibinizden dosyaya izin vermesini isteyin.

Güvenlik yazılımı, Rug kendini başlangıç programlarına eklerken de onay isteyebilir. Zararlı yazılımlar da aynı yeri kullanıyor. Rug bu kaydı yalnızca kutuyu işaretlediğinizde yazıyor. Kutu, Windows'un kaydı tutup tutmadığını gösteriyor.

### Bilinen sınırlamalar

- Yalnızca ana monitör kapsanır.
- Zemin gölgesi henüz çizilmiyor.
- Explorer yeniden başlarsa Rug'ı da yeniden başlatın.
- **Simgeleri otomatik hizala** açıkken süpürme çalışmaz.
- Her kabarcığın yeri, simgenin konumundan ve boyutundan hesaplanıyor. Kaydırma değerleri %125 ölçekte, orta boy simgelerle ölçüldü. Başka ayarlarda bir kabarcık simgesinden bir iki piksel kayabilir.

### Lisans

MIT. Bkz. [LICENSE](LICENSE).

### Teşekkür

- Orijinal macOS uygulaması **Rug**, Artem tarafından yazıldı, MIT lisanslı. Masaüstünü halının altına süpürme fikri Terkel'in ilhamıdır.
- Windows sürümü: CodeBlanks
