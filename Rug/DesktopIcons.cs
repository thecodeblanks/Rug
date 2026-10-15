using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Rug;

internal static class DesktopIcons
{
    // How high the rug rises over an icon. Raised so an icon away from the rug's edge still reads through shading,
    // where before only the silhouette at the edge gave it away.
    private const float BumpHeight = 30f;

    // The rectangle the desktop list view reports for LVIR_ICON, rebuilt from the item position: spacing.x - 1 wide,
    // icon + 4 high, centred on the icon and 3 px below the position. Measured 2026-10-14 on Windows 11 26200
    // (125 %, medium icons) for all 25 icons; other scales and icon sizes were not measured.
    private const int IconRectWidthInset = 1;
    private const int IconRectExtraHeight = 4;
    private const int IconRectTopOffset = 3;

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int left, top, right, bottom; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? title);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr hWnd);

    public static float[] Scan(IntPtr desktopHost, int screenWidth, int screenHeight)
    {
        IntPtr defView = FindWindowEx(desktopHost, IntPtr.Zero, "SHELLDLL_DefView", null);
        if (defView == IntPtr.Zero) return Array.Empty<float>();
        IntPtr listView = FindWindowEx(defView, IntPtr.Zero, "SysListView32", null);
        if (listView == IntPtr.Zero) return Array.Empty<float>();

        if (!DesktopShell.TryReadIcons(out int[] x, out int[] y, out DesktopShell.IconLayout layout)) return Array.Empty<float>();
        int count = x.Length;
        if (count <= 0) return Array.Empty<float>();

        int icon = (int)Math.Round(layout.IconSize * GetDpiForWindow(listView) / 96.0);
        int w = layout.SpacingX - IconRectWidthInset, h = icon + IconRectExtraHeight;
        if (w <= 0 || h <= 0) return Array.Empty<float>();

        var bumps = new List<float>();
        GetWindowRect(listView, out RECT origin);
        for (int i = 0; i < count; i++)
        {
            int left = x[i] - (w - icon) / 2, top = y[i] + IconRectTopOffset;

            float cx = origin.left + left + w / 2f;
            float cy = origin.top + top + h / 2f;
            if (cx < 0 || cx > screenWidth || cy < 0 || cy > screenHeight) continue;

            float side = Math.Min(w, h);

            float radius = Math.Max(18f, Math.Min(side, 64f) * 0.47f);
            bumps.Add(cx);
            bumps.Add(screenHeight - cy);
            bumps.Add(radius);
            bumps.Add(radius);
            bumps.Add(BumpHeight);
        }
        return bumps.ToArray();
    }
}
