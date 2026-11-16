// fscene: edit Foundry scene files from the command line (no GPU needed).
//   fscene <file.fscene> [-c "command"]... [--script cmds.txt] [--list] [--no-save] [--keep-going]
// Prints one line per command ("ok: ..." / "error: ..."); stops at the first error
// (nothing is saved) unless --keep-going. With no commands it prints the scene.
#include "scene/Scene.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace foundry::scene;

int main(int argc, char** argv) {
    if (argc < 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
        std::printf("usage: fscene <file.fscene> [-c \"command\"]... [--script file] [--list] [--no-save] [--keep-going]\n\n%s\n", Scene::helpText());
        return argc < 2 ? 2 : 0;
    }
    std::string path = argv[1];
    std::vector<std::string> cmds;
    bool list = false, save = true, keepGoing = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) cmds.push_back(argv[++i]);
        else if (a == "--script" && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            if (!f) { std::fprintf(stderr, "error: cannot open script %s\n", argv[i]); return 2; }
            std::string l; while (std::getline(f, l)) cmds.push_back(l);
        } else if (a == "--list") list = true;
        else if (a == "--no-save") save = false;
        else if (a == "--keep-going") keepGoing = true;
        else { std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str()); return 2; }
    }

    Scene scene;
    { size_t sl = path.find_last_of("/\\"); scene.setBaseDir(sl == std::string::npos ? "" : path.substr(0, sl + 1)); }
    {
        std::ifstream probe(path);
        if (probe) {
            std::string e;
            if (!scene.load(path, e)) { std::fprintf(stderr, "error: %s\n", e.c_str()); return 1; }
        } else if (cmds.empty()) {
            std::fprintf(stderr, "error: cannot open %s (give commands with -c to create it)\n", path.c_str());
            return 1;
        }
    }

    bool failed = false;
    uint64_t rev0 = scene.revision();
    for (const std::string& c : cmds) {
        Result r = scene.exec(c);
        if (c.find_first_not_of(" \t") == std::string::npos) continue;
        if (r.ok) { if (!r.message.empty()) std::printf("ok: %s\n", r.message.c_str()); }
        else { std::printf("error: %s\n", r.message.c_str()); failed = true; if (!keepGoing) break; }
    }
    if (failed && !keepGoing) { std::printf("not saved (a command failed)\n"); return 1; }

    if (save && scene.revision() != rev0) {
        std::string e;
        if (!scene.save(path, e)) { std::fprintf(stderr, "error: %s\n", e.c_str()); return 1; }
        std::printf("saved %s\n", path.c_str());
    }
    if (list || cmds.empty()) std::printf("%s", scene.serialize().c_str());
    return failed ? 1 : 0;
}
