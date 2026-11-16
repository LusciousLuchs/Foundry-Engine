#pragma once
// Minimal Wavefront OBJ loader (v / vn / f, negative indices, n-gon fan triangulation).
// Pure C++: used by the scene model for bounds + picking and by the renderer for drawing.
#include <string>
#include <vector>

namespace foundry::scene {

struct ObjMesh {
    std::vector<float> positions;   // 3 per vertex, unindexed triangle list
    std::vector<float> normals;     // 3 per vertex (file normals, or flat face normals)
    float lo[3]{0, 0, 0}, hi[3]{0, 0, 0};
    size_t triangleCount() const { return positions.size() / 9; }
};

bool loadObj(const std::string& path, ObjMesh& out, std::string& error);

} // namespace foundry::scene
