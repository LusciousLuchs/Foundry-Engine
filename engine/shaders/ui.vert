#version 450
layout(location = 0) in vec3 inPos;      // pixels (xy), origin top-left
layout(location = 3) in vec4 inColor;
layout(push_constant) uniform PC { vec2 screen; float flip; } pc;
layout(location = 0) out vec4 vColor;
void main() {
    vColor = inColor;
    vec2 ndc = inPos.xy / pc.screen * 2.0 - 1.0;
    ndc.y *= pc.flip;   // -1 in render-target passes (GL orientation)
    gl_Position = vec4(ndc, 0.0, 1.0);
}
