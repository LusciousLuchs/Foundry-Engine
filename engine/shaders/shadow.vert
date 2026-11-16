#version 450
// Depth-only pass from the sun's point of view.
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform PC { mat4 lightViewProj; } pc;
void main() {
    vec4 c = pc.lightViewProj * vec4(inPos, 1.0);
    c.y = -c.y;
    c.z = (c.z + c.w) * 0.5;
    gl_Position = c;
}
