#include "scene/Scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace foundry::scene {
using namespace foundry::compat;

static const char* kKinds[] = {"cube", "sphere", "cylinder", "cone", "plane", "light"};
const char* kindName(Kind k) { return kKinds[int(k)]; }
bool parseKind(const std::string& s, Kind& out) {
    for (int i = 0; i < 6; ++i) if (s == kKinds[i]) { out = Kind(i); return true; }
    return false;
}

// ---------------------------------------------------------------- parsing helpers
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '#' && cur.empty()) break;   // comment only at the start of a token (colors use #rrggbb)
        if (std::isspace(static_cast<unsigned char>(c))) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

static bool parseFloat(const std::string& s, float& v) {
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end && *end == 0 && !s.empty() && std::isfinite(v);
}
// "a,b,c" -> n floats. A single value fills all n (so scale=2 means 2,2,2).
static bool parseFloats(const std::string& s, int n, float* out) {
    std::vector<std::string> parts;
    std::stringstream ss(s); std::string p;
    while (std::getline(ss, p, ',')) parts.push_back(p);
    if (int(parts.size()) == 1 && n > 1) {
        float v; if (!parseFloat(parts[0], v)) return false;
        for (int i = 0; i < n; ++i) out[i] = v;
        return true;
    }
    if (int(parts.size()) != n) return false;
    for (int i = 0; i < n; ++i) if (!parseFloat(parts[i], out[i])) return false;
    return true;
}
static bool parseVec3(const std::string& s, Vector3& v) { float f[3]; if (!parseFloats(s, 3, f)) return false; v = {f[0], f[1], f[2]}; return true; }
static bool parseColor(const std::string& s, Color& c) {
    if (s.size() == 7 && s[0] == '#') {   // #rrggbb
        unsigned v = unsigned(std::strtoul(s.c_str() + 1, nullptr, 16));
        c = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 255};
        return true;
    }
    std::vector<std::string> parts; std::stringstream ss(s); std::string p;
    while (std::getline(ss, p, ',')) parts.push_back(p);
    if (parts.size() != 3 && parts.size() != 4) return false;
    float f[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < parts.size(); ++i) if (!parseFloat(parts[i], f[i]) || f[i] < 0 || f[i] > 255) return false;
    c = {(unsigned char)f[0], (unsigned char)f[1], (unsigned char)f[2], (unsigned char)f[3]};
    return true;
}
static bool validName(const std::string& n) {
    if (n.empty()) return false;
    for (char c : n) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}
static std::string f2s(float v) {
    char b[32]; std::snprintf(b, sizeof b, "%.6g", double(v));
    if (!std::strcmp(b, "-0")) return "0";
    return b;
}
static std::string v3(Vector3 v) { return f2s(v.x) + "," + f2s(v.y) + "," + f2s(v.z); }
static std::string col(Color c) {
    std::string s = std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
    if (c.a != 255) s += "," + std::to_string(c.a);
    return s;
}
static Result err(const std::string& m) { return {false, false, m}; }
static Result ok(const std::string& m, bool changed = false) { return {true, changed, m}; }

// ---------------------------------------------------------------- lookup
int Scene::indexOf(const std::string& name) const {
    for (size_t i = 0; i < entities.size(); ++i) if (entities[i].name == name) return int(i);
    return -1;
}
Entity* Scene::find(const std::string& n) { int i = indexOf(n); return i < 0 ? nullptr : &entities[size_t(i)]; }
const Entity* Scene::find(const std::string& n) const { int i = indexOf(n); return i < 0 ? nullptr : &entities[size_t(i)]; }

std::string Scene::uniqueName(const std::string& base) const {
    for (int i = 1;; ++i) {
        std::string n = base + std::to_string(i);
        if (indexOf(n) < 0) return n;
    }
}

// ---------------------------------------------------------------- entity key=value
static bool applyEntityKV(Entity& e, const std::string& key, const std::string& val, std::string& error) {
    auto bad = [&](const char* what) { error = std::string("bad value '") + val + "' for " + key + " (expected " + what + ")"; return false; };
    if (key == "pos") { if (!parseVec3(val, e.pos)) return bad("x,y,z"); }
    else if (key == "rot") { if (!parseVec3(val, e.rot)) return bad("degrees x,y,z"); }
    else if (key == "scale") { if (!parseVec3(val, e.scale)) return bad("n or x,y,z"); }
    else if (key == "color") { if (!parseColor(val, e.color)) return bad("r,g,b[,a] (0-255) or #rrggbb"); }
    else if (key == "range") { if (!parseFloat(val, e.range) || e.range <= 0) return bad("number > 0"); }
    else if (key == "intensity") { if (!parseFloat(val, e.intensity) || e.intensity < 0) return bad("number >= 0"); }
    else if (key == "visible") { float f; if (!parseFloat(val, f)) return bad("0 or 1"); e.visible = f != 0; }
    else { error = "unknown key '" + key + "' (valid: pos rot scale color visible" + std::string(e.kind == Kind::Light ? " range intensity" : "") + ")"; return false; }
    return true;
}

static bool splitKV(const std::string& tok, std::string& k, std::string& v) {
    size_t eq = tok.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    k = lower(tok.substr(0, eq)); v = tok.substr(eq + 1);
    return true;
}

// ---------------------------------------------------------------- commands
Result Scene::cmdAdd(Kind kind, const std::vector<std::string>& tk, size_t i) {
    Entity e; e.kind = kind;
    if (kind == Kind::Plane) e.scale = {10, 1, 10};
    if (kind == Kind::Light) e.color = {255, 255, 255, 255};
    std::string name;
    if (i < tk.size() && tk[i].find('=') == std::string::npos) name = tk[i++];
    if (name.empty()) name = uniqueName(kindName(kind));
    if (!validName(name)) return err("invalid name '" + name + "' (use letters, digits, _ - .)");
    if (indexOf(name) >= 0) return err("an entity named '" + name + "' already exists");
    e.name = name;
    for (; i < tk.size(); ++i) {
        std::string k, v, m;
        if (!splitKV(tk[i], k, v)) return err("expected key=value, got '" + tk[i] + "'");
        if (!applyEntityKV(e, k, v, m)) return err(m);
    }
    entities.push_back(e);
    return ok(std::string("added ") + kindName(kind) + " " + name, true);
}

Result Scene::cmdSet(const std::vector<std::string>& tk) {
    if (tk.size() < 3) return err("usage: set <name> key=value ...");
    Entity* e = find(tk[1]);
    if (!e) return err("no entity named '" + tk[1] + "'");
    Entity copy = *e;
    for (size_t i = 2; i < tk.size(); ++i) {
        std::string k, v, m;
        if (!splitKV(tk[i], k, v)) return err("expected key=value, got '" + tk[i] + "'");
        if (k == "name") {
            if (!validName(v)) return err("invalid name '" + v + "'");
            if (v != copy.name && indexOf(v) >= 0) return err("an entity named '" + v + "' already exists");
            copy.name = v;
        } else if (!applyEntityKV(copy, k, v, m)) return err(m);
    }
    *e = copy;
    return ok("set " + tk[1], true);
}

static bool needArgs(const std::vector<std::string>& tk, size_t n) { return tk.size() >= n; }

Result Scene::dispatch(const std::vector<std::string>& tk) {
    const std::string verb = lower(tk[0]);
    Kind kind;
    if (verb == "add") {
        if (tk.size() < 2 || !parseKind(lower(tk[1]), kind)) return err("usage: add <cube|sphere|cylinder|cone|plane|light> [name] [key=value ...]");
        return cmdAdd(kind, tk, 2);
    }
    if (parseKind(verb, kind)) return cmdAdd(kind, tk, 1);
    if (verb == "set") return cmdSet(tk);
    if (verb == "move" || verb == "rotate" || verb == "scale") {
        if (!needArgs(tk, 3)) return err("usage: " + verb + " <name> x,y,z" + (verb == "scale" ? "  (multiplies; one number scales uniformly)" : "  (adds)"));
        Entity* e = find(tk[1]);
        if (!e) return err("no entity named '" + tk[1] + "'");
        Vector3 d;
        if (!parseVec3(tk[2], d)) return err("bad vector '" + tk[2] + "'");
        if (verb == "move") e->pos = Vector3Add(e->pos, d);
        else if (verb == "rotate") e->rot = Vector3Add(e->rot, d);
        else e->scale = {e->scale.x * d.x, e->scale.y * d.y, e->scale.z * d.z};
        return ok(verb + " " + tk[1], true);
    }
    if (verb == "delete" || verb == "rm") {
        if (!needArgs(tk, 2)) return err("usage: delete <name>");
        int i = indexOf(tk[1]);
        if (i < 0) return err("no entity named '" + tk[1] + "'");
        entities.erase(entities.begin() + i);
        return ok("deleted " + tk[1], true);
    }
    if (verb == "rename") {
        if (!needArgs(tk, 3)) return err("usage: rename <old> <new>");
        return cmdSet({"set", tk[1], "name=" + tk[2]});
    }
    if (verb == "duplicate" || verb == "dup") {
        if (!needArgs(tk, 2)) return err("usage: duplicate <name> [newname] [offset=x,y,z]");
        const Entity* src = find(tk[1]);
        if (!src) return err("no entity named '" + tk[1] + "'");
        Entity e = *src; std::string name; Vector3 off{1, 0, 0};
        for (size_t i = 2; i < tk.size(); ++i) {
            std::string k, v;
            if (splitKV(tk[i], k, v)) { if (k != "offset" || !parseVec3(v, off)) return err("bad option '" + tk[i] + "'"); }
            else name = tk[i];
        }
        if (name.empty()) name = uniqueName(kindName(e.kind));
        if (!validName(name) || indexOf(name) >= 0) return err("invalid or existing name '" + name + "'");
        e.name = name; e.pos = Vector3Add(e.pos, off);
        entities.push_back(e);
        return ok("duplicated " + tk[1] + " as " + name, true);
    }
    if (verb == "clear") { entities.clear(); return ok("cleared all entities", true); }
    if (verb == "sun" || verb == "ambient" || verb == "shadow" || verb == "background" || verb == "camera") {
        Environment n = env;
        if (tk.size() < 2) return err("usage: " + verb + " key=value ...");
        for (size_t i = 1; i < tk.size(); ++i) {
            std::string k, v;
            if (!splitKV(tk[i], k, v)) return err("expected key=value, got '" + tk[i] + "'");
            bool good = false; float f;
            if (verb == "sun") {
                if (k == "dir") good = parseVec3(v, n.sunDir) && Vector3Length(n.sunDir) > 0;
                else if (k == "color") good = parseColor(v, n.sunColor);
                else if (k == "intensity") good = parseFloat(v, n.sunIntensity) && n.sunIntensity >= 0;
                else return err("unknown sun key '" + k + "' (dir color intensity)");
            } else if (verb == "ambient") {
                if (k == "sky") good = parseColor(v, n.sky);
                else if (k == "ground") good = parseColor(v, n.ground);
                else return err("unknown ambient key '" + k + "' (sky ground)");
            } else if (verb == "shadow") {
                if (k == "on") good = parseFloat(v, f) && ((n.shadows = f != 0), true);
                else if (k == "center") good = parseVec3(v, n.shadowCenter);
                else if (k == "radius") good = parseFloat(v, n.shadowRadius) && n.shadowRadius > 0;
                else return err("unknown shadow key '" + k + "' (on center radius)");
            } else if (verb == "background") {
                if (k == "color") good = parseColor(v, n.background);
                else return err("unknown background key '" + k + "' (color)");
            } else {
                if (k == "pos") good = parseVec3(v, n.camPos);
                else if (k == "target") good = parseVec3(v, n.camTarget);
                else if (k == "fov") good = parseFloat(v, n.camFov) && n.camFov > 1 && n.camFov < 170;
                else return err("unknown camera key '" + k + "' (pos target fov)");
            }
            if (!good) return err("bad value '" + v + "' for " + verb + " " + k);
        }
        env = n;
        return ok("set " + verb, true);
    }
    if (verb == "list") return ok(serialize());
    if (verb == "get") {
        if (!needArgs(tk, 2)) return err("usage: get <name>");
        const Entity* e = find(tk[1]);
        if (!e) return err("no entity named '" + tk[1] + "'");
        Scene tmp; tmp.entities.push_back(*e);
        std::string s = tmp.serialize();
        size_t p = s.find("\n" + std::string(kindName(e->kind)) + " ");
        return ok(p == std::string::npos ? s : s.substr(p + 1, s.find('\n', p + 1) - p - 1));
    }
    if (verb == "help" || verb == "?") return ok(helpText());
    return err("unknown command '" + tk[0] + "' (try: help)");
}

Result Scene::exec(const std::string& line) {
    auto tk = tokenize(line);
    if (tk.empty()) return ok("");
    const std::string verb = lower(tk[0]);
    if (verb == "undo" || verb == "redo") {
        auto& from = verb == "undo" ? undo_ : redo_;
        auto& to = verb == "undo" ? redo_ : undo_;
        if (from.empty()) return err("nothing to " + verb);
        to.push_back({entities, env});
        entities = std::move(from.back().entities); env = from.back().env; from.pop_back();
        ++rev_;
        return ok(verb + " done", true);
    }
    Snapshot before{entities, env};
    Result r = dispatch(tk);
    if (r.ok && r.changed) {
        ++rev_;
        if (!loading_) {
            undo_.push_back(std::move(before));
            if (undo_.size() > 256) undo_.erase(undo_.begin());
            redo_.clear();
        }
    }
    return r;
}

// ---------------------------------------------------------------- files
std::string Scene::serialize() const {
    std::string s = "# Foundry scene. One command per line; edit freely (see `help`).\n";
    s += "sun dir=" + v3(env.sunDir) + " color=" + col(env.sunColor) + " intensity=" + f2s(env.sunIntensity) + "\n";
    s += "ambient sky=" + col(env.sky) + " ground=" + col(env.ground) + "\n";
    s += std::string("shadow on=") + (env.shadows ? "1" : "0") + " center=" + v3(env.shadowCenter) + " radius=" + f2s(env.shadowRadius) + "\n";
    s += "background color=" + col(env.background) + "\n";
    s += "camera pos=" + v3(env.camPos) + " target=" + v3(env.camTarget) + " fov=" + f2s(env.camFov) + "\n";
    for (const Entity& e : entities) {
        s += std::string(kindName(e.kind)) + " " + e.name + " pos=" + v3(e.pos);
        if (e.kind != Kind::Light) s += " rot=" + v3(e.rot) + " scale=" + v3(e.scale);
        s += " color=" + col(e.color);
        if (e.kind == Kind::Light) s += " range=" + f2s(e.range) + " intensity=" + f2s(e.intensity);
        if (!e.visible) s += " visible=0";
        s += "\n";
    }
    return s;
}

bool Scene::save(const std::string& path, std::string& error) const {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) { error = "cannot write " + tmp; return false; }
        f << serialize();
        if (!f) { error = "write failed: " + tmp; return false; }
    }
    std::remove(path.c_str());   // Windows rename() will not overwrite
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { error = "cannot replace " + path; return false; }
    return true;
}

bool Scene::load(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + path; return false; }
    Scene fresh;
    fresh.loading_ = true;
    std::string line; int n = 0; std::string firstErr;
    while (std::getline(f, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Result r = fresh.exec(line);
        if (!r.ok && firstErr.empty()) firstErr = path + ":" + std::to_string(n) + ": " + r.message;
    }
    fresh.loading_ = false;
    if (!firstErr.empty()) { error = firstErr; return false; }
    entities = std::move(fresh.entities); env = fresh.env;
    undo_.clear(); redo_.clear(); ++rev_;
    return true;
}

// ---------------------------------------------------------------- geometry
Matrix Scene::modelMatrix(const Entity& e) {
    Matrix m = MatrixScale(e.scale.x, e.scale.y, e.scale.z);
    m = MatrixMultiply(m, MatrixRotateX(e.rot.x * 3.14159265f / 180.0f));
    m = MatrixMultiply(m, MatrixRotateY(e.rot.y * 3.14159265f / 180.0f));
    m = MatrixMultiply(m, MatrixRotateZ(e.rot.z * 3.14159265f / 180.0f));
    return MatrixMultiply(m, MatrixTranslate(e.pos.x, e.pos.y, e.pos.z));
}

bool Scene::raycast(const Entity& e, const Ray3& ray, float& tOut) {
    if (e.kind == Kind::Light) {   // pickable marker sphere
        Vector3 oc = Vector3Subtract(ray.origin, e.pos);
        float r = 0.35f, b = Vector3DotProduct(oc, ray.dir), c = Vector3DotProduct(oc, oc) - r * r, d = b * b - c;
        if (d < 0) return false;
        float t = -b - std::sqrt(d); if (t < 0) t = -b + std::sqrt(d);
        if (t < 0) return false;
        tOut = t; return true;
    }
    Matrix inv = MatrixInvert(modelMatrix(e));
    Vector3 o = Vector3Transform(ray.origin, inv);
    Vector3 d = Vector3Subtract(Vector3Transform(Vector3Add(ray.origin, ray.dir), inv), o);   // local, unnormalised: same t
    if (e.kind == Kind::Sphere) {
        float a = Vector3DotProduct(d, d), b = Vector3DotProduct(o, d), c = Vector3DotProduct(o, o) - 0.25f, disc = b * b - a * c;
        if (disc < 0 || a == 0) return false;
        float t = (-b - std::sqrt(disc)) / a; if (t < 0) t = (-b + std::sqrt(disc)) / a;
        if (t < 0) return false;
        tOut = t; return true;
    }
    float h = e.kind == Kind::Plane ? 0.01f : 0.5f;   // box slab test (cylinder/cone use their bounding box)
    float lo[3] = {-0.5f, -h, -0.5f}, hi[3] = {0.5f, h, 0.5f};
    float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    float t0 = 0, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-9f) { if (oo[i] < lo[i] || oo[i] > hi[i]) return false; continue; }
        float a = (lo[i] - oo[i]) / dd[i], b = (hi[i] - oo[i]) / dd[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    tOut = t0; return true;
}

int Scene::pick(const Ray3& ray, float& best) const {
    int hit = -1; best = 1e30f;
    for (size_t i = 0; i < entities.size(); ++i) {
        float t;
        if (entities[i].visible && raycast(entities[i], ray, t) && t < best) { best = t; hit = int(i); }
    }
    return hit;
}

const char* Scene::helpText() {
    return
"Commands (one per line; the scene file is a list of these):\n"
"  add <kind> [name] [key=val ...]   kinds: cube sphere cylinder cone plane light\n"
"  <kind> [name] [key=val ...]       shorthand for add\n"
"  set <name> key=val ...            keys: pos rot scale color visible name; lights: range intensity\n"
"  move <name> x,y,z                 add to position\n"
"  rotate <name> x,y,z               add degrees to rotation\n"
"  scale <name> n | x,y,z            multiply scale\n"
"  duplicate <name> [new] [offset=x,y,z]     delete <name>     rename <old> <new>     clear\n"
"  sun dir= color= intensity=        ambient sky= ground=      background color=\n"
"  shadow on=0|1 center= radius=     camera pos= target= fov=\n"
"  list | get <name> | undo | redo | help\n"
"Values: vectors x,y,z (one number = all three for scale); colors r,g,b[,a] or #rrggbb.\n"
"Transform order: scale, rotate X then Y then Z (degrees), translate.";
}

} // namespace foundry::scene
