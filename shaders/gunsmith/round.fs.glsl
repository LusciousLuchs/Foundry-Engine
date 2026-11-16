#version 330
in vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor;
void main() { vec2 d = fragTexCoord - vec2(0.5); float r = length(d); if (r > 0.5) discard;
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor; c.rgb *= 1.0 - smoothstep(0.38, 0.5, r) * 0.45; finalColor = c; }
