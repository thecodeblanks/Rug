using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace Rug;

internal static class IconPile
{
    private const uint LVM_GETITEMCOUNT = 0x1004;
    private const uint LVM_SETITEMPOSITION = 0x100F;
    private const int GWL_STYLE = -16;
    private const long LVS_AUTOARRANGE = 0x0100;
    private const uint SMTO_ABORTIFHUNG = 0x0002;

    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }
    [DllImport("user32.dll")] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? title);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int index);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);

    public sealed class HomeData
    {
        public int Count { get; set; }
        public int[] X { get; set; } = Array.Empty<int>();
        public int[] Y { get; set; } = Array.Empty<int>();
    }

    private static string HomeFile => Path.Combine(AppSettings.DataDir, "icon-home.json");

    public static bool HasHome => File.Exists(HomeFile);

    public static string? Pile(IntPtr desktopHost, int screenWidth, int screenHeight, float[] rugBounds)
    {
        IntPtr listView = FindListView(desktopHost);
        if (listView == IntPtr.Zero) return "Could not find the desktop icons.";
        if ((GetWindowLongPtr(listView, GWL_STYLE).ToInt64() & LVS_AUTOARRANGE) != 0)
            return "Desktop auto arrange is on, so the icons snap back. Right-click the desktop, View, and turn off \"Auto arrange icons\".";

        if (ItemCount(listView) <= 0) return "There are no desktop icons to move.";
        if (!DesktopShell.TryReadIcons(out int[] x, out int[] y, out _)) return "Could not read the icon positions.";
        int count = x.Length;
        if (count <= 0) return "There are no desktop icons to move.";

        if (!HasHome) SaveHome(count, x, y);

        GetWindowRect(listView, out RECT origin);
        float cx = (rugBounds[0] + rugBounds[2]) / 2f;
        float cy = (rugBounds[1] + rugBounds[3]) / 2f;
        float hw = (rugBounds[2] - rugBounds[0]) * 0.3f;
        float hh = (rugBounds[3] - rugBounds[1]) * 0.3f;
        int left = (int)(cx - hw) - origin.left;
        int right = (int)(cx + hw) - origin.left;
        int top = (screenHeight - (int)(cy + hh)) - origin.top;
        int bottom = (screenHeight - (int)(cy - hh)) - origin.top;

        var rng = new Random();
        int moved = 0;
        for (int i = 0; i < count; i++)
            if (SetPosition(listView, i, rng.Next(left, Math.Max(left + 1, right)), rng.Next(top, Math.Max(top + 1, bottom)))) moved++;
        return moved == 0 ? "Explorer did not accept the new icon positions." : null;
    }

    public static string? Restore(IntPtr desktopHost)
    {
        if (!HasHome) return "There is no saved layout to put back.";
        HomeData? home;
        try { home = JsonSerializer.Deserialize<HomeData>(File.ReadAllText(HomeFile)); }
        catch (Exception) { home = null; }
        // The file is ours, but a damaged or edited one must not crash the restore.
        if (home?.X == null || home.Y == null || home.Count < 0 || home.X.Length < home.Count || home.Y.Length < home.Count)
            return "The saved icon layout could not be read.";

        IntPtr listView = FindListView(desktopHost);
        if (listView == IntPtr.Zero) return "Could not find the desktop icons.";
        int count = ItemCount(listView);
        if (count != home.Count)
            return "The number of desktop icons has changed, so the old layout cannot be restored.";

        int restored = 0;
        for (int i = 0; i < count; i++)
            if (SetPosition(listView, i, home.X[i], home.Y[i])) restored++;
        if (restored == 0) return "Explorer did not accept the icon positions. The saved layout was kept.";
        File.Delete(HomeFile);
        return null;
    }

    private static void SaveHome(int count, int[] x, int[] y)
    {
        Directory.CreateDirectory(AppSettings.DataDir);
        File.WriteAllText(HomeFile, JsonSerializer.Serialize(new HomeData { Count = count, X = x, Y = y }));
    }

    private static IntPtr FindListView(IntPtr desktopHost)
    {
        IntPtr defView = FindWindowEx(desktopHost, IntPtr.Zero, "SHELLDLL_DefView", null);
        if (defView == IntPtr.Zero) return IntPtr.Zero;
        return FindWindowEx(defView, IntPtr.Zero, "SysListView32", null);
    }

    private static int ItemCount(IntPtr listView)
    {
        SendMessageTimeout(listView, LVM_GETITEMCOUNT, IntPtr.Zero, IntPtr.Zero, SMTO_ABORTIFHUNG, 200, out IntPtr count);
        return count.ToInt32();
    }

    private static bool SetPosition(IntPtr listView, int index, int x, int y)
    {
        int packed = ((y & 0xFFFF) << 16) | (x & 0xFFFF);
        SendMessageTimeout(listView, LVM_SETITEMPOSITION, new IntPtr(index), new IntPtr(packed), SMTO_ABORTIFHUNG, 200, out IntPtr ok);
        return ok != IntPtr.Zero;
    }
}
