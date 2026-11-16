#include "scene/Obj.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace foundry::scene {

bool loadObj(const std::string& path, ObjMesh& out, std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "cannot open " + path; return false; }
    std::vector<float> v, vn;
    out = ObjMesh{};
    std::string line;
    int lineNo = 0;
    auto idx = [](const std::string& s, size_t count, bool& ok) -> long {
        if (s.empty()) { ok = false; return 0; }
        long i = std::strtol(s.c_str(), nullptr, 10);
        if (i < 0) i += long(count); else i -= 1;
        if (i < 0 || size_t(i) >= count) ok = false;
        return i;
    };
    while (std::getline(f, line)) {
        ++lineNo;
        std::istringstream ss(line);
        std::string tag; ss >> tag;
        if (tag == "v") { float x, y, z; if (ss >> x >> y >> z) { v.insert(v.end(), {x, y, z}); } }
        else if (tag == "vn") { float x, y, z; if (ss >> x >> y >> z) { vn.insert(vn.end(), {x, y, z}); } }
        else if (tag == "f") {
            struct Corner { long p, n; };
            std::vector<Corner> poly;
            std::string tok;
            while (ss >> tok) {
                // forms: p, p/t, p//n, p/t/n
                size_t a = tok.find('/'), b = a == std::string::npos ? a : tok.find('/', a + 1);
                std::string ps = tok.substr(0, a), ns = b == std::string::npos ? "" : tok.substr(b + 1);
                bool ok = true;
                long p = idx(ps, v.size() / 3, ok), n = -1;
                if (!ns.empty()) { bool okn = true; n = idx(ns, vn.size() / 3, okn); if (!okn) n = -1; }
                if (!ok) { error = path + ":" + std::to_string(lineNo) + ": bad vertex index '" + tok + "'"; return false; }
                poly.push_back({p, n});
            }
            if (poly.size() < 3) { error = path + ":" + std::to_string(lineNo) + ": face with fewer than 3 vertices"; return false; }
            for (size_t i = 1; i + 1 < poly.size(); ++i) {
                const Corner c[3] = {poly[0], poly[i], poly[i + 1]};
                float p[3][3];
                for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) p[k][j] = v[size_t(c[k].p) * 3 + size_t(j)];
                float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
                float e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
                float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                float l = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
                if (l > 0) for (float& x : fn) x /= l;
                for (int k = 0; k < 3; ++k) {
                    out.positions.insert(out.positions.end(), {p[k][0], p[k][1], p[k][2]});
                    if (c[k].n >= 0) out.normals.insert(out.normals.end(), {vn[size_t(c[k].n) * 3], vn[size_t(c[k].n) * 3 + 1], vn[size_t(c[k].n) * 3 + 2]});
                    else out.normals.insert(out.normals.end(), {fn[0], fn[1], fn[2]});
                }
            }
        }
    }
    if (out.positions.empty()) { error = path + ": no faces found"; return false; }
    for (int j = 0; j < 3; ++j) { out.lo[j] = 1e30f; out.hi[j] = -1e30f; }
    for (size_t i = 0; i < out.positions.size(); i += 3)
        for (int j = 0; j < 3; ++j) { out.lo[j] = std::min(out.lo[j], out.positions[i + size_t(j)]); out.hi[j] = std::max(out.hi[j], out.positions[i + size_t(j)]); }
    return true;
}

} // namespace foundry::scene
