// The Shell automation headers need the OLE declarations that WIN32_LEAN_AND_MEAN leaves out.
#undef WIN32_LEAN_AND_MEAN

#include "rug.h"

#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shobjidl.h>

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

// Initializes COM for the calling thread when it has not been, and undoes only what it did.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

// The desktop's folder view, reached through the Shell's documented automation objects
// (ShellWindows, the desktop's top-level browser, its active view) instead of Explorer's memory.
HRESULT DesktopFolderView(IFolderView2** view) {
    ComPtr<IShellWindows> windows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows));
    if (FAILED(hr)) return hr;

    VARIANT location{};
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    VARIANT root{};
    long hwnd = 0;
    ComPtr<IDispatch> dispatch;
    hr = windows->FindWindowSW(&location, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &dispatch);
    if (hr != S_OK) return FAILED(hr) ? hr : E_FAIL;

    ComPtr<IServiceProvider> provider;
    if (FAILED(hr = dispatch->QueryInterface(IID_PPV_ARGS(&provider)))) return hr;
    ComPtr<IShellBrowser> browser;
    if (FAILED(hr = provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) return hr;
    ComPtr<IShellView> shellView;
    if (FAILED(hr = browser->QueryActiveShellView(&shellView))) return hr;
    return shellView->QueryInterface(IID_PPV_ARGS(view));
}

}

extern "C" int rug_desktop_items(int* xy, int capacity, int* layout) {
    if (!layout || capacity < 0 || (capacity > 0 && !xy)) return -1;
    ComScope com;
    ComPtr<IFolderView2> view;
    if (FAILED(DesktopFolderView(&view))) return -1;

    int count = 0;
    POINT spacing{};
    FOLDERVIEWMODE mode{};
    int iconSize = 0;
    if (FAILED(view->ItemCount(SVGIO_ALLVIEW, &count)) || FAILED(view->GetSpacing(&spacing)) ||
        FAILED(view->GetViewModeAndIconSize(&mode, &iconSize)))
        return -1;
    layout[0] = spacing.x;
    layout[1] = spacing.y;
    layout[2] = iconSize;

    for (int i = 0; i < count && i < capacity; ++i) {
        PITEMID_CHILD item = nullptr;
        if (FAILED(view->Item(i, &item))) return -1;
        POINT position{};
        const HRESULT hr = view->GetItemPosition(item, &position);
        CoTaskMemFree(item);
        if (FAILED(hr)) return -1;
        xy[i * 2] = position.x;
        xy[i * 2 + 1] = position.y;
    }
    return count;
}

extern "C" int rug_set_wallpaper(const wchar_t* path, int position) {
    ComScope com;
    ComPtr<IDesktopWallpaper> wallpaper;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper))))
        return 0;
    if (position >= DWPOS_CENTER && position <= DWPOS_SPAN &&
        FAILED(wallpaper->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(position))))
        return 0;
    if (!path || !*path) return 1;
    return SUCCEEDED(wallpaper->SetWallpaper(nullptr, path)) ? 1 : 0;
}
