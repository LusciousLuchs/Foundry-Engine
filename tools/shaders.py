#!/usr/bin/env python3
"""Shader porting tool.

  tools/shaders.py extract <game-src-dir>   copy the game's embedded GLSL 330 shaders to shaders/gunsmith/
  tools/shaders.py strings                  write examples/gunsmith_shader_strings.hpp (demo/test helper)
  tools/shaders.py build                    translate shaders/*/*.glsl -> Vulkan GLSL 450, compile to SPIR-V
                                            (glslangValidator), write engine/render/PortedShaders.inc

The generated table is keyed by a hash of the ORIGINAL source strings, so the game's own
LoadShaderFromMemory(vs, fs) call finds its precompiled equivalent with no code change.
Translation rules (raylib GLSL 330 -> Vulkan GLSL 450):
  * loose `uniform` declarations -> one std140 block (set 0, binding 0); names stay global
  * sampler2D textureN          -> set 1, binding N
  * in/out varyings             -> explicit locations; vertex attributes use raylib's locations
  * gl_FragCoord                -> y flipped back to GL's bottom-left origin (via a `_screen` uniform)
"""
import os, re, subprocess, sys, tempfile, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ATTR_LOC = {"vertexPosition": 0, "vertexTexCoord": 1, "vertexNormal": 2, "vertexColor": 3,
            "vertexTangent": 4, "vertexTexCoord2": 5}
DEFAULT_VS = """#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
out vec2 fragTexCoord;
out vec4 fragColor;
uniform mat4 mvp;
void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
"""
TYPES = {  # name: (std140 size, align, enum)
    "float": (4, 4, 0), "vec2": (8, 8, 1), "vec3": (12, 16, 2), "vec4": (16, 16, 3),
    "int": (4, 4, 4), "ivec2": (8, 8, 5), "ivec3": (12, 16, 6), "ivec4": (16, 16, 7),
    "mat4": (64, 16, 9),
}
SAMPLER = 8

def fnv1a(b: bytes) -> int:
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c; h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

# ------------------------------------------------------------------ extract
def c_unescape(lit: str) -> str:
    return (lit.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\'))

def extract(src):
    out = os.path.join(ROOT, "shaders", "gunsmith")
    os.makedirs(out, exist_ok=True)
    found = {}
    for fn in ("app/scene.cpp", "app/gfx.cpp"):
        t = open(os.path.join(src, fn), errors="ignore").read()
        for name, body in re.findall(r'static const char\* (\w+) = R"\((.*?)\)";', t, re.S):
            found[name] = body
    pairs = {"bg": ("BG_VS", "BG_FS"), "grid": ("GRID_VS", "GRID_FS"), "lit": ("VS", "FS")}
    for n, (v, f) in pairs.items():
        open(os.path.join(out, n + ".vs.glsl"), "w", newline="\n").write(found[v])
        open(os.path.join(out, n + ".fs.glsl"), "w", newline="\n").write(found[f])
    # the scope eyepiece mask: adjacent C string literals passed to LoadShaderFromMemory(nullptr, ...)
    r = open(os.path.join(src, "app/range.cpp"), errors="ignore").read()
    m = re.search(r'LoadShaderFromMemory\(nullptr,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', r)
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    open(os.path.join(out, "round.fs.glsl"), "w", newline="\n").write("".join(c_unescape(x) for x in lits))
    print("extracted:", ", ".join(sorted(os.listdir(out))))

# ------------------------------------------------------------------ translate
UNI = re.compile(r'\buniform\s+(\w+)\s+(\w+)\s*(?:\[(\d+)\])?\s*;')
INOUT = re.compile(r'(?m)^[ \t]*\b(in|out)\s+(float|vec[234]|ivec[234]|int|mat4)\s+(\w+)\s*;')
INOUT_ANY = re.compile(r'\b(in|out)\s+(float|vec[234]|ivec[234]|int|mat4)\s+(\w+)\s*;')

def collect_uniforms(text, acc):
    for typ, name, n in UNI.findall(text):
        if typ == "sampler2D":
            continue
        if typ not in TYPES:
            raise SystemExit(f"unsupported uniform type {typ} {name}")
        count = int(n) if n else 1
        if name in acc and acc[name][:2] != (typ, count):
            raise SystemExit(f"uniform {name} declared with different types in vs/fs")
        acc.setdefault(name, (typ, count))

def layout_std140(uniforms):
    off, cur, table = 0, 0, []
    for name, (typ, count) in uniforms.items():
        size, align, enum = TYPES[typ]
        if count > 1 or typ == "mat4":
            align = 16
        cur = (cur + align - 1) // align * align
        stride = (size + 15) // 16 * 16 if count > 1 else size
        table.append((name, enum, count, cur))
        cur += stride * count if count > 1 else size
    return table, (cur + 15) // 16 * 16

def translate(vs, fs):
    uniforms = {}
    collect_uniforms(vs, uniforms); collect_uniforms(fs, uniforms)
    uniforms.setdefault("_screen", ("vec2", 1))          # engine-provided render size (gl_FragCoord fix)
    table, size = layout_std140(uniforms)
    block = "layout(set = 0, binding = 0, std140) uniform Params {\n" + "".join(
        f"    {t} {n}{'[%d]' % c if c > 1 else ''};\n" for n, (t, c) in uniforms.items()) + "};\n"
    samplers = sorted(set(re.findall(r'\buniform\s+sampler2D\s+(\w+)\s*;', vs + fs)))

    def finish(src, stage, vs_out):
        src = UNI.sub(lambda m: "" if True else m.group(0), src)            # drop loose uniforms (incl. samplers)
        src = src.replace("#version 330", "#version 450", 1)
        outs = {}
        def fix(m):
            kind, typ, name = m.groups()
            if stage == "vert":
                if kind == "in":
                    return f"layout(location = {ATTR_LOC[name]}) in {typ} {name};"
                loc = len(vs_out); vs_out[name] = loc
                return f"layout(location = {loc}) out {typ} {name};"
            if kind == "in":
                if name not in vs_out: raise SystemExit(f"fragment input {name} has no vertex output")
                return f"layout(location = {vs_out[name]}) in {typ} {name};"
            return f"layout(location = 0) out {typ} {name};"
        src = INOUT_ANY.sub(fix, src)
        head = block
        for s in samplers:
            b = int(re.search(r'(\d+)$', s).group(1))
            head += f"layout(set = 1, binding = {b}) uniform sampler2D {s};\n"
        if stage == "frag":
            head += ("vec4 foundry_FragCoord() { return vec4(gl_FragCoord.x, _screen.y - gl_FragCoord.y, gl_FragCoord.z, gl_FragCoord.w); }\n")
            src = re.sub(r'\bgl_FragCoord\b', 'foundry_FragCoord()', src)
        nl = src.index("\n", src.index("#version"))
        return src[:nl + 1] + head + src[nl + 1:]

    vs_out = {}
    return finish(vs, "vert", vs_out), finish(fs, "frag", vs_out), table, size

def compile_spv(src, stage, tmp):
    p = os.path.join(tmp, "s." + stage)
    open(p, "w").write(src)
    o = p + ".spv"
    r = subprocess.run(["glslangValidator", "-V", "--target-env", "vulkan1.3", "-S", stage, p, "-o", o],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"glslang failed ({stage}):\n{r.stdout}{r.stderr}\n--- source ---\n{src}")
    data = open(o, "rb").read()
    return struct.unpack("<%dI" % (len(data) // 4), data)

def build():
    entries = []
    base = os.path.join(ROOT, "shaders")
    for d in sorted(os.listdir(base)):
        dd = os.path.join(base, d)
        if not os.path.isdir(dd): continue
        names = sorted({f.split(".")[0] for f in os.listdir(dd) if f.endswith(".fs.glsl")})
        for n in names:
            fs = open(os.path.join(dd, n + ".fs.glsl"), newline="").read()
            vp = os.path.join(dd, n + ".vs.glsl")
            vs = open(vp, newline="").read() if os.path.exists(vp) else None
            entries.append((d + "_" + n, vs, fs))
    out = ["// Generated by tools/shaders.py from shaders/*/*.glsl. Do not edit.\n"]
    reg = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, vs, fs in entries:
            tvs, tfs, table, size = translate(vs if vs is not None else DEFAULT_VS, fs)
            vspv, fspv = compile_spv(tvs, "vert", tmp), compile_spv(tfs, "frag", tmp)
            for label, arr in (("vs", vspv), ("fs", fspv)):
                out.append(f"static const uint32_t k_{name}_{label}[] = {{" + ",".join(hex(x) for x in arr) + "};\n")
            out.append(f"static const ShaderUniformInfo k_{name}_uniforms[] = {{\n" + "".join(
                f'    {{"{n}", {e}, {c}, {o}}},\n' for n, e, c, o in table) + "};\n")
            hv = fnv1a((vs or "").encode()); hf = fnv1a(fs.encode())
            reg.append(f'    {{"{name}", 0x{hv:x}ull, 0x{hf:x}ull, k_{name}_vs, sizeof k_{name}_vs, k_{name}_fs, sizeof k_{name}_fs, '
                       f'k_{name}_uniforms, {len(table)}, {size}}},\n')
            print(f"{name}: block {size} bytes, {len(table)} uniforms")
    out.append("static const PortedShader kPortedShaders[] = {\n" + "".join(reg) + "};\n")
    open(os.path.join(ROOT, "engine/render/PortedShaders.inc"), "w").write("".join(out))

def strings():
    """examples/gunsmith_shader_strings.hpp: the game's shader sources as raw strings, byte-identical to what the
    game passes to LoadShaderFromMemory, so a demo can exercise the real hash-lookup path."""
    d = os.path.join(ROOT, "shaders", "gunsmith")
    out = ["// Generated by tools/shaders.py strings. Do not edit.\n#pragma once\n"]
    for n in sorted({f.split(".")[0] for f in os.listdir(d)}):
        for stage in ("vs", "fs"):
            p = os.path.join(d, f"{n}.{stage}.glsl")
            if os.path.exists(p):
                out.append(f'static const char* GS_{n.upper()}_{stage.upper()} = R"GLSL({open(p, newline="").read()})GLSL";\n')
    open(os.path.join(ROOT, "examples", "gunsmith_shader_strings.hpp"), "w", newline="\n").write("".join(out))

if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "strings": strings()
    elif len(sys.argv) >= 3 and sys.argv[1] == "extract": extract(sys.argv[2])
    elif len(sys.argv) >= 2 and sys.argv[1] == "build": build()
    else: print(__doc__); sys.exit(2)
