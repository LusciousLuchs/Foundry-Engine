// Fonts (stb_truetype atlas), MeasureTextEx, DrawRing. Loads a TTF by path (default: DejaVu Sans via Wine's Z: drive).
//   text_demo.exe [--font path] [--frames N] [--screenshot out.ppm]
#include "compat/raylib.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace foundry::compat;

int main(int argc, char** argv) {
    int maxFrames = -1; const char* shot = nullptr; std::string fontPath = "Z:\\usr\\share\\fonts\\truetype\\dejavu\\DejaVuSans.ttf";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--font") && i + 1 < argc) fontPath = argv[++i];
    }
    InitWindow(1000, 520, "text demo");
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0xA1; c <= 0xFF; ++c) cps.push_back(c);              // Latin-1
    for (int c : {0x2713, 0x2192, 0x00B0, 0x03C0, 0x20AC}) cps.push_back(c);   // check, arrow, degree, pi, euro
    Font font = LoadFontEx(fontPath.c_str(), 40, cps.data(), int(cps.size()));
    if (!IsFontReady(font)) { std::fprintf(stderr, "font failed to load: %s\n", fontPath.c_str()); return 1; }

    int frame = 0;
    while (!WindowShouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) TakeScreenshot(shot);
        BeginDrawing();
        ClearBackground(Color{28, 31, 40, 255});
        struct Line { const char* text; float size; float y; };
        const Line lines[] = {
            {"The quick brown fox jumps over the lazy dog", 36, 20},
            {"Gunsmith 3D  \xe2\x86\x92  caf\xc3\xa9, \xc3\xbc, \xc3\xb1, 90\xc2\xb0, \xcf\x80 \xe2\x82\xac 9.5 \xe2\x9c\x93", 28, 80},
            {"Small text at 14 px: 0123456789 mass 412.5 g, energy 1893 J", 14, 130},
            {"Line one\nline two (newline)", 22, 170},
        };
        for (const Line& l : lines) {
            Vector2 m = MeasureTextEx(font, l.text, l.size, 1.0f);
            DrawRectangleLinesEx({20 - 2, l.y - 2, m.x + 4, m.y + 4}, 1, Color{255, 90, 90, 255});   // measured box
            DrawTextEx(font, l.text, {20, l.y}, l.size, 1.0f, Color{235, 238, 245, 255});
        }
        DrawTextEx(font, "tinted", {20, 260}, 30, 1, Color{120, 200, 255, 255});
        DrawTextEx(font, "translucent", {160, 260}, 30, 1, Color{255, 255, 255, 110});
        DrawText("built-in bitmap font for comparison", 20, 310, 14, Color{200, 200, 200, 255});
        DrawRing({760, 380}, 50, 70, 0, 360, 48, Color{255, 200, 60, 255});
        DrawRing({880, 380}, 30, 70, -90, 135, 48, Color{90, 200, 255, 255});     // an arc, like a scope's tube rim
        EndDrawing();
        ++frame;
    }
    UnloadFont(font);
    CloseWindow();
    std::printf("ok: %d frames\n", frame);
    return 0;
}
