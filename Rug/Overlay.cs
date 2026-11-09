using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Rug;

internal static class Overlay
{
    private const int SM_CXSCREEN = 0;
    private const int SM_CYSCREEN = 1;
    private const uint WM_SPAWN_WORKER = 0x052C;

    private const uint WS_POPUP = 0x80000000;
    private const long WS_EX_TOOLWINDOW = 0x00000080;
    private const long WS_EX_TRANSPARENT = 0x00000020;
    private const long WS_EX_LAYERED = 0x00080000;
    private const long WS_EX_NOACTIVATE = 0x08000000;
    private const int GWL_EXSTYLE = -20;

    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOMOVE = 0x0002;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const int SW_SHOWNOACTIVATE = 4;
    private const uint ULW_ALPHA = 0x00000002;
    private const uint GW_HWNDNEXT = 2;
    private const uint GW_HWNDPREV = 3;
    private const uint PM_REMOVE = 0x0001;
    private const byte AC_SRC_OVER = 0x00;
    private const byte AC_SRC_ALPHA = 0x01;
    private const int VK_LBUTTON = 0x01;
    private const int VK_LMENU = 0xA4;
    private const int VK_RMENU = 0xA5;

    private const uint WM_MOUSEWHEEL = 0x020A;
    private const uint WM_DISPLAYCHANGE = 0x007E;
    private const double IconRefreshMs = 2000;
    private const float DefaultDrop = 90f;
    private const float MenuDrop = 110f;
    private const float CenterDrop = 60f;
    private const int FrameMs = 16;

    // Display settings for EnumDisplaySettings and the high-resolution waitable timer the awake loop waits on.
    private const int ENUM_CURRENT_SETTINGS = -1;
    private const uint CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x2;
    private const uint TIMER_ALL_ACCESS = 0x1F0003;
    private static volatile int _refreshHz = 60;

    private static readonly ConcurrentQueue<Action> _commands = new();
    private static readonly ManualResetEventSlim _ready = new(false);
    private static volatile bool _running = true;
    private static volatile float _currentScale = 0.46f;
    private static string? _startError;
    private static int _width, _height;
    private static IntPtr _host;
    private static float _rugScale = 0.46f;
    private static float[] _lastBumps = Array.Empty<float>();
    private static float[] _sentBumps = Array.Empty<float>();

    public static Action<string>? Notifier;

    public static float CurrentScale => _currentScale;

    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_init(int width, int height);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] private static extern int rug_load_texture(string path);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_rebuild(float scale, float drop);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_set_bumps(float[] data, int count);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_step(float dt);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_render(IntPtr dst, int stride, int[] rect);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_invalidate();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_mouse_down(float x, float y);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_mouse_drag(float x, float y);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_mouse_up();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_hit_test(float x, float y);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_rotate(float angle);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_resize(float scale);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_get_pose(float[] pose);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_get_corners(float[] corners);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_hold_begin();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_hold_end();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_get_bounds(float[] bounds);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_layout(float cx, float cy, float angle, float drop);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_point_count();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_get_points(float[] points, int maxVertices);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_set_points(float[] points, int count);
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern int rug_is_settled();
    [DllImport("rug_native.dll", CallingConvention = CallingConvention.Cdecl)] private static extern void rug_shutdown();

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    private delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? className, string? windowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr childAfter, string? className, string? windowName);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern ushort RegisterClassW(ref WNDCLASS wc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowExW(uint exStyle, string className, string windowName, uint style,
                                                 int x, int y, int width, int height,
                                                 IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll", EntryPoint = "LoadCursorW")] private static extern IntPtr LoadCursor(IntPtr instance, IntPtr cursorName);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int cmdShow);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr DefWindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int index);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int index, IntPtr value);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT pt);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT pt);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hWnd, IntPtr hdc);
    [DllImport("user32.dll")] private static extern bool UpdateLayeredWindowIndirect(IntPtr hwnd, ref UPDATELAYEREDWINDOWINFO info);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr hWnd, uint cmd);
    [DllImport("user32.dll")] private static extern bool PeekMessage(out MSG msg, IntPtr hWnd, uint filterMin, uint filterMax, uint remove);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr hdc);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFO bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? moduleName);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr CreateWaitableTimerExW(IntPtr attributes, string? name, uint flags, uint access);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool SetWaitableTimer(IntPtr timer, ref long dueTime, int period, IntPtr callback, IntPtr arg, bool resume);
    [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool EnumDisplaySettingsW(string? deviceName, int mode, ref DEVMODE devMode);

    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct SIZE { public int cx, cy; }
    [StructLayout(LayoutKind.Sequential)] private struct MSG
    {
        public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam;
        public uint time; public POINT pt;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BLENDFUNCTION
    {
        public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat;
    }
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct UPDATELAYEREDWINDOWINFO
    {
        public int cbSize;
        public IntPtr hdcDst, pptDst, psize, hdcSrc, pptSrc;
        public uint crKey;
        public IntPtr pblend;
        public uint dwFlags;
        public IntPtr prcDirty;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAPINFOHEADER
    {
        public int biSize, biWidth, biHeight;
        public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAPINFO { public BITMAPINFOHEADER bmiHeader; }
    // The full DEVMODEW, so EnumDisplaySettings never writes past the struct; only dmDisplayFrequency is read.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public ushort dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public uint dmFields;
        public int dmPositionX, dmPositionY;
        public uint dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public ushort dmLogPixels;
        public uint dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public uint dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WNDCLASS
    {
        public uint style;
        public IntPtr lpfnWndProc;
        public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground;
        public string? lpszMenuName;
        public string lpszClassName;
    }

    private static readonly WndProc _wndProc = OnWndProc;

    public static string? Start(AppSettings settings)
    {
        _rugScale = settings.Scale;
        _currentScale = settings.Scale;
        var thread = new Thread(() =>
        {
            try
            {
                Loop(settings);
            }
            catch (Exception e)
            {

                if (!_ready.IsSet)
                    _startError = "The rug could not be started: " + e.Message;
                Debug.WriteLine("Rug overlay failed: " + e);
                _ready.Set();
            }
        }) { IsBackground = true, Name = "Rug overlay" };
        thread.Start();
        _ready.Wait();
        return _startError;
    }

    public static void Stop() => _running = false;

    public static void SetRug(string path) => Post(() =>
    {
        if (rug_load_texture(path) != 0)
            RebuildAt(_rugScale);
    });

    public static void SetScale(float scale) => Post(() =>
    {
        _rugScale = Math.Clamp(scale, 0.25f, 0.75f);
        _currentScale = _rugScale;
        rug_resize(_rugScale);
    });

    public static void Redrop() => Post(() =>
    {
        var pose = new float[3];
        rug_get_pose(pose);
        rug_layout(pose[0], pose[1], pose[2], MenuDrop);
    });

    public static void Center() => Post(() => rug_layout(_width / 2f, _height / 2f, 0f, CenterDrop));

    public static void CoverIcons() => Post(() =>
    {
        if (_lastBumps.Length < 5)
        {
            var pose = new float[3];
            rug_get_pose(pose);
            rug_layout(pose[0], pose[1], pose[2], MenuDrop);
            return;
        }
        float lox = float.MaxValue, loy = float.MaxValue, hix = float.MinValue, hiy = float.MinValue;
        for (int i = 0; i + 4 < _lastBumps.Length; i += 5)
        {
            lox = Math.Min(lox, _lastBumps[i]); hix = Math.Max(hix, _lastBumps[i]);
            loy = Math.Min(loy, _lastBumps[i + 1]); hiy = Math.Max(hiy, _lastBumps[i + 1]);
        }
        var b = new float[4];
        rug_get_bounds(b);
        float halfW = (b[2] - b[0]) / 2f, halfH = (b[3] - b[1]) / 2f;
        float cx = Math.Clamp((lox + hix) / 2f, halfW, _width - halfW);
        float cy = Math.Clamp((loy + hiy) / 2f, halfH, _height - halfH);
        rug_layout(cx, cy, 0f, MenuDrop);
    });

    public static void PileIcons() => Post(() =>
    {
        var bounds = new float[4];
        rug_get_bounds(bounds);
        IntPtr host = _host;
        Task.Run(() => Report(IconPile.Pile(host, _width, _height, bounds)));
    });

    public static void RestoreIcons() => Post(() =>
    {
        IntPtr host = _host;
        Task.Run(() => Report(IconPile.Restore(host)));
    });

    private static void Post(Action action) => _commands.Enqueue(action);

    private static void Report(string? message)
    {
        if (message != null) Notifier?.Invoke(message);
    }

    private static void RebuildAt(float scale)
    {
        rug_resize(scale);
        _currentScale = scale;
    }

    private static void Loop(AppSettings settings)
    {
        SetProcessDPIAware();

        _width = GetSystemMetrics(SM_CXSCREEN);
        _height = GetSystemMetrics(SM_CYSCREEN);
        int width = _width, height = _height;

        _host = FindDesktopHost();
        if (_host == IntPtr.Zero)
        {
            _startError = "Could not find the desktop window, so the rug cannot be shown.";
            _ready.Set();
            return;
        }

        if (rug_init(width, height) == 0)
        {
            _startError = "Direct3D 11 could not be started on this computer.";
            _ready.Set();
            return;
        }

        string? texture = settings.RugPath != null && File.Exists(settings.RugPath)
            ? settings.RugPath
            : Assets.BuiltinRugs().FirstOrDefault();
        if (texture == null || rug_load_texture(texture) == 0)
        {
            _startError = "The rug picture could not be loaded.";
            rug_shutdown();
            _ready.Set();
            return;
        }
        // Put back the exact drape from last time: rebuild at its saved scale, then (once the bumps are known)
        // restore every vertex. A missing or mismatched file leaves the fresh centred drop below instead.
        bool haveDrape = DrapeStore.TryLoad(out float drapeScale, out int drapeW, out int drapeH, out float[] drapePoints);
        // The saved positions are screen pixels. On another screen size they would land off-screen, out of reach
        // of the mouse, so drop a fresh rug instead.
        if (haveDrape && (drapeW != _width || drapeH != _height))
            haveDrape = false;
        if (haveDrape)
        {
            _rugScale = Math.Clamp(drapeScale, 0.25f, 0.75f);
            _currentScale = _rugScale;
        }
        rug_rebuild(_rugScale, DefaultDrop);

        IntPtr instance = GetModuleHandle(null);
        var wc = new WNDCLASS
        {
            lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_wndProc),
            hInstance = instance,
            hCursor = LoadCursor(IntPtr.Zero, (IntPtr)32512),
            lpszClassName = "RugOverlay",
        };
        RegisterClassW(ref wc);

        IntPtr hwnd = CreateWindowExW(
            (uint)(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT),
            "RugOverlay", "Rug", WS_POPUP,
            0, 0, width, height,
            IntPtr.Zero, IntPtr.Zero, instance, IntPtr.Zero);
        if (hwnd == IntPtr.Zero)
        {
            _startError = "The overlay window could not be created.";
            rug_shutdown();
            _ready.Set();
            return;
        }

        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        IntPtr screenDc = GetDC(IntPtr.Zero);
        IntPtr memDc = CreateCompatibleDC(screenDc);
        var bmi = new BITMAPINFO
        {
            bmiHeader = new BITMAPINFOHEADER
            {
                biSize = Marshal.SizeOf<BITMAPINFOHEADER>(),
                biWidth = width,
                biHeight = -height,
                biPlanes = 1,
                biBitCount = 32,
                biCompression = 0,
            },
        };
        IntPtr dib = CreateDIBSection(memDc, ref bmi, 0, out IntPtr bits, IntPtr.Zero, 0);
        IntPtr oldBitmap = SelectObject(memDc, dib);

        var size = new SIZE { cx = width, cy = height };
        var origin = new POINT();
        var activeRect = new int[4];
        var blend = new BLENDFUNCTION
        {
            BlendOp = AC_SRC_OVER,
            BlendFlags = 0,
            SourceConstantAlpha = 255,
            AlphaFormat = AC_SRC_ALPHA,
        };
        IntPtr originPtr = Marshal.AllocHGlobal(Marshal.SizeOf<POINT>());
        IntPtr sizePtr = Marshal.AllocHGlobal(Marshal.SizeOf<SIZE>());
        IntPtr blendPtr = Marshal.AllocHGlobal(Marshal.SizeOf<BLENDFUNCTION>());
        IntPtr dirtyPtr = Marshal.AllocHGlobal(Marshal.SizeOf<RECT>());
        Marshal.StructureToPtr(origin, originPtr, false);
        Marshal.StructureToPtr(size, sizePtr, false);
        Marshal.StructureToPtr(blend, blendPtr, false);
        var update = new UPDATELAYEREDWINDOWINFO
        {
            cbSize = Marshal.SizeOf<UPDATELAYEREDWINDOWINFO>(),
            hdcDst = screenDc,
            pptDst = originPtr,
            psize = sizePtr,
            hdcSrc = memDc,
            pptSrc = originPtr,
            pblend = blendPtr,
            dwFlags = ULW_ALPHA,
        };
        bool presented = false;
        bool handlesShown = false;

        QueryRefresh();
        // A high-resolution timer paces a moving rug to the display's refresh; IntPtr.Zero on Windows before 1803 falls
        // back to a plain sleep. dueNext is the clock time (seconds) of the next frame boundary.
        IntPtr frameTimer = CreateWaitableTimerExW(IntPtr.Zero, null, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        double dueNext = 0;

        var clock = Stopwatch.StartNew();
        double last = 0;
        bool clickable = false;
        bool wasDown = false;
        bool rotating = false;
        int grabbedCorner = -1;
        float prevAngle = 0;
        float prevDist = 1;
        float pendingFactor = 1;
        float[] corners = new float[8];
        float[] pose = new float[3];
        bool dragging = false;
        IntPtr zAbove = IntPtr.Zero, zBelow = IntPtr.Zero;
        long lastIconScan = 0;
        Task<float[]>? scanTask = null;
        bool wasSettled = false;
        float[]? drapeBuf = null;
        float[] lastScanRaw = Array.Empty<float>();
        float[] appliedPose = new float[3];
        float[] scanPose = new float[3];

        SetClickThrough(hwnd, true);
        ApplyBumps(DesktopIcons.Scan(_host, width, height));
        // The drape was saved over these same icons, so restore it now that the ground is set. The count only
        // differs when the rug picture (its aspect) changed since; then the fresh drop above stands.
        int drapeCount = rug_point_count();
        if (haveDrape && drapePoints.Length == drapeCount * 3)
            rug_set_points(drapePoints, drapeCount);
        _ready.Set();

        while (_running)
        {
            while (_commands.TryDequeue(out Action? command))
            {
                try { command(); }
                catch (Exception e) { Debug.WriteLine("Rug command failed: " + e); }
            }

            while (PeekMessage(out MSG msg, IntPtr.Zero, 0, 0, PM_REMOVE))
            {
                TranslateMessage(ref msg);
                DispatchMessage(ref msg);
            }

            GetCursorPos(out POINT cursor);

            bool overOverlay = WindowFromPoint(cursor) == hwnd;
            float px = cursor.x;
            float py = height - cursor.y;
            bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            bool alt = AltDown();

            // Decide click-through before handling input, so overOverlay is current when the corner grab and the drag
            // read it below. The rug is clickable when the cursor is over it, or over a corner handle while Alt is
            // held. Re-read overOverlay the instant WS_EX_TRANSPARENT is dropped (verified same-thread immediate by
            // tests/clickthrough-test): otherwise the first click after the cursor reaches the rug is lost, because
            // overOverlay was taken at the top of the loop while the window was still click-through.
            bool hit = rug_hit_test(px, py) != 0;
            bool wantClick = hit || rotating || dragging || (alt && NearCorner(px, py, corners) >= 0);
            if (wantClick != clickable)
            {
                clickable = wantClick;
                SetClickThrough(hwnd, !wantClick);
                if (wantClick) overOverlay = WindowFromPoint(cursor) == hwnd;
            }

            // Alt on its own only shows the corner handles; it must not disturb the rug. The rug is lifted when a
            // manipulation actually starts (the corner grab below), so Alt+Tab or a stray Alt leaves it in place.

            if (alt && down && !wasDown && !dragging && overOverlay && NearCorner(px, py, corners) >= 0)
            {
                rotating = true;
                rug_hold_begin();
                grabbedCorner = NearCorner(px, py, corners);
                rug_get_pose(pose);
                float sx = px - pose[0], sy = py - pose[1];
                prevAngle = MathF.Atan2(sy, sx);
                prevDist = MathF.Sqrt(sx * sx + sy * sy);
                pendingFactor = 1f;
            }
            else if (rotating)
            {
                rug_get_pose(pose);
                float dx = px - pose[0], dy = py - pose[1];
                if (!down)
                {
                    rotating = false;

                    if (MathF.Abs(pendingFactor - 1f) > 0.001f)
                    {
                        _rugScale = Math.Clamp(_rugScale * pendingFactor, 0.25f, 0.75f);
                        _currentScale = _rugScale;
                        rug_resize(_rugScale);
                    }
                    rug_hold_end();
                }
                else
                {

                    float a = MathF.Atan2(dy, dx);
                    float d = a - prevAngle;
                    if (d > MathF.PI) d -= 2 * MathF.PI;
                    if (d < -MathF.PI) d += 2 * MathF.PI;
                    rug_rotate(d);
                    prevAngle = a;

                    float dist = MathF.Sqrt(dx * dx + dy * dy);
                    if (prevDist > 1f && dist > 1f && MathF.Abs(dist - prevDist) > 0.5f)
                    {
                        float target = _rugScale * dist / prevDist;
                        float newScale = Math.Clamp(target, 0.25f, 0.75f);
                        if (MathF.Abs(newScale - _rugScale) > 1e-4f)
                        {
                            _rugScale = newScale;
                            _currentScale = _rugScale;
                            rug_resize(_rugScale);
                        }
                        if (newScale != target && grabbedCorner >= 0)
                        {

                            rug_get_corners(corners);
                            float cx = corners[grabbedCorner * 2], cy = corners[grabbedCorner * 2 + 1];
                            SetCursorPos((int)MathF.Round(cx), height - (int)MathF.Round(cy));
                            float ex = cx - pose[0], ey = cy - pose[1];
                            dist = MathF.Sqrt(ex * ex + ey * ey);
                            prevAngle = MathF.Atan2(ey, ex);
                        }
                        prevDist = dist;
                    }
                    else if (prevDist <= 1f) prevDist = dist;
                }
            }

            if (!rotating)
            {
                if (!dragging)
                {

                    if (down && !wasDown && hit && overOverlay && rug_mouse_down(px, py) != 0)
                        dragging = true;
                }
                else
                {
                    rug_mouse_drag(px, py);
                    if (!down)
                    {
                        rug_mouse_up();
                        dragging = false;
                    }
                }
            }
            wasDown = down;

            // Windows keeps the shell's desktop window at the bottom, so where SetWindowPos leaves the overlay differs
            // between versions: on Windows 11 26200 it stays above Progman, never below it. The overlay is put back
            // only when a window next to it changed since it was last put back.
            if (GetWindow(hwnd, GW_HWNDPREV) != zAbove || GetWindow(hwnd, GW_HWNDNEXT) != zBelow)
            {
                SetWindowPos(hwnd, _host, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                zAbove = GetWindow(hwnd, GW_HWNDPREV);
                zBelow = GetWindow(hwnd, GW_HWNDNEXT);
            }

            long nowMs = Environment.TickCount64;
            if (scanTask == null && !dragging && nowMs - lastIconScan > IconRefreshMs)
            {
                lastIconScan = nowMs;
                IntPtr host = _host;
                scanTask = Task.Run(() => DesktopIcons.Scan(host, width, height));
            }
            if (scanTask != null && scanTask.IsCompleted)
            {
                float[] raw = scanTask.Result;
                scanTask = null;
                // Re-apply the bumps only when the icons actually moved, or the rug slid far enough to cover
                // different ones. GroupPile reads the live pose through rug_hit_test, so re-running it on a
                // sub-pixel tremble resends near-identical bumps and resets the sleep window every scan, which
                // keeps the rug awake for good. A few pixels of real movement still re-groups the pile.
                const float regroupMovePx = 4f;
                const float regroupTurnRad = 0.02f;
                rug_get_pose(scanPose);
                bool iconsMoved = !MemoryMarshal.AsBytes(raw.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(lastScanRaw.AsSpan()));
                float dx = scanPose[0] - appliedPose[0], dy = scanPose[1] - appliedPose[1];
                bool rugMoved = dx * dx + dy * dy > regroupMovePx * regroupMovePx
                    || MathF.Abs(scanPose[2] - appliedPose[2]) > regroupTurnRad;
                if (iconsMoved || rugMoved)
                {
                    ApplyBumps(raw);
                    lastScanRaw = raw;
                    appliedPose[0] = scanPose[0];
                    appliedPose[1] = scanPose[1];
                    appliedPose[2] = scanPose[2];
                }
            }

            double now = clock.Elapsed.TotalSeconds;
            rug_step((float)(now - last));
            last = now;

            // Save the drape each time the rug comes to rest, so a restart puts the exact folds back. This fires
            // only on the settle edge, so a sleeping rug writes nothing and a drag writes once when it stops.
            bool settledNow = rug_is_settled() != 0;
            if (settledNow && !wasSettled)
            {
                int n = rug_point_count();
                if (n > 0)
                {
                    if (drapeBuf == null || drapeBuf.Length < n * 3) drapeBuf = new float[n * 3];
                    if (rug_get_points(drapeBuf, n) == n) DrapeStore.Save(_currentScale, _width, _height, drapeBuf, n);
                }
            }
            wasSettled = settledNow;

            // The handles sit inside the rug's area, so a redraw of that area erases them.
            if (!alt && handlesShown)
            {
                rug_invalidate();
                handlesShown = false;
            }
            var dirty = new RECT();
            if (rug_render(bits, width * 4, activeRect) != 0)
                dirty = new RECT { left = activeRect[0], top = activeRect[1], right = activeRect[0] + activeRect[2], bottom = activeRect[1] + activeRect[3] };
            bool drawn = dirty.right > dirty.left && dirty.bottom > dirty.top;
            if (alt)
            {
                rug_get_corners(corners);
                if (drawn || !handlesShown)
                {
                    dirty = Union(dirty, DrawCornerHandles(bits, width, height, corners));
                    handlesShown = true;
                }
            }

            // Only pixels inside the dirty rectangle changed since the last update, so only those go to the compositor.
            if (!presented || (dirty.right > dirty.left && dirty.bottom > dirty.top))
            {
                Marshal.StructureToPtr(dirty, dirtyPtr, false);
                update.prcDirty = presented ? dirtyPtr : IntPtr.Zero;
                UpdateLayeredWindowIndirect(hwnd, ref update);
                presented = true;
            }

            // A moving rug is paced to the display's refresh, so a drag looks as smooth as the monitor allows (60 Hz or
            // 120 Hz) instead of the 32 fps a Thread.Sleep(16) gives at the default 15.6 ms timer. While the rug sleeps
            // nothing is drawn, so a plain sleep keeps the idle wake-ups low and input latency unchanged.
            if ((drawn || dragging || rotating) && frameTimer != IntPtr.Zero)
            {
                double frame = 1.0 / Math.Max(24, _refreshHz);
                double now2 = clock.Elapsed.TotalSeconds;
                // Resume from now after a sleep or an overrun, so the timer never tries to catch up a long gap.
                dueNext = (dueNext <= now2 ? now2 : dueNext) + frame;
                double waitSec = dueNext - clock.Elapsed.TotalSeconds;
                if (waitSec > 0.0002)
                {
                    long due = -(long)(waitSec * 1e7); // 100 ns units, negative for a relative wait
                    SetWaitableTimer(frameTimer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false);
                    WaitForSingleObject(frameTimer, FrameMs * 2);
                }
            }
            else
                Thread.Sleep(FrameMs);
        }

        rug_shutdown();
        if (frameTimer != IntPtr.Zero) CloseHandle(frameTimer);
        Marshal.FreeHGlobal(originPtr);
        Marshal.FreeHGlobal(sizePtr);
        Marshal.FreeHGlobal(blendPtr);
        Marshal.FreeHGlobal(dirtyPtr);
        SelectObject(memDc, oldBitmap);
        DeleteObject(dib);
        DeleteDC(memDc);
        ReleaseDC(IntPtr.Zero, screenDc);
        DestroyWindow(hwnd);
    }

    private static bool AltDown() =>
        ((GetAsyncKeyState(VK_LMENU) | GetAsyncKeyState(VK_RMENU)) & 0x8000) != 0;

    // The primary display's current refresh, so a moving rug is paced to it (60 Hz or 120 Hz). Left at 60 when the
    // call fails or reports a rate too low to trust.
    private static void QueryRefresh()
    {
        var mode = new DEVMODE { dmSize = (ushort)Marshal.SizeOf<DEVMODE>() };
        if (EnumDisplaySettingsW(null, ENUM_CURRENT_SETTINGS, ref mode) && mode.dmDisplayFrequency >= 24)
            _refreshHz = (int)mode.dmDisplayFrequency;
    }

    private static void ApplyBumps(float[] bumps)
    {
        _lastBumps = bumps;
        if (IconPile.HasHome) bumps = GroupPile(bumps);
        // The same bumps again would only restart the ground animation from where it already is.
        if (MemoryMarshal.AsBytes(bumps.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(_sentBumps.AsSpan()))) return;
        _sentBumps = bumps;
        rug_set_bumps(bumps, bumps.Length / 5);
    }

    private static float[] GroupPile(float[] bumps)
    {
        const float cell = 64f;
        const float radius = 0.47f * 64f * 1.12f;
        var cells = new Dictionary<(int, int), (float sx, float sy, int n)>();
        var result = new List<float>();
        for (int i = 0; i + 4 < bumps.Length; i += 5)
        {
            float x = bumps[i], y = bumps[i + 1];
            if (rug_hit_test(x, y) == 0)
            {
                result.Add(bumps[i]); result.Add(bumps[i + 1]); result.Add(bumps[i + 2]); result.Add(bumps[i + 3]); result.Add(bumps[i + 4]);
                continue;
            }
            var key = ((int)MathF.Floor(x / cell), (int)MathF.Floor(y / cell));
            cells.TryGetValue(key, out var c);
            cells[key] = (c.sx + x, c.sy + y, c.n + 1);
        }
        foreach (var c in cells.Values)
        {
            float height = Math.Min(50f, 24f + 6f * MathF.Pow(c.n, 0.7f));
            result.Add(c.sx / c.n); result.Add(c.sy / c.n); result.Add(radius); result.Add(radius); result.Add(height);
        }
        return result.ToArray();
    }

    private static IntPtr OnWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_DISPLAYCHANGE)
        {
            QueryRefresh();
            return IntPtr.Zero;
        }
        if (msg == WM_MOUSEWHEEL)
        {
            long v = wParam.ToInt64();
            int delta = (short)((v >> 16) & 0xFFFF);
            if (AltDown())
            {
                _rugScale = Math.Clamp(_rugScale + delta / 120f * 0.02f, 0.25f, 0.75f);
                _currentScale = _rugScale;
                rug_resize(_rugScale);
            }
            else
            {
                rug_rotate(delta / 120f * 0.08f);
            }
            return IntPtr.Zero;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    private static int NearCorner(float px, float py, float[] corners)
    {
        const float reach = 14f;
        for (int i = 0; i < 4; i++)
        {
            float dx = px - corners[i * 2], dy = py - corners[i * 2 + 1];
            if (dx * dx + dy * dy <= reach * reach) return i;
        }
        return -1;
    }

    // Returns the area it painted, clipped to the screen.
    private static RECT DrawCornerHandles(IntPtr bits, int width, int height, float[] corners)
    {
        const int radius = 7;
        const int blue = unchecked((int)0xFF007AFF);
        var painted = new RECT();
        for (int i = 0; i < 4; i++)
        {
            int cx = (int)MathF.Round(corners[i * 2]);
            int cy = height - 1 - (int)MathF.Round(corners[i * 2 + 1]);
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++)
                {
                    int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height) continue;
                    Marshal.WriteInt32(bits, (y * width + x) * 4, blue);
                }
            var box = new RECT
            {
                left = Math.Max(0, cx - radius),
                top = Math.Max(0, cy - radius),
                right = Math.Min(width, cx + radius + 1),
                bottom = Math.Min(height, cy + radius + 1),
            };
            painted = Union(painted, box);
        }
        return painted;
    }

    private static RECT Union(RECT a, RECT b)
    {
        if (a.right <= a.left || a.bottom <= a.top) return b;
        if (b.right <= b.left || b.bottom <= b.top) return a;
        return new RECT
        {
            left = Math.Min(a.left, b.left),
            top = Math.Min(a.top, b.top),
            right = Math.Max(a.right, b.right),
            bottom = Math.Max(a.bottom, b.bottom),
        };
    }

    private static void SetClickThrough(IntPtr hwnd, bool through)
    {
        long ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE).ToInt64();
        ex = through ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, new IntPtr(ex));
    }

    private static IntPtr FindDesktopHost()
    {
        IntPtr progman = FindWindow("Progman", null);
        if (progman == IntPtr.Zero) return IntPtr.Zero;

        SendMessageTimeout(progman, WM_SPAWN_WORKER, IntPtr.Zero, IntPtr.Zero, 0, 1000, out _);

        IntPtr host = IntPtr.Zero;
        EnumWindows((top, _) =>
        {
            if (FindWindowEx(top, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
                host = top;
            return true;
        }, IntPtr.Zero);

        return host != IntPtr.Zero ? host : progman;
    }
}
