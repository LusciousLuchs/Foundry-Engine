#include "platform/Window.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>

namespace foundry::platform {

static int mapKey(WPARAM vk, LPARAM lp) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == VK_SPACE) return int(vk);
    if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + int(vk - VK_F1);
    bool ext = (lp >> 24) & 1;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return KEY_KP_0 + int(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_DECIMAL: return KEY_KP_DECIMAL;   case VK_DIVIDE: return KEY_KP_DIVIDE;
        case VK_MULTIPLY: return KEY_KP_MULTIPLY; case VK_SUBTRACT: return KEY_KP_SUBTRACT;
        case VK_ADD: return KEY_KP_ADD;
        case VK_CAPITAL: return KEY_CAPS_LOCK; case VK_SCROLL: return KEY_SCROLL_LOCK; case VK_NUMLOCK: return KEY_NUM_LOCK;
        case VK_SNAPSHOT: return KEY_PRINT_SCREEN; case VK_PAUSE: return KEY_PAUSE;
        case VK_RETURN: return ext ? KEY_KP_ENTER : KEY_ENTER;
        default: break;
    }
    switch (vk) {
        case VK_ESCAPE: return KEY_ESCAPE;
        case VK_TAB: return KEY_TAB;         case VK_BACK: return KEY_BACKSPACE;
        case VK_INSERT: return KEY_INSERT;   case VK_DELETE: return KEY_DELETE;
        case VK_RIGHT: return KEY_RIGHT;     case VK_LEFT: return KEY_LEFT;
        case VK_DOWN: return KEY_DOWN;       case VK_UP: return KEY_UP;
        case VK_PRIOR: return KEY_PAGE_UP;   case VK_NEXT: return KEY_PAGE_DOWN;
        case VK_HOME: return KEY_HOME;       case VK_END: return KEY_END;
        case VK_OEM_COMMA: return KEY_COMMA; case VK_OEM_MINUS: return KEY_MINUS;
        case VK_OEM_PERIOD: return KEY_PERIOD; case VK_OEM_2: return KEY_SLASH;
        case VK_OEM_1: return KEY_SEMICOLON; case VK_OEM_PLUS: return KEY_EQUAL;
        case VK_OEM_4: return KEY_LEFT_BRACKET; case VK_OEM_5: return KEY_BACKSLASH;
        case VK_OEM_6: return KEY_RIGHT_BRACKET; case VK_OEM_3: return KEY_GRAVE;
        case VK_OEM_7: return KEY_APOSTROPHE;
        case VK_SHIFT: {
            UINT vkx = MapVirtualKey((lp >> 16) & 0xff, MAPVK_VSC_TO_VK_EX);
            return vkx == VK_RSHIFT ? KEY_RIGHT_SHIFT : KEY_LEFT_SHIFT;
        }
        case VK_CONTROL: return ext ? KEY_RIGHT_CONTROL : KEY_LEFT_CONTROL;
        case VK_MENU: return ext ? KEY_RIGHT_ALT : KEY_LEFT_ALT;
        default: return KEY_NULL;
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtr(h, GWLP_USERDATA));
    if (self) {
        bool handled = false;
        long r = self->handleMessage(msg, wp, lp, handled);
        if (handled) return r;
    }
    return DefWindowProc(h, msg, wp, lp);
}

long Window::handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled) {
    handled = true;
    switch (msg) {
        case WM_CLOSE: close_ = true; return 0;
        case WM_SIZE:
            w_ = LOWORD(lp); h_ = HIWORD(lp); resized_ = true; return 0;
        case WM_SETFOCUS: focused_ = true; return 0;
        case WM_KILLFOCUS:
            focused_ = false;
            std::memset(down_, 0, sizeof down_); std::memset(mdown_, 0, sizeof mdown_); return 0;
        case WM_GETMINMAXINFO:
            if (minW_ > 0 || minH_ > 0) {
                auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
                RECT r{0, 0, minW_, minH_};
                AdjustWindowRect(&r, DWORD(GetWindowLong(HWND(hwnd_), GWL_STYLE)), FALSE);
                mm->ptMinTrackSize.x = r.right - r.left; mm->ptMinTrackSize.y = r.bottom - r.top;
                return 0;
            }
            handled = false; return 0;
        case WM_DROPFILES: {
            HDROP d = reinterpret_cast<HDROP>(wp);
            UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                wchar_t w[1024]; DragQueryFileW(d, i, w, 1024);
                char u[3072]; int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
                if (len > 0) dropped_.emplace_back(u);
            }
            DragFinish(d);
            return 0;
        }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            int k = mapKey(wp, lp);
            if (k) {
                if (lp & 0x40000000) repeated_[k] = true;        // bit 30: key was already down
                else if (!down_[k]) { pressed_[k] = true; if (((keyHead_ + 1) & 31) != keyTail_) { keyQ_[keyHead_] = (unsigned short)k; keyHead_ = (keyHead_ + 1) & 31; } }
                down_[k] = true;
            }
            if (msg == WM_SYSKEYDOWN) handled = (wp == VK_MENU || wp == VK_F10); // keep Alt+F4, Alt+Space
            return 0;
        }
        case WM_KEYUP: case WM_SYSKEYUP: {
            int k = mapKey(wp, lp);
            if (k) { down_[k] = false; released_[k] = true; }
            return 0;
        }
        case WM_CHAR:
            if (wp >= 32 && wp != 127 && ((charHead_ + 1) & 63) != charTail_) { chars_[charHead_] = unsigned(wp); charHead_ = (charHead_ + 1) & 63; }
            return 0;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
            mx_ = float(GET_X_LPARAM(lp)); my_ = float(GET_Y_LPARAM(lp));   // button messages carry the position too
            int b = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : 2;
            if (!mdown_[b]) mpressed_[b] = true;
            mdown_[b] = true;
            SetCapture(HWND(hwnd_));
            return 0;
        }
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
            mx_ = float(GET_X_LPARAM(lp)); my_ = float(GET_Y_LPARAM(lp));
            int b = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : 2;
            mdown_[b] = false; mreleased_[b] = true;
            if (!mdown_[0] && !mdown_[1] && !mdown_[2]) ReleaseCapture();
            return 0;
        }
        case WM_MOUSEMOVE: mx_ = float(GET_X_LPARAM(lp)); my_ = float(GET_Y_LPARAM(lp)); return 0;
        case WM_MOUSEWHEEL: wheel_ += float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA); return 0;
        default: handled = false; return 0;
    }
}

bool Window::create(const char* title, int width, int height) {
    hinst_ = GetModuleHandle(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = HINSTANCE(hinst_);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "FoundryWindow";
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND h = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, HINSTANCE(hinst_), nullptr);
    if (!h) { std::fprintf(stderr, "CreateWindow failed (%lu)\n", GetLastError()); return false; }
    hwnd_ = h;
    SetWindowLongPtr(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DragAcceptFiles(h, TRUE);
    RECT cr; GetClientRect(h, &cr);
    w_ = cr.right; h_ = cr.bottom;
    return true;
}

void Window::destroy() {
    if (hwnd_) { SetWindowLongPtr(HWND(hwnd_), GWLP_USERDATA, 0); DestroyWindow(HWND(hwnd_)); hwnd_ = nullptr; }
}

void Window::pollEvents() {
    std::memset(pressed_, 0, sizeof pressed_);
    std::memset(released_, 0, sizeof released_);
    std::memset(repeated_, 0, sizeof repeated_);
    std::memset(mpressed_, 0, sizeof mpressed_);
    std::memset(mreleased_, 0, sizeof mreleased_);
    wheel_ = 0;
    MSG m;
    while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) close_ = true;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (!cursorEnabled_ && GetForegroundWindow() == HWND(hwnd_)) {
        RECT cr; GetClientRect(HWND(hwnd_), &cr);
        POINT c{cr.right / 2, cr.bottom / 2}, p;
        GetCursorPos(&p); ScreenToClient(HWND(hwnd_), &p);
        dx_ = float(p.x - c.x); dy_ = float(p.y - c.y);
        ClientToScreen(HWND(hwnd_), &c); SetCursorPos(c.x, c.y);
    } else {
        dx_ = mx_ - lastMx_; dy_ = my_ - lastMy_;
    }
    lastMx_ = mx_; lastMy_ = my_;
}

int Window::popKey() {
    if (keyTail_ == keyHead_) return 0;
    int k = keyQ_[keyTail_]; keyTail_ = (keyTail_ + 1) & 31;
    return k;
}

// ---- window state ----
static RECT monitorRect(HWND h) {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfo(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}
void Window::setFull(bool on, bool& flag) {
    HWND h = HWND(hwnd_);
    if (on == (fullscreen_ || borderless_)) { flag = on; return; }   // already in the requested mode
    if (on) {
        savedStyle_ = GetWindowLong(h, GWL_STYLE);
        RECT r; GetWindowRect(h, &r);
        savedRect_[0] = r.left; savedRect_[1] = r.top; savedRect_[2] = r.right - r.left; savedRect_[3] = r.bottom - r.top;
        RECT m = monitorRect(h);
        SetWindowLong(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLong(h, GWL_STYLE, savedStyle_);
        SetWindowPos(h, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, savedRect_[0], savedRect_[1], savedRect_[2], savedRect_[3], SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    flag = on;
}
void Window::setFullscreen(bool on) { setFull(on, fullscreen_); }
void Window::setBorderless(bool on) { setFull(on, borderless_); }
void Window::applyStyle() {
    if (fullscreen_ || borderless_) return;
    HWND h = HWND(hwnd_);
    LONG st = undecorated_ ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    if (!undecorated_ && !resizable_) st &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLong(h, GWL_STYLE, st);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
void Window::setResizable(bool on) { resizable_ = on; applyStyle(); }
void Window::setUndecorated(bool on) { undecorated_ = on; applyStyle(); }
void Window::setTopmost(bool on) {
    topmost_ = on;
    SetWindowPos(HWND(hwnd_), on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void Window::setSize(int cw, int ch) {
    if (fullscreen_ || borderless_ || cw < 1 || ch < 1) return;
    HWND h = HWND(hwnd_);
    RECT r{0, 0, cw, ch};
    AdjustWindowRect(&r, DWORD(GetWindowLong(h, GWL_STYLE)), FALSE);
    SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void Window::setPosition(int x, int y) { SetWindowPos(HWND(hwnd_), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
void Window::setMinSize(int w, int h) { minW_ = w; minH_ = h; }
void Window::setCursorPos(float x, float y) {
    POINT p{LONG(x), LONG(y)};
    ClientToScreen(HWND(hwnd_), &p);
    SetCursorPos(p.x, p.y);
    mx_ = x; my_ = y; lastMx_ = x; lastMy_ = y;
}
void Window::hideCursor() { if (!cursorHidden_) { cursorHidden_ = true; while (ShowCursor(FALSE) >= 0) {} } }
void Window::showCursor() { if (cursorHidden_) { cursorHidden_ = false; while (ShowCursor(TRUE) < 0) {} } }

// ---- monitors ----
namespace {
struct Mon { RECT r; std::string name; int hz; };
BOOL CALLBACK monCb(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<Mon>*>(lp);
    MONITORINFOEXA mi; mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(m, &mi)) return TRUE;
    DEVMODEA dm{}; dm.dmSize = sizeof dm;
    int hz = 60;
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) hz = int(dm.dmDisplayFrequency);
    Mon x{mi.rcMonitor, mi.szDevice, hz};
    if (mi.dwFlags & MONITORINFOF_PRIMARY) v->insert(v->begin(), x); else v->push_back(x);
    return TRUE;
}
std::vector<Mon> monitors() {
    std::vector<Mon> v;
    EnumDisplayMonitors(nullptr, nullptr, monCb, reinterpret_cast<LPARAM>(&v));
    return v;
}
}
int Window::monitorCount() { return int(monitors().size()); }
int Window::monitorWidth(int i) { auto m = monitors(); return i >= 0 && i < int(m.size()) ? int(m[size_t(i)].r.right - m[size_t(i)].r.left) : 0; }
int Window::monitorHeight(int i) { auto m = monitors(); return i >= 0 && i < int(m.size()) ? int(m[size_t(i)].r.bottom - m[size_t(i)].r.top) : 0; }
int Window::monitorRefresh(int i) { auto m = monitors(); return i >= 0 && i < int(m.size()) ? m[size_t(i)].hz : 0; }
void Window::monitorPosition(int i, int& x, int& y) { auto m = monitors(); x = y = 0; if (i >= 0 && i < int(m.size())) { x = int(m[size_t(i)].r.left); y = int(m[size_t(i)].r.top); } }
std::string Window::monitorName(int i) { auto m = monitors(); return i >= 0 && i < int(m.size()) ? m[size_t(i)].name : ""; }
int Window::currentMonitor() const {
    RECT mr = monitorRect(HWND(hwnd_));
    auto m = monitors();
    for (size_t i = 0; i < m.size(); ++i) if (m[i].r.left == mr.left && m[i].r.top == mr.top) return int(i);
    return 0;
}

// ---- clipboard ----
std::string Window::clipboard() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) { out.resize(size_t(n - 1)); WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr); }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
void Window::setClipboard(const std::string& u) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    if (n <= 0 || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size_t(n) * sizeof(wchar_t));
    if (g) {
        MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, static_cast<wchar_t*>(GlobalLock(g)), n);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

unsigned Window::popChar() {
    if (charTail_ == charHead_) return 0;
    unsigned c = chars_[charTail_]; charTail_ = (charTail_ + 1) & 63;
    return c;
}

void Window::enableCursor() {
    if (cursorEnabled_) return;
    cursorEnabled_ = true; cursorHidden_ = false;
    ClipCursor(nullptr);
    while (ShowCursor(TRUE) < 0) {}
}
void Window::disableCursor() {
    if (!cursorEnabled_) return;
    cursorEnabled_ = false; cursorHidden_ = true;
    while (ShowCursor(FALSE) >= 0) {}
    RECT cr; GetClientRect(HWND(hwnd_), &cr);
    POINT tl{cr.left, cr.top}, br{cr.right, cr.bottom};
    ClientToScreen(HWND(hwnd_), &tl); ClientToScreen(HWND(hwnd_), &br);
    RECT clip{tl.x, tl.y, br.x, br.y}; ClipCursor(&clip);
    POINT c{(cr.right) / 2, (cr.bottom) / 2}; ClientToScreen(HWND(hwnd_), &c); SetCursorPos(c.x, c.y);
}

} // namespace foundry::platform
