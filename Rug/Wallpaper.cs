using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace Rug;

internal static class Wallpaper
{
    private const uint SPI_SETDESKWALLPAPER = 0x0014;
    private const uint SPIF_UPDATEINIFILE = 0x01;
    private const uint SPIF_SENDCHANGE = 0x02;
    private const string DesktopKey = @"Control Panel\Desktop";

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SystemParametersInfoW(uint action, uint param, string? value, uint winIni);

    private static string WallpaperDir => Path.Combine(AppSettings.DataDir, "wallpapers");

    public static string? Current()
    {
        using var key = Registry.CurrentUser.OpenSubKey(DesktopKey);
        return key?.GetValue("Wallpaper") as string;
    }

    // Explorer applies and records the wallpaper through IDesktopWallpaper, so Rug itself never writes the
    // Control Panel\Desktop values that ransomware also rewrites.
    public static void Apply(AppSettings settings, string floorPath)
    {
        Directory.CreateDirectory(WallpaperDir);
        string copy = Path.Combine(WallpaperDir, Path.GetFileName(floorPath));
        // File.Copy keeps the source's write time, so an equal copy is left alone: rewriting the same files on
        // every click is the burst of modifications that ransomware protection counts.
        var source = new FileInfo(floorPath);
        var existing = new FileInfo(copy);
        if (!existing.Exists || existing.Length != source.Length || existing.LastWriteTimeUtc != source.LastWriteTimeUtc)
            File.Copy(floorPath, copy, true);

        string? current = Current();
        bool alreadyOurs = current != null && current.StartsWith(WallpaperDir, StringComparison.OrdinalIgnoreCase);
        string? prevWallpaper = null, prevStyle = null, prevTile = null;
        if (settings.PrevWallpaper == null && !alreadyOurs)
        {
            using var key = Registry.CurrentUser.OpenSubKey(DesktopKey);
            prevWallpaper = current ?? "";
            prevStyle = key?.GetValue("WallpaperStyle")?.ToString() ?? "0";
            prevTile = key?.GetValue("TileWallpaper")?.ToString() ?? "0";
        }

        // Record the restore point and persist only after the change takes. Otherwise a rejected wallpaper would
        // still leave CanRestore true, offering to restore a wallpaper that was never replaced.
        if (!DesktopShell.SetWallpaper(copy, DesktopShell.WallpaperPosition.Fill))
            throw new InvalidOperationException("Windows did not accept the new wallpaper.");
        if (prevWallpaper != null)
        {
            settings.PrevWallpaper = prevWallpaper;
            settings.PrevWallpaperStyle = prevStyle;
            settings.PrevTileWallpaper = prevTile;
        }
        settings.Save();
    }

    public static bool CanRestore(AppSettings settings) => settings.PrevWallpaper != null;

    public static void Restore(AppSettings settings)
    {
        if (settings.PrevWallpaper == null) return;
        var position = PositionOf(settings.PrevWallpaperStyle, settings.PrevTileWallpaper);
        if (settings.PrevWallpaper.Length > 0)
        {
            DesktopShell.SetWallpaper(settings.PrevWallpaper, position);
        }
        else
        {
            // IDesktopWallpaper documents no way to clear the picture, so "no wallpaper" still goes through the system call.
            DesktopShell.SetWallpaper("", position);
            SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, "", SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        }
        settings.PrevWallpaper = null;
        settings.PrevWallpaperStyle = null;
        settings.PrevTileWallpaper = null;
        settings.Save();
    }

    // Control Panel\Desktop WallpaperStyle and TileWallpaper as Windows writes them for each position.
    private static DesktopShell.WallpaperPosition PositionOf(string? style, string? tile) => (style, tile) switch
    {
        ("0", "1") => DesktopShell.WallpaperPosition.Tile,
        ("0", _) => DesktopShell.WallpaperPosition.Center,
        ("2", _) => DesktopShell.WallpaperPosition.Stretch,
        ("6", _) => DesktopShell.WallpaperPosition.Fit,
        ("10", _) => DesktopShell.WallpaperPosition.Fill,
        ("22", _) => DesktopShell.WallpaperPosition.Span,
        _ => DesktopShell.WallpaperPosition.Keep,
    };
}
