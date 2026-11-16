#pragma once
// Win32 window + input. Key codes and mouse-button numbers match raylib so the
// compat layer (IsKeyDown, GetMousePosition, ...) maps 1:1.
#include <cstdint>
#include <string>
#include <vector>

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
    KEY_CAPS_LOCK = 280, KEY_SCROLL_LOCK, KEY_NUM_LOCK, KEY_PRINT_SCREEN, KEY_PAUSE,
    KEY_KP_0 = 320, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
    KEY_KP_DECIMAL = 330, KEY_KP_DIVIDE, KEY_KP_MULTIPLY, KEY_KP_SUBTRACT, KEY_KP_ADD, KEY_KP_ENTER, KEY_KP_EQUAL,
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
    bool isKeyRepeated(int k) const { return valid(k) && repeated_[k]; }   // OS auto-repeat events only
    unsigned popChar();   // next typed character (unicode code point) or 0
    bool isMouseDown(int b) const { return b >= 0 && b < 8 && mdown_[b]; }
    bool isMousePressed(int b) const { return b >= 0 && b < 8 && mpressed_[b]; }
    bool isMouseReleased(int b) const { return b >= 0 && b < 8 && mreleased_[b]; }
    float mouseX() const { return mx_; }
    float mouseY() const { return my_; }
    float mouseDX() const { return dx_; }
    float mouseDY() const { return dy_; }
    float mouseWheel() const { return wheel_; }
    int popKey();         // next key pressed (raylib key code) or 0
    // ---- window state ----
    bool focused() const { return focused_; }
    bool fullscreen() const { return fullscreen_; }
    bool borderless() const { return borderless_; }
    void setFullscreen(bool on);          // borderless window covering the current monitor (not exclusive mode)
    void setBorderless(bool on);          // same, tracked separately like raylib's flag
    void setSize(int clientW, int clientH);
    void setPosition(int x, int y);
    void setMinSize(int w, int h);
    void setTopmost(bool on);
    void setResizable(bool on);
    void setUndecorated(bool on);
    bool topmost() const { return topmost_; }
    bool resizable() const { return resizable_; }
    bool undecorated() const { return undecorated_; }
    void setCursorPos(float x, float y);  // client coordinates
    void hideCursor();
    void showCursor();
    bool cursorHidden() const { return cursorHidden_; }
    // monitors (index 0 = primary)
    static int monitorCount();
    static int monitorWidth(int i);
    static int monitorHeight(int i);
    static int monitorRefresh(int i);
    static void monitorPosition(int i, int& x, int& y);
    static std::string monitorName(int i);
    int currentMonitor() const;
    // clipboard / file drop
    static std::string clipboard();
    static void setClipboard(const std::string& utf8);
    bool filesDropped() const { return !dropped_.empty(); }
    std::vector<std::string> takeDropped() { auto d = std::move(dropped_); dropped_.clear(); return d; }
    void enableCursor();
    void disableCursor();            // hidden + captured, deltas keep flowing
    bool cursorEnabled() const { return cursorEnabled_; }

    // internal: called from the window procedure
    long handleMessage(unsigned msg, uintptr_t wp, intptr_t lp, bool& handled);

private:
    static bool valid(int k) { return k >= 0 && k < 512; }
    void* hwnd_ = nullptr;
    void* hinst_ = nullptr;
    bool close_ = false, resized_ = false, cursorEnabled_ = true, focused_ = true, cursorHidden_ = false;
    bool fullscreen_ = false, borderless_ = false, topmost_ = false, resizable_ = true, undecorated_ = false;
    int minW_ = 0, minH_ = 0;
    bool warpPending_ = false;   // cursor was just captured: the next poll starts from zero delta
    long savedStyle_ = 0; int savedRect_[4]{};
    unsigned short keyQ_[32]{}; int keyHead_ = 0, keyTail_ = 0;
    std::vector<std::string> dropped_;
    void applyStyle();
    void setFull(bool on, bool& flag);
    int w_ = 0, h_ = 0;
    bool down_[512]{}, pressed_[512]{}, released_[512]{}, repeated_[512]{};
    unsigned chars_[64]{}; int charHead_ = 0, charTail_ = 0;
    bool mdown_[8]{}, mpressed_[8]{}, mreleased_[8]{};
    float mx_ = 0, my_ = 0, dx_ = 0, dy_ = 0, wheel_ = 0;
    float lastMx_ = 0, lastMy_ = 0;
};

} // namespace foundry::platform
