#pragma once
// Draws a Scene through the raylib-compat API. Windows/Vulkan only.
#include "compat/raylib.hpp"
#include "scene/Scene.hpp"

namespace foundry::scene {

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();
    // Applies sun/ambient/shadows and the point lights. Call between BeginDrawing and EndDrawing.
    void applyEnvironment(const Scene& scene);
    // Draw the entities (call inside BeginMode3D).
    void drawEntities(const Scene& scene, bool showLightMarkers);
    // Editor helpers (call inside BeginMode3D).
    static void drawOutline(const Entity& e, foundry::compat::Color c);
    static void drawGrid(float half, float step);

private:
    foundry::compat::Mesh cube_, sphere_, cylinder_, cone_, plane_;
    foundry::compat::Material mat_;
};

} // namespace foundry::scene
