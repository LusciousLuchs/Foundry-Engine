#include "platform/Window.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstring>

namespace foundry::platform {

static int mapKey(WPARAM vk, LPARAM lp) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == VK_SPACE) return int(vk);
    if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + int(vk - VK_F1);
    bool ext = (lp >> 24) & 1;
    switch (vk) {
        case VK_ESCAPE: return KEY_ESCAPE;   case VK_RETURN: return KEY_ENTER;
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
        case WM_KILLFOCUS:
            std::memset(down_, 0, sizeof down_); std::memset(mdown_, 0, sizeof mdown_); return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            int k = mapKey(wp, lp);
            if (k) {
                if (lp & 0x40000000) repeated_[k] = true;        // bit 30: key was already down
                else if (!down_[k]) pressed_[k] = true;
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
            int b = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : 2;
            if (!mdown_[b]) mpressed_[b] = true;
            mdown_[b] = true;
            SetCapture(HWND(hwnd_));
            return 0;
        }
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
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

unsigned Window::popChar() {
    if (charTail_ == charHead_) return 0;
    unsigned c = chars_[charTail_]; charTail_ = (charTail_ + 1) & 63;
    return c;
}

void Window::enableCursor() {
    if (cursorEnabled_) return;
    cursorEnabled_ = true;
    ClipCursor(nullptr);
    while (ShowCursor(TRUE) < 0) {}
}
void Window::disableCursor() {
    if (!cursorEnabled_) return;
    cursorEnabled_ = false;
    while (ShowCursor(FALSE) >= 0) {}
    RECT cr; GetClientRect(HWND(hwnd_), &cr);
    POINT tl{cr.left, cr.top}, br{cr.right, cr.bottom};
    ClientToScreen(HWND(hwnd_), &tl); ClientToScreen(HWND(hwnd_), &br);
    RECT clip{tl.x, tl.y, br.x, br.y}; ClipCursor(&clip);
    POINT c{(cr.right) / 2, (cr.bottom) / 2}; ClientToScreen(HWND(hwnd_), &c); SetCursorPos(c.x, c.y);
}

} // namespace foundry::platform
