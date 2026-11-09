using System.Runtime.InteropServices;

namespace Rug;

// The desktop as the Shell's documented objects report it (IFolderView, IDesktopWallpaper), so Rug never
// touches Explorer's memory or writes the wallpaper settings itself.
internal static class DesktopShell
{
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int rug_desktop_items(int[] xy, int capacity, int[] layout);

    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int rug_set_wallpaper(string path, int position);

    public readonly record struct IconLayout(int SpacingX, int SpacingY, int IconSize);

    // Item positions are in the desktop list view's client coordinates, the same ones LVM_SETITEMPOSITION takes.
    public static bool TryReadIcons(out int[] x, out int[] y, out IconLayout layout)
    {
        x = [];
        y = [];
        layout = default;
        int[] info = new int[3];
        int capacity = 64;
        // Icons can appear between the count and the read, so a short buffer is retried a few times.
        for (int attempt = 0; attempt < 3; attempt++)
        {
            int[] xy = new int[capacity * 2];
            int count = rug_desktop_items(xy, capacity, info);
            if (count < 0) return false;
            if (count > capacity)
            {
                capacity = count;
                continue;
            }
            x = new int[count];
            y = new int[count];
            for (int i = 0; i < count; i++)
            {
                x[i] = xy[i * 2];
                y[i] = xy[i * 2 + 1];
            }
            layout = new IconLayout(info[0], info[1], info[2]);
            return true;
        }
        return false;
    }

    public static bool SetWallpaper(string path, WallpaperPosition position) => rug_set_wallpaper(path, (int)position) != 0;

    // DESKTOP_WALLPAPER_POSITION, plus Keep for leaving the current one.
    public enum WallpaperPosition
    {
        Keep = -1,
        Center = 0,
        Tile = 1,
        Stretch = 2,
        Fit = 3,
        Fill = 4,
        Span = 5,
    }
}
