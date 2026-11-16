#include "scene/Scene.hpp"
#include <cmath>
#include <cstdio>

using namespace foundry::scene;
using foundry::compat::Vector3;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    Scene s;
    CHECK(s.exec("cube crate pos=0,1,0 scale=2 color=200,120,60").ok);
    CHECK(s.exec("add sphere ball pos=3,0.5,1").ok);
    CHECK(s.exec("light lamp pos=0,3,0 range=9 intensity=14 color=#ff5a3c").ok);
    CHECK(s.entities.size() == 3);
    CHECK(s.find("crate") && s.find("lamp") && s.find("ball"));
    if (failures) return 1;
    CHECK(s.find("crate")->scale.y == 2.0f);                 // single number fills all three
    CHECK(s.find("lamp")->color.r == 255 && s.find("lamp")->color.g == 0x5a);

    // errors leave the scene untouched and say what is wrong
    uint64_t rev = s.revision();
    Result r = s.exec("cube crate");
    CHECK(!r.ok && r.message.find("already exists") != std::string::npos);
    r = s.exec("set crate pos=1,2");
    CHECK(!r.ok && r.message.find("pos") != std::string::npos);
    r = s.exec("set crate bogus=1");
    CHECK(!r.ok && r.message.find("valid:") != std::string::npos);
    r = s.exec("move nothere 1,0,0");
    CHECK(!r.ok && r.message.find("no entity") != std::string::npos);
    CHECK(s.exec("frobnicate").ok == false);
    CHECK(s.revision() == rev);

    // relative edits
    CHECK(s.exec("move crate 1,0,0").ok);
    CHECK(s.find("crate")->pos.x == 1.0f);
    CHECK(s.exec("rotate crate 0,45,0").ok);
    CHECK(s.exec("scale crate 0.5").ok && s.find("crate")->scale.x == 1.0f);

    // undo / redo
    CHECK(s.exec("delete ball").ok && s.find("ball") == nullptr);
    CHECK(s.exec("undo").ok && s.find("ball") != nullptr);
    CHECK(s.exec("redo").ok && s.find("ball") == nullptr);
    CHECK(s.exec("undo").ok);
    CHECK(s.exec("duplicate crate copy offset=0,0,2").ok && s.find("copy")->pos.z == 2.0f);
    CHECK(s.exec("rename copy twin").ok && s.find("twin") && !s.find("copy"));
    CHECK(s.exec("# a comment line").ok && s.exec("cube c2 pos=0,0,0 # trailing comment").ok);
    CHECK(s.exec("delete c2").ok);
    CHECK(s.exec("sun dir=0,-1,0 intensity=3").ok && s.env.sunIntensity == 3.0f);
    CHECK(!s.exec("sun dir=0,0,0").ok);                     // zero direction rejected
    CHECK(s.exec("camera pos=1,2,3 target=0,0,0 fov=60").ok);

    // file round trip: serialize -> load -> serialize is stable, and the file is valid commands
    std::string text = s.serialize();
    { std::string se; CHECK(s.save("scene_test_tmp.fscene", se)); }
    Scene t; std::string e;
    CHECK(t.load("scene_test_tmp.fscene", e));
    CHECK(t.serialize() == text);
    CHECK(t.entities.size() == s.entities.size());
    CHECK(!t.canUndo());                                    // loading is not an undoable edit
    std::remove("scene_test_tmp.fscene");

    // a bad line reports file:line
    { FILE* f = std::fopen("scene_bad_tmp.fscene", "wb"); std::fputs("cube a\ncube a\n", f); std::fclose(f); }
    CHECK(!t.load("scene_bad_tmp.fscene", e) && e.find(":2:") != std::string::npos);
    std::remove("scene_bad_tmp.fscene");

    // picking
    Scene p;
    p.exec("cube near pos=0,0,0"); p.exec("sphere far pos=0,0,-5"); p.exec("cube rotated pos=4,0,0 rot=0,45,0 scale=2,1,0.2");
    float tt;
    int hit = p.pick({{0, 0, 10}, {0, 0, -1}}, tt);
    CHECK(hit >= 0 && p.entities[size_t(hit)].name == "near" && std::fabs(tt - 9.5f) < 1e-3f);   // nearest of the two in line
    CHECK(p.pick({{0, 0.9f, 10}, {0, 0, -1}}, tt) < 0);                                          // misses everything
    // the thin rotated slab is hit face-on, and missed where an unrotated slab would be hit
    CHECK(p.pick({{4, 0, 10}, {0, 0, -1}}, tt) == 2);
    p.exec("light lamp pos=0,5,0");
    hit = p.pick({{0, 5, 10}, {0, 0, -1}}, tt);
    CHECK(hit >= 0 && p.entities[size_t(hit)].name == "lamp");              // lights pickable
    p.exec("set near visible=0");
    hit = p.pick({{0, 0, 10}, {0, 0, -1}}, tt);
    CHECK(hit >= 0 && p.entities[size_t(hit)].name == "far");                                    // hidden entities are not picked

    {   // OBJ meshes: import, picking is exact (a hole in the middle is not hit), errors are clear
        Scene m; m.setBaseDir("models/");
        Result r = m.exec("mesh ring file=torus.obj pos=0,0,0");
        CHECK(r.ok);
        const ObjMesh* o = m.model("torus.obj");
        CHECK(o && o->triangleCount() == 32 * 16 * 2);
        CHECK(o && std::fabs(o->hi[0] - 0.95f) < 1e-3f && std::fabs(o->hi[1] - 0.25f) < 1e-3f);   // R + r, r
        float t;
        CHECK(m.pick({{0.7f, 5, 0}, {0, -1, 0}}, t) == 0 && std::fabs(t - 4.75f) < 1e-2f);        // through the tube: top at y=0.25
        CHECK(m.pick({{0, 5, 0}, {0, -1, 0}}, t) < 0);                                            // the hole: inside the box, no hit
        CHECK(m.exec("mesh lone file=pyramid.obj pos=5,0,0").ok);
        CHECK(m.model("pyramid.obj")->triangleCount() == 6);   // quad = 2 tris, 3 sides, 1 face via negative indices
        CHECK(!m.exec("mesh bad file=missing.obj").ok);
        CHECK(!m.exec("mesh nofile").ok);
        CHECK(!m.exec("cube c file=x.obj").ok);
        CHECK(m.serialize().find("mesh ring pos=0,0,0 file=torus.obj") != std::string::npos);
        std::string se;
        CHECK(m.save("models/_roundtrip_tmp.fscene", se));
        Scene back; CHECK(back.load("models/_roundtrip_tmp.fscene", se) && back.entities.size() == 2 && back.entities[0].file == "torus.obj");
        CHECK(back.pick({{0.7f, 5, 0}, {0, -1, 0}}, t) == 0);   // meshes resolve relative to the scene file
        std::remove("models/_roundtrip_tmp.fscene");
    }
    std::printf(failures ? "%d failure(s)\n" : "scene tests passed\n", failures);
    return failures;
}
