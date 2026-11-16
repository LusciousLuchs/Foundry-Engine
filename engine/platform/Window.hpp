#pragma once
// Win32 window + input. Key codes and mouse-button numbers match raylib so the
// compat layer (IsKeyDown, GetMousePosition, ...) maps 1:1.
#include <cstdint>

namespace foundry::platform {

// raylib KeyboardKey values (subset; same numbers as raylib.h).
enum Key : int {
    KEY_NULL = 0, KEY_SPACE = 32, KEY_APOSTROPHE = 39, KEY_COMMA = 44, KEY_MINUS = 45,
    KEY_PERIOD = 46, KEY_SLASH = 47, KEY_ZERO = 48, KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR,
    KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_SEMICOLON = 59, KEY_EQUAL = 61,
    KEY_A = 65, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L,
    KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
    KEY_Z, KEY_LEFT_BRACKET = 91, KEY_BACKSLASH, KEY_RIGHT_BRACKET, KEY_GRAVE = 96,
    KEY_ESCAPE = 256, KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_INSERT, KEY_DELETE,
    KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_HOME, KEY_END,
    KEY_F1 = 290, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_F11, KEY_F12, KEY_LEFT_SHIFT = 340, KEY_LEFT_CONTROL, KEY_LEFT_ALT,
    KEY_RIGHT_SHIFT = 344, KEY_RIGHT_CONTROL, KEY_RIGHT_ALT,
};
enum MouseButton : int { MOUSE_LEFT = 0, MOUSE_RIGHT = 1, MOUSE_MIDDLE = 2 };

class Window {
public:
    Window() = default;
    ~Window() { destroy(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const char* title, int width, int height);
    void destroy();
    void pollEvents();               // call once per frame; updates input state

    bool shouldClose() const { return close_; }
    void requestClose() { close_ = true; }
    void* nativeHandle() const { return hwnd_; }       // HWND
    void* nativeInstance() const { return hinst_; }    // HINSTANCE
    int width() const { return w_; }
    int height() const { return h_; }
    bool resizedFlag() const { return resized_; }
    bool consumeResized() { bool r = resized_; resized_ = false; return r; }
    bool minimized() const { return w_ == 0 || h_ == 0; }

    bool isKeyDown(int k) const { return valid(k) && down_[k]; }
    bool isKeyPressed(int k) const { return valid(k) && pressed_[k]; }
    bool isKeyReleased(int k) const { return valid(k) && released_[k]; }
    bool isMouseDown(int b) const { return b >= 0 && b < 8 && mdown_[b]; }
    bool isMousePressed(int b) const { return b >= 0 && b < 8 && mpressed_[b]; }
    bool isMouseReleased(int b) const { return b >= 0 && b < 8 && mreleased_[b]; }
    float mouseX() const { return mx_; }
    float mouseY() const { return my_; }
    float mouseDX() const { return dx_; }
    float mouseDY() const { return dy_; }
    float mouseWheel() const { return wheel_; }
    void enableCursor();
    void disableCursor();            // hidden + captured, deltas keep flowing
    bool cursorEnabled() const { return cursorEnabled_; }

    // internal: called from the window procedure
    long handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled);

private:
    static bool valid(int k) { return k >= 0 && k < 512; }
    void* hwnd_ = nullptr;
    void* hinst_ = nullptr;
    bool close_ = false, resized_ = false, cursorEnabled_ = true;
    int w_ = 0, h_ = 0;
    bool down_[512]{}, pressed_[512]{}, released_[512]{};
    bool mdown_[8]{}, mpressed_[8]{}, mreleased_[8]{};
    float mx_ = 0, my_ = 0, dx_ = 0, dy_ = 0, wheel_ = 0;
    float lastMx_ = 0, lastMy_ = 0;
};

} // namespace foundry::platform
