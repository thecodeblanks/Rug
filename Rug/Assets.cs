using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;

namespace Rug;

internal static class Assets
{
    private static readonly string[] ImageExtensions = { ".jpg", ".jpeg", ".png", ".bmp" };

    public static string UserRugDir => Path.Combine(AppSettings.DataDir, "rugs");
    public static string UserFloorDir => Path.Combine(AppSettings.DataDir, "floors");

    private static string CacheDir => Path.Combine(AppSettings.DataDir, "assets");
    private static string ThumbnailDir => Path.Combine(AppSettings.DataDir, "thumbnails");

    public static List<string> BuiltinRugs() => Bundled("rugs");

    public static List<string> UserRugs() => ListImages(UserRugDir);

    public static List<string> AllRugs() => BuiltinRugs().Concat(UserRugs()).ToList();

    public static List<string> Floors() => Bundled("floors").Concat(ListImages(UserFloorDir)).ToList();

    // The pictures are embedded in Rug.exe. The renderer and the wallpaper need real files,
    // so they are copied to a cache folder first and the paths of those copies are returned.
    private static List<string> Bundled(string folder)
    {
        var assembly = Assembly.GetExecutingAssembly();
        string prefix = folder + "/";
        string dir = Path.Combine(CacheDir, folder);
        foreach (string name in assembly.GetManifestResourceNames())
        {
            if (!name.StartsWith(prefix, StringComparison.Ordinal)) continue;
            string target = Path.Combine(dir, name.Substring(prefix.Length));
            using Stream? src = assembly.GetManifestResourceStream(name);
            if (src == null) continue;
            if (File.Exists(target) && new FileInfo(target).Length == src.Length) continue;
            Directory.CreateDirectory(dir);
            using var dst = File.Create(target);
            src.CopyTo(dst);
        }
        return ListImages(dir);
    }

    // The app icon is embedded in Rug.exe too. size 0 keeps every size the icon file has.
    public static Icon AppIcon(int size)
    {
        using Stream? src = Assembly.GetExecutingAssembly().GetManifestResourceStream("codeblanks.ico");
        if (src == null) return SystemIcons.Application;
        return size > 0 ? new Icon(src, new Size(size, size)) : new Icon(src);
    }

    private static List<string> ListImages(string dir)
    {
        if (!Directory.Exists(dir)) return new List<string>();
        return Directory.GetFiles(dir)
            .Where(f => ImageExtensions.Contains(Path.GetExtension(f).ToLowerInvariant()))
            .OrderBy(f => Path.GetFileName(f), StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public static string DisplayName(string path)
    {
        string n = Path.GetFileNameWithoutExtension(path);
        int space = n.IndexOf(' ');
        if (space > 0 && n.Substring(0, space).All(char.IsDigit)) n = n.Substring(space + 1);
        return n switch
        {
            "persian" => "Persian",
            "neuro" => "Neural",
            "soviet" => "Soviet",
            "pixel" => "Pixel",
            "turkish" => "Turkish",
            _ => n,
        };
    }

    // Decoding a 5000 px picture takes about 120 ms (measured 2026-10-14), so each thumbnail is kept as a
    // lossless PNG; a cached one has the same pixels as a fresh one. The name changes when the picture changes.
    // fresh is true when the picture was decoded now and the result belongs in the cache.
    public static Bitmap Thumbnail(string path, out bool fresh, int width = 100, int height = 68)
    {
        fresh = false;
        string? cached = ThumbnailCachePath(path, width, height);
        if (cached != null && File.Exists(cached))
        {
            try
            {
                using var stored = new Bitmap(cached);
                return new Bitmap(stored);
            }
            catch (Exception)
            {
                // A damaged cache file is decoded again and overwritten.
            }
        }

        var bmp = new Bitmap(width, height);
        try
        {
            using var src = new Bitmap(path);
            using var g = Graphics.FromImage(bmp);
            g.InterpolationMode = InterpolationMode.HighQualityBilinear;
            g.Clear(Color.FromArgb(40, 40, 40));
            float s = Math.Max(width / (float)src.Width, height / (float)src.Height);
            float w = src.Width * s, h = src.Height * s;
            g.DrawImage(src, (width - w) / 2, (height - h) / 2, w, h);
        }
        catch (Exception)
        {
            return bmp;
        }
        fresh = cached != null;
        return bmp;
    }

    // Written straight under its final name. On 2026-10-14 Trend Micro's ransomware protection stopped Rug
    // when it wrote a randomly named .tmp file and renamed it over the .png, the way ransomware replaces a file.
    public static void StoreThumbnail(string path, Bitmap bmp, int width = 100, int height = 68)
    {
        string? target = ThumbnailCachePath(path, width, height);
        if (target == null) return;
        try
        {
            Directory.CreateDirectory(ThumbnailDir);
            bmp.Save(target, ImageFormat.Png);
        }
        catch (Exception)
        {
            // Without the cache the next start decodes the picture again.
        }
    }

    private static string? ThumbnailCachePath(string path, int width, int height)
    {
        try
        {
            var info = new FileInfo(path);
            if (!info.Exists) return null;
            string key = $"{info.FullName}|{info.Length}|{info.LastWriteTimeUtc.Ticks}|{width}x{height}";
            string name = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(key)), 0, 16) + ".png";
            return Path.Combine(ThumbnailDir, name);
        }
        catch (Exception)
        {
            return null;
        }
    }

}
