// Validates the "first click on the sleeping rug is lost" fix without the full app or a mouse.
// The overlay reads overOverlay = (WindowFromPoint == hwnd); the drag needs that to flip the instant click-through
// (WS_EX_TRANSPARENT) is dropped. This checks whether WindowFromPoint reflects the style change immediately on the
// same thread (so re-reading overOverlay is enough), or whether it needs a SWP_FRAMECHANGED to apply.
#define NOMINMAX
#include <windows.h>
#include <cstdio>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProc(h, m, w, l); }

int main() {
    const wchar_t* cls = L"RugClickThroughTest";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    // Same extended styles as the overlay, topmost and opaque enough that WindowFromPoint can return it.
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        cls, L"ct", WS_POPUP, 100, 100, 300, 300, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { std::printf("CreateWindowExW failed %lu\n", GetLastError()); return 2; }
    SetLayeredWindowAttributes(hwnd, 0, 220, LWA_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    POINT p{ 200, 200 }; // inside the window
    auto ex = [&] { return GetWindowLongW(hwnd, GWL_EXSTYLE); };
    auto overlayAt = [&] { return WindowFromPoint(p) == hwnd; };

    bool whileTransparent = overlayAt();

    SetWindowLongW(hwnd, GWL_EXSTYLE, ex() & ~WS_EX_TRANSPARENT);
    bool afterRemoveImmediate = overlayAt(); // the fix relies on this being true

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    bool afterFrameChanged = overlayAt();

    std::printf("while WS_EX_TRANSPARENT:        WindowFromPoint==hwnd = %d (expect 0)\n", whileTransparent);
    std::printf("after remove (same thread):     WindowFromPoint==hwnd = %d (expect 1 => re-read is enough)\n", afterRemoveImmediate);
    std::printf("after SWP_FRAMECHANGED:         WindowFromPoint==hwnd = %d (expect 1)\n", afterFrameChanged);

    int fail = 0;
    if (whileTransparent) { std::printf("UNEXPECTED: transparent window was hit-tested\n"); ++fail; }
    // The overlay (Overlay.cs SetClickThrough + the re-read right after it) relies on the removal taking effect
    // immediately, without SWP_FRAMECHANGED. So the fix is sufficient only when afterRemoveImmediate holds; fail
    // otherwise, even if SWP_FRAMECHANGED alone would have worked, because the shipped code does not call it.
    if (!afterRemoveImmediate) {
        std::printf("FAIL: re-reading WindowFromPoint after dropping WS_EX_TRANSPARENT did not take effect "
                    "immediately (SWP_FRAMECHANGED would have worked: %d); the overlay's re-read-only fix cannot work\n",
                    afterFrameChanged);
        ++fail;
    } else {
        std::printf("re-read-only fix is sufficient\n");
    }
    DestroyWindow(hwnd);
    return fail ? 1 : 0;
}
