#version 450
// World-space vertices (CPU-transformed). Accepts raylib/OpenGL-convention
// matrices and converts clip space to Vulkan (y down, z in 0..1).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(push_constant) uniform PC { mat4 viewProj; vec4 camPos; } pc;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColor;
void main() {
    vWorld = inPos; vNormal = inNormal; vColor = inColor;
    vec4 c = pc.viewProj * vec4(inPos, 1.0);
    c.y = -c.y;
    c.z = (c.z + c.w) * 0.5;
    gl_Position = c;
}
