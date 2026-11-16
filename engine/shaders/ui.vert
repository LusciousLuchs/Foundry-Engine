#version 450
layout(location = 0) in vec2 inPos;      // pixels, origin top-left
layout(location = 1) in vec4 inColor;
layout(push_constant) uniform PC { vec2 screen; } pc;
layout(location = 0) out vec4 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPos / pc.screen * 2.0 - 1.0, 0.0, 1.0);
}
