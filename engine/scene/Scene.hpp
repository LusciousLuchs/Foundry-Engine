#pragma once
// Scene model + command interpreter shared by every editing front-end (GUI editor,
// `fscene` CLI, tests, Claude). The scene FILE is just a list of commands, one per
// line, so a human or an agent can read and edit it directly:
//
//     sun dir=-0.8,-0.7,-0.45 color=255,242,220 intensity=2.2
//     cube crate pos=0,1,0 rot=0,30,0 scale=2,2,2 color=200,120,60
//     light lamp pos=0,3,0 range=9 color=255,90,60 intensity=14
//
// Transform order: scale, then rotate about X, Y, Z (degrees), then translate.
#include "compat/raymath.hpp"
#include "scene/Obj.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace foundry::scene {
using foundry::compat::Color;
using foundry::compat::Matrix;
using foundry::compat::Vector3;

enum class Kind { Cube, Sphere, Cylinder, Cone, Plane, Light, Mesh };
const char* kindName(Kind k);
bool parseKind(const std::string& s, Kind& out);

struct Entity {
    std::string name;
    Kind kind = Kind::Cube;
    Vector3 pos{0, 0, 0}, rot{0, 0, 0}, scale{1, 1, 1};
    Color color{200, 200, 200, 255};
    std::string file;                        // meshes only: .obj path (relative to the scene file)
    float range = 8.0f, intensity = 10.0f;   // lights only
    bool visible = true;
};

struct Environment {
    Vector3 sunDir{-0.8f, -0.7f, -0.45f};
    Color sunColor{255, 242, 220, 255};
    float sunIntensity = 2.2f;
    Color sky{40, 54, 82, 255}, ground{15, 13, 13, 255};
    bool shadows = true;
    Vector3 shadowCenter{0, 0, 0};
    float shadowRadius = 14.0f;
    Color background{22, 26, 36, 255};
    Vector3 camPos{11, 6, 9}, camTarget{0, 1, 0};
    float camFov = 50.0f;
};

struct Result {
    bool ok = true;
    bool changed = false;
    std::string message;
};

struct Ray3 { Vector3 origin, dir; };   // dir normalised

class Scene {
public:
    std::vector<Entity> entities;
    Environment env;

    // Execute one command line (see `help`). Mutating commands are undoable.
    Result exec(const std::string& line);

    bool load(const std::string& path, std::string& error);   // replaces the scene
    bool save(const std::string& path, std::string& error) const;
    std::string serialize() const;                              // == the file contents

    Entity* find(const std::string& name);
    const Entity* find(const std::string& name) const;
    int indexOf(const std::string& name) const;

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    uint64_t revision() const { return rev_; }   // bumps on every change, incl. undo/redo

    // Directory that mesh paths are relative to (set automatically by load()).
    void setBaseDir(const std::string& dir) { baseDir_ = dir; models_.clear(); }
    // Loaded (and cached) OBJ for a mesh path, or nullptr with `error` set.
    const ObjMesh* model(const std::string& file, std::string* error = nullptr) const;

    static Matrix modelMatrix(const Entity& e);
    // Nearest visible entity hit by the ray (-1 if none); t = distance along ray.
    int pick(const Ray3& ray, float& t) const;
    bool raycast(const Entity& e, const Ray3& ray, float& t) const;

    static const char* helpText();

private:
    struct Snapshot { std::vector<Entity> entities; Environment env; };
    Result dispatch(const std::vector<std::string>& tk);
    Result cmdAdd(Kind kind, const std::vector<std::string>& tk, size_t first);
    Result cmdSet(const std::vector<std::string>& tk);
    std::string uniqueName(const std::string& base) const;

    std::vector<Snapshot> undo_, redo_;
    std::string baseDir_;
    mutable std::map<std::string, std::unique_ptr<ObjMesh>> models_;
    uint64_t rev_ = 0;
    bool loading_ = false;
};

} // namespace foundry::scene
