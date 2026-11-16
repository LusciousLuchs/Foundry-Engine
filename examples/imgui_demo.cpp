// Dear ImGui through the Foundry renderer: the kinds of widgets Gunsmith3D uses, over a lit 3D scene.
//   imgui_demo.exe [--frames N] [--screenshot out.ppm] [--demo]     (--demo opens ImGui's full demo window)
#include "compat/raylib.hpp"
#include "ui/rlImGui.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace foundry::compat;

int main(int argc, char** argv) {
    int maxFrames = -1; const char* shot = nullptr; bool fullDemo = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--demo")) fullDemo = true;
    }
    InitWindow(1280, 720, "Foundry ImGui");
    rlImGuiSetup(true);

    float speed = 1.0f, tint[3] = {0.9f, 0.45f, 0.2f};
    bool shadows = true, wire = false, showSecond = true;
    int mode = 1, clicks = 0;
    char name[64] = "barrel_01";
    const char* modes[] = {"Sandbox", "Test bench", "Map editor"};
    Camera3D cam{{7, 4.5f, 7}, {0, 1, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
    SetShadowBounds({0, 0, 0}, 10);

    int frame = 0;
    while (!WindowShouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        float t = (maxFrames > 0) ? float(frame) * 0.05f : float(GetTime());
        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) TakeScreenshot(shot);
        EnableShadows(shadows);

        BeginDrawing();
        ClearBackground({30, 34, 44, 255});
        BeginMode3D(cam);
        DrawPlane({0, 0, 0}, {16, 16}, {150, 150, 155, 255});
        DrawCube({0, 1, 0}, 2, 2, 2, {Color{(unsigned char)(tint[0] * 255), (unsigned char)(tint[1] * 255), (unsigned char)(tint[2] * 255), 255}});
        DrawSphere({2.5f, 0.8f, 1.0f + std::sin(t * speed)}, 0.8f, {90, 200, 120, 255});
        if (wire) DrawCubeWires({0, 1, 0}, 2.1f, 2.1f, 2.1f, WHITE);
        EndMode3D();

        rlImGuiBegin();
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(340, 330), ImGuiCond_Once);
        if (ImGui::Begin("Properties")) {
            ImGui::Text("Frame %d  (%.0f fps)", frame, double(ImGui::GetIO().Framerate));
            ImGui::SeparatorText("Part");
            ImGui::InputText("Name", name, sizeof name);
            ImGui::Combo("Mode", &mode, modes, 3);
            ImGui::SliderFloat("Speed", &speed, 0.0f, 5.0f);
            ImGui::ColorEdit3("Tint", tint);
            ImGui::Checkbox("Shadows", &shadows); ImGui::SameLine(); ImGui::Checkbox("Wireframe", &wire);
            if (ImGui::Button("Fire")) ++clicks;
            ImGui::SameLine(); ImGui::Text("fired %d", clicks);
            ImGui::Separator();
            if (ImGui::BeginTabBar("tabs")) {
                if (ImGui::BeginTabItem("Stats")) { ImGui::BulletText("Mass  %.1f g", 412.5); ImGui::BulletText("Energy  %.0f J", 1893.0); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Notes")) { ImGui::TextWrapped("Click, drag and type: input flows from the compat layer."); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
            ImGui::Text("WantCaptureMouse: %d", ImGui::GetIO().WantCaptureMouse);
        }
        ImGui::End();
        if (showSecond) {
            ImGui::SetNextWindowPos(ImVec2(900, 380), ImGuiCond_Once);
            ImGui::Begin("Log", &showSecond);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "renderer: Vulkan 1.3");
            ImGui::TextDisabled("(unicode: caf\xc3\xa9)");
            ImGui::End();
        }
        if (fullDemo) ImGui::ShowDemoWindow();
        rlImGuiEnd();
        EndDrawing();
        ++frame;
    }
    rlImGuiShutdown();
    CloseWindow();
    std::printf("ok: %d frames (clicks=%d name=%s shadows=%d)\n", frame, clicks, name, shadows);
    return 0;
}
