#pragma once
// Access to the compat layer's window and renderer for companion modules (ImGui backend, tools).
#include "platform/Window.hpp"
#include "render/Renderer.hpp"

namespace foundry::compat::internal {
render::Renderer& renderer();
platform::Window& window();
} // namespace foundry::compat::internal
