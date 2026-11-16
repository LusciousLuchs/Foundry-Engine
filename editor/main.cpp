// Foundry Editor: a scene editor driven by the same text commands as `fscene`.
//   foundry_editor.exe <scene.fscene> [-c "command"]... [--render out.ppm] [--size WxH]
//                      [--select name] [--save]
// Interactive: edits are saved to the file immediately and the file is watched, so
// changes made by another tool (fscene, a text editor, Claude) appear live.
// --render: applies -c commands, draws the scene through its saved camera, writes a
// screenshot and exits (nothing is saved unless --save).
#include "compat/raylib.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneRender.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

using namespace foundry::compat;
using namespace foundry::scene;
namespace fs = std::filesystem;

namespace {

struct LogLine { std::string text; bool ok; double time; };

struct Orbit {
    Vector3 target{0, 1, 0};
    float yaw = 0.7f, pitch = 0.45f, dist = 14.0f;
    Vector3 position() const {
        return {target.x + dist * std::cos(pitch) * std::cos(yaw), target.y + dist * std::sin(pitch),
                target.z + dist * std::cos(pitch) * std::sin(yaw)};
    }
    void adopt(Vector3 pos, Vector3 tgt) {
        target = tgt;
        Vector3 d = Vector3Subtract(pos, tgt);
        dist = std::max(0.5f, Vector3Length(d));
        pitch = std::asin(std::clamp(d.y / dist, -1.0f, 1.0f));
        yaw = std::atan2(d.z, d.x);
    }
};

struct Editor {
    Scene scene;
    std::string path;
    Orbit orbit;
    std::string selected;                    // entity name, empty = none
    bool consoleFocus = false, showHelp = false, showGrid = true, holdSave = false;
    std::string input;
    std::vector<std::string> history;
    int histPos = -1;
    std::deque<LogLine> log;
    fs::file_time_type stamp{};
    double nextWatch = 0;
    float listScroll = 0;
    float snap = 0.5f;
    Vector3 lastCamPos{}, lastCamTarget{};

    void say(const std::string& t, bool ok = true) {
        log.push_back({t, ok, GetTime()});
        while (log.size() > 40) log.pop_front();
    }
    fs::file_time_type fileStamp() const {
        std::error_code ec; auto t = fs::last_write_time(path, ec);
        return ec ? fs::file_time_type{} : t;
    }
    void adoptSceneCamera() {
        orbit.adopt(scene.env.camPos, scene.env.camTarget);
        lastCamPos = scene.env.camPos; lastCamTarget = scene.env.camTarget;
    }
    void saveNow(bool force = false) {
        if (holdSave && !force) return;
        std::string e;
        if (scene.save(path, e)) { stamp = fileStamp(); holdSave = false; }
        else say(e, false);
    }
    // Run one command; autosave if it changed the scene.
    Result run(const std::string& cmd, bool echo = true) {
        Result r = scene.exec(cmd);
        if (!r.ok) { if (echo) say(r.message, false); return r; }
        if (echo && !r.message.empty()) say(r.message);
        if (r.changed) {
            saveNow();
            if (holdSave) say("not saved: the file on disk has errors (Ctrl+S overwrites it)", false);
            if (scene.env.camPos.x != lastCamPos.x || scene.env.camPos.y != lastCamPos.y || scene.env.camPos.z != lastCamPos.z ||
                scene.env.camTarget.x != lastCamTarget.x || scene.env.camTarget.y != lastCamTarget.y || scene.env.camTarget.z != lastCamTarget.z)
                adoptSceneCamera();
        }
        if (!selected.empty() && !scene.find(selected)) selected.clear();
        return r;
    }
    void reloadIfChanged() {
        if (GetTime() < nextWatch) return;
        nextWatch = GetTime() + 0.3;
        auto t = fileStamp();
        if (t == stamp) return;
        stamp = t;
        std::string e;
        if (scene.load(path, e)) { holdSave = false; adoptSceneCamera(); say("reloaded from disk"); }
        else { holdSave = true; say("not reloaded: " + e, false); }
        if (!selected.empty() && !scene.find(selected)) selected.clear();
    }
    Entity* sel() { return selected.empty() ? nullptr : scene.find(selected); }
};

bool inRect(Vector2 p, float x, float y, float w, float h) { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
float snapTo(float v, float s) { return std::round(v / s) * s; }
void panel(float x, float y, float w, float h) { DrawRectangleRounded({x, y, w, h}, 0.04f, 8, {12, 14, 20, 215}); }
std::string fmt(float v) { char b[32]; std::snprintf(b, sizeof b, "%.3g", double(v)); return b; }
std::string fmt3(Vector3 v) { return fmt(v.x) + ", " + fmt(v.y) + ", " + fmt(v.z); }

const char* kHelp =
    "MOUSE   left click: select    right drag: orbit    middle drag: pan    wheel: zoom\n"
    "ADD     1 cube  2 sphere  3 cylinder  4 cone  5 plane  6 light   (at the view target)\n"
    "EDIT    arrows: move X/Z   PgUp/PgDn: move Y   Q/E: rotate   - / = : scale\n"
    "        Shift = fine step   Del: delete   Ctrl+D: duplicate   Ctrl+Z/Y: undo/redo\n"
    "VIEW    F: focus selection   C: save view as scene camera   G: grid   [ ]: cycle selection\n"
    "FILE    saved on every change; Ctrl+S saves now; external edits reload automatically\n"
    "CONSOLE Tab or /: type any command (see `help`), Enter runs it, Esc leaves, Up/Down: history\n"
    "H: toggle this help   F12: screenshot (foundry_shot.ppm)";

} // namespace

int main(int argc, char** argv) {
    Editor ed;
    std::vector<std::string> cmds;
    std::string renderOut, preselect;
    int W = 1280, H = 720;
    bool saveInRender = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) cmds.push_back(argv[++i]);
        else if (a == "--render" && i + 1 < argc) renderOut = argv[++i];
        else if (a == "--select" && i + 1 < argc) preselect = argv[++i];
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
        else if (a == "--save") saveInRender = true;
        else if (a[0] != '-' && ed.path.empty()) ed.path = a;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (ed.path.empty()) {
        std::fprintf(stderr, "usage: foundry_editor <scene.fscene> [-c cmd]... [--render out.ppm] [--size WxH] [--select name] [--save]\n");
        return 2;
    }
    const bool renderMode = !renderOut.empty();

    if (fs::exists(ed.path)) {
        std::string e;
        if (!ed.scene.load(ed.path, e)) { std::fprintf(stderr, "error: %s\n", e.c_str()); return 1; }
    }
    ed.stamp = ed.fileStamp();
    ed.adoptSceneCamera();
    int rc = 0;
    for (const std::string& c : cmds) {
        Result r = ed.scene.exec(c);
        if (!r.message.empty() && c != "list") std::printf("%s: %s\n", r.ok ? "ok" : "error", r.message.c_str());
        if (!r.ok) rc = 1;
    }
    if (rc) return rc;
    if (!cmds.empty()) ed.adoptSceneCamera();
    if (!cmds.empty() && (!renderMode || saveInRender)) ed.saveNow(true);
    ed.selected = preselect;

    InitWindow(W, H, (std::string("Foundry Editor - ") + fs::path(ed.path).filename().string()).c_str());
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // Esc leaves the console; close with the window button
    SceneRenderer sr;
    int frame = 0;

    while (!WindowShouldClose()) {
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 mouse = GetMousePosition();
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const float leftW = 240, rightW = 300, consoleH = 26;
        bool overUI = inRect(mouse, 8, 8, leftW, float(sh) - consoleH - 16) || inRect(mouse, 0, float(sh) - consoleH, float(sw), consoleH) ||
                      (ed.sel() && inRect(mouse, float(sw) - rightW - 8, 8, rightW, 250));

        if (!renderMode) {
            ed.reloadIfChanged();

            // ---- text input / console ----
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ed.consoleFocus) ed.input += char(ch);
                else if (ch == '/') { ed.consoleFocus = true; }
            }
            if (ed.consoleFocus) {
                if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !ed.input.empty()) ed.input.pop_back();
                if (IsKeyPressed(KEY_ENTER)) {
                    if (!ed.input.empty()) {
                        ed.say("> " + ed.input);
                        Result r = ed.run(ed.input);
                        if (r.ok && r.message.find('\n') != std::string::npos) ed.say("(see console history)");
                        if (r.ok && (ed.input.rfind("add ", 0) == 0 || r.message.rfind("added ", 0) == 0)) {
                            std::string n = r.message.substr(r.message.rfind(' ') + 1);
                            if (ed.scene.find(n)) ed.selected = n;
                        }
                        ed.history.push_back(ed.input);
                    }
                    ed.input.clear(); ed.histPos = -1;
                }
                if (IsKeyPressed(KEY_UP) && !ed.history.empty()) {
                    ed.histPos = ed.histPos < 0 ? int(ed.history.size()) - 1 : std::max(0, ed.histPos - 1);
                    ed.input = ed.history[size_t(ed.histPos)];
                }
                if (IsKeyPressed(KEY_DOWN) && ed.histPos >= 0) {
                    ed.histPos++;
                    if (ed.histPos >= int(ed.history.size())) { ed.histPos = -1; ed.input.clear(); }
                    else ed.input = ed.history[size_t(ed.histPos)];
                }
                if (IsKeyPressed(KEY_ESCAPE)) ed.consoleFocus = false;
            } else {
                auto step = [&]() { return shift ? ed.snap * 0.2f : ed.snap; };
                auto key = [](int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); };
                if (IsKeyPressed(KEY_TAB)) ed.consoleFocus = true;
                if (IsKeyPressed(KEY_H)) ed.showHelp = !ed.showHelp;
                if (IsKeyPressed(KEY_F12)) { TakeScreenshot("foundry_shot.ppm"); ed.say("screenshot: foundry_shot.ppm"); }
                if (IsKeyPressed(KEY_G)) ed.showGrid = !ed.showGrid;
                if (ctrl && IsKeyPressed(KEY_S)) { ed.saveNow(true); ed.say("saved " + ed.path); }
                if (ctrl && IsKeyPressed(KEY_Z)) ed.run(shift ? "redo" : "undo");
                if (ctrl && IsKeyPressed(KEY_Y)) ed.run("redo");
                if (IsKeyPressed(KEY_C) && !ctrl) {
                    Vector3 p = ed.orbit.position();
                    ed.run("camera pos=" + fmt3(p) + " target=" + fmt3(ed.orbit.target) + " fov=" + fmt(ed.scene.env.camFov));
                    ed.say("view saved as scene camera");
                }
                // add primitives at the (snapped) view target
                static const char* kAdd[] = {"cube", "sphere", "cylinder", "cone", "plane", "light"};
                static const float kAddY[] = {0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 3.0f};
                for (int i = 0; i < 6; ++i)
                    if (!ctrl && IsKeyPressed(KEY_ONE + i)) {
                        Vector3 t = ed.orbit.target;
                        Result r = ed.run(std::string("add ") + kAdd[i] + " pos=" + fmt(snapTo(t.x, ed.snap)) + "," + fmt(kAddY[i]) + "," + fmt(snapTo(t.z, ed.snap)));
                        if (r.ok) ed.selected = r.message.substr(r.message.rfind(' ') + 1);
                    }
                if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
                    int n = int(ed.scene.entities.size());
                    if (n) {
                        int i = ed.selected.empty() ? -1 : ed.scene.indexOf(ed.selected);
                        i = IsKeyPressed(KEY_RIGHT_BRACKET) ? (i + 1) % n : (i <= 0 ? n - 1 : i - 1);
                        ed.selected = ed.scene.entities[size_t(i)].name;
                    }
                }
                if (Entity* e = ed.sel()) {
                    const std::string n = e->name;
                    auto fv = [&](float x, float y, float z) { return fmt(x) + "," + fmt(y) + "," + fmt(z); };
                    float s = step();
                    if (key(KEY_LEFT)) ed.run("move " + n + " " + fv(-s, 0, 0), false);
                    if (key(KEY_RIGHT)) ed.run("move " + n + " " + fv(s, 0, 0), false);
                    if (key(KEY_UP)) ed.run("move " + n + " " + fv(0, 0, -s), false);
                    if (key(KEY_DOWN)) ed.run("move " + n + " " + fv(0, 0, s), false);
                    if (key(KEY_PAGE_UP)) ed.run("move " + n + " " + fv(0, s, 0), false);
                    if (key(KEY_PAGE_DOWN)) ed.run("move " + n + " " + fv(0, -s, 0), false);
                    if (key(KEY_Q)) ed.run("rotate " + n + " 0," + fmt(shift ? -5.0f : -15.0f) + ",0", false);
                    if (key(KEY_E)) ed.run("rotate " + n + " 0," + fmt(shift ? 5.0f : 15.0f) + ",0", false);
                    if (key(KEY_MINUS)) ed.run("scale " + n + " 0.9", false);
                    if (key(KEY_EQUAL)) ed.run("scale " + n + " 1.1", false);
                    if (IsKeyPressed(KEY_DELETE)) { ed.run("delete " + n); ed.selected.clear(); }
                    else if (ctrl && IsKeyPressed(KEY_D)) {
                        Result r = ed.run("duplicate " + n);
                        if (r.ok) ed.selected = r.message.substr(r.message.rfind(' ') + 1);
                    }
                    else if (IsKeyPressed(KEY_F)) ed.orbit.target = e->pos;
                }
            }

            // ---- mouse: camera + selection ----
            if (!overUI || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                Vector2 d = GetMouseDelta();
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    ed.orbit.yaw += d.x * 0.01f;
                    ed.orbit.pitch = std::clamp(ed.orbit.pitch + d.y * 0.01f, -1.5f, 1.5f);
                }
                if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                    Vector3 pos = ed.orbit.position();
                    Vector3 fwd = Vector3Normalize(Vector3Subtract(ed.orbit.target, pos));
                    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0, 1, 0}));
                    Vector3 up = Vector3CrossProduct(right, fwd);
                    float k = ed.orbit.dist * 0.0016f;
                    ed.orbit.target = Vector3Add(ed.orbit.target, Vector3Add(Vector3Scale(right, -d.x * k), Vector3Scale(up, d.y * k)));
                }
                float wheel = GetMouseWheelMove();
                if (wheel != 0 && !overUI) ed.orbit.dist = std::clamp(ed.orbit.dist * std::pow(0.9f, wheel), 0.8f, 200.0f);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (inRect(mouse, 8, 8, leftW, float(sh) - consoleH - 16)) {
                    int row = int((mouse.y - 8 - 40 + ed.listScroll) / 16);
                    if (row >= 0 && row < int(ed.scene.entities.size())) ed.selected = ed.scene.entities[size_t(row)].name;
                } else if (!overUI) {
                    Camera3D cam{ed.orbit.position(), ed.orbit.target, {0, 1, 0}, ed.scene.env.camFov, CAMERA_PERSPECTIVE};
                    Ray r = GetScreenToWorldRay(mouse, cam);
                    float t;
                    int hit = ed.scene.pick({r.position, r.direction}, t);
                    ed.selected = hit >= 0 ? ed.scene.entities[size_t(hit)].name : std::string();
                }
            }
            if (inRect(mouse, 8, 8, leftW, float(sh) - consoleH - 16)) {
                ed.listScroll -= GetMouseWheelMove() * 48;
                float maxScroll = std::max(0.0f, float(ed.scene.entities.size()) * 16 - (float(sh) - 140));
                ed.listScroll = std::clamp(ed.listScroll, 0.0f, maxScroll);
            }
        }

        // ---------------------------------------------------------------- draw
        Camera3D cam{renderMode ? ed.scene.env.camPos : ed.orbit.position(),
                     renderMode ? ed.scene.env.camTarget : ed.orbit.target, {0, 1, 0}, ed.scene.env.camFov, CAMERA_PERSPECTIVE};
        if (renderMode && frame == 3) TakeScreenshot(renderOut.c_str());

        BeginDrawing();
        sr.applyEnvironment(ed.scene);
        BeginMode3D(cam);
        if (ed.showGrid) SceneRenderer::drawGrid(20, 1);
        sr.drawEntities(ed.scene, true);
        if (Entity* e = ed.sel()) {
            SceneRenderer::drawOutline(*e, {255, 210, 60, 255});
            if (e->kind == Kind::Light) DrawSphereWires(e->pos, e->range, 10, 16, {120, 100, 40, 255});
        }
        EndMode3D();

        // left: entity list
        panel(8, 8, leftW, float(sh) - consoleH - 16);
        DrawText(TextFormat("ENTITIES (%d)   snap %s", int(ed.scene.entities.size()), fmt(ed.snap).c_str()), 18, 16, 12, {255, 210, 60, 255});
        DrawText("[H] help   [Tab] console", 18, 33, 12, {140, 146, 160, 255});
        for (size_t i = 0; i < ed.scene.entities.size(); ++i) {
            const Entity& e = ed.scene.entities[i];
            float y = 8 + 40 + float(i) * 16 - ed.listScroll;
            if (y < 44 || y > float(sh) - consoleH - 24) continue;
            if (e.name == ed.selected) DrawRectangle(12, int(y) - 1, int(leftW) - 8, 16, {60, 90, 150, 255});
            DrawRectangle(18, int(y) + 3, 8, 8, e.color);
            DrawText(TextFormat("%-8s %s%s", kindName(e.kind), e.name.c_str(), e.visible ? "" : " (hidden)"), 32, int(y), 12, e.visible ? Color{225, 228, 235, 255} : Color{120, 124, 135, 255});
        }

        // right: properties of the selection
        if (Entity* e = ed.sel()) {
            float x = float(sw) - rightW - 8;
            panel(x, 8, rightW, 250);
            int tx = int(x) + 12, ty = 16;
            DrawText(TextFormat("%s  [%s]", e->name.c_str(), kindName(e->kind)), tx, ty, 12, {255, 210, 60, 255});
            auto row = [&](const char* k, const std::string& v) {
                ty += 20; DrawText(k, tx, ty, 12, {140, 146, 160, 255}); DrawText(v.c_str(), tx + 70, ty, 12, {225, 228, 235, 255});
            };
            row("pos", fmt3(e->pos));
            if (e->kind != Kind::Light) { row("rot", fmt3(e->rot)); row("scale", fmt3(e->scale)); }
            row("color", TextFormat("%d, %d, %d", e->color.r, e->color.g, e->color.b));
            DrawRectangle(tx + 70 + 100, ty + 1, 14, 12, e->color);
            if (e->kind == Kind::Light) { row("range", fmt(e->range)); row("intensity", fmt(e->intensity)); }
            row("visible", e->visible ? "yes" : "no");
            DrawText("edit with keys or the console:", tx, ty + 30, 12, {140, 146, 160, 255});
            DrawText(TextFormat("set %s pos=0,1,0", e->name.c_str()), tx, ty + 46, 12, {120, 200, 255, 255});
        }

        // bottom: log + console
        double now = GetTime();
        int ly = sh - int(consoleH) - 4;
        int shown = 0;
        for (auto it = ed.log.rbegin(); it != ed.log.rend() && shown < 8; ++it) {
            if (now - it->time > 8.0 && !ed.consoleFocus) break;
            std::string line = it->text.substr(0, it->text.find('\n'));
            ly -= 16;
            Color c = it->ok ? Color{190, 220, 190, 255} : Color{255, 130, 120, 255};
            DrawRectangle(int(leftW) + 18, ly, MeasureText(line.c_str(), 12) + 8, 16, {12, 14, 20, 190});
            DrawText(line.c_str(), int(leftW) + 22, ly, 12, c);
            ++shown;
        }
        DrawRectangle(0, sh - int(consoleH), sw, int(consoleH), {12, 14, 20, 235});
        if (ed.consoleFocus) {
            DrawText(TextFormat("> %s%s", ed.input.c_str(), (int(now * 2) % 2) ? "_" : " "), 12, sh - int(consoleH) + 6, 12, {255, 255, 255, 255});
        } else {
            DrawText(TextFormat("%s%s   (Tab or / to type a command)", fs::path(ed.path).filename().string().c_str(), ed.holdSave ? "  [NOT SAVING: file has errors]" : ""),
                     12, sh - int(consoleH) + 6, 12, ed.holdSave ? Color{255, 130, 120, 255} : Color{140, 146, 160, 255});
        }
        if (ed.showHelp) {
            int hw = MeasureText("CONSOLE Tab or /: type any command (see `help`), Enter runs it, Esc leaves, Up/Down: history", 12) + 32;
            panel(float(sw) / 2 - float(hw) / 2, 60, float(hw), 150);
            DrawText(kHelp, sw / 2 - hw / 2 + 16, 72, 12, {225, 228, 235, 255});
        }
        EndDrawing();
        ++frame;
        if (renderMode && frame > 4) break;
    }
    CloseWindow();
    return 0;
}
