#pragma once
// Dear ImGui on the Foundry renderer, with the same entry points the raylib rlImGui add-on provides
// (the subset Gunsmith3D uses). Include after "imgui.h".
//
//   rlImGuiSetup(true);            // once, after InitWindow
//   ...  BeginDrawing(); rlImGuiBegin(); <ImGui calls>; rlImGuiEnd(); EndDrawing();
//   rlImGuiShutdown();
//
// Input (mouse, keyboard, typed characters) comes from the compat layer; fonts and the glyph atlas are managed
// through ImGui 1.92's texture requests (ImGuiBackendFlags_RendererHasTextures), so fonts can grow at runtime.
#include "compat/raylib.hpp"
#include "imgui.h"

namespace foundry::compat {

void rlImGuiSetup(bool darkTheme);                       // creates the context and applies a theme
void rlImGuiSetLoadFontsCallback(void (*callback)());    // called once during setup, before the first frame, to add fonts
void rlImGuiBegin();                                     // new ImGui frame (feeds input and time)
void rlImGuiEnd();                                       // renders the frame's draw lists
void rlImGuiShutdown();
void rlImGuiImageSize(const Texture2D* image, int width, int height);
bool rlImGuiImageButtonSize(const char* name, const Texture2D* image, ImVec2 size);

} // namespace foundry::compat
