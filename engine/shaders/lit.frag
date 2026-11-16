#version 450
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColor;
layout(location = 0) out vec4 outColor;

struct PointLight { vec4 posRange; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform Lights {
    vec4 sunDir;       // xyz: direction light travels
    vec4 sunColor;     // rgb * intensity
    vec4 ambSky;
    vec4 ambGround;
    vec4 counts;       // x: point light count
    PointLight pts[8];
} L;
layout(push_constant) uniform PC { mat4 viewProj; vec4 camPos; } pc;

vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

vec3 shade(vec3 albedo, vec3 n, vec3 v, vec3 l, vec3 radiance) {
    float ndl = max(dot(n, l), 0.0);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.3;
    return radiance * (albedo * ndl + vec3(spec * ndl));
}

void main() {
    // Zero normal = unlit (debug lines, gizmos): output the colour as authored.
    if (dot(vNormal, vNormal) < 1e-6) { outColor = vColor; return; }

    vec3 albedo = pow(vColor.rgb, vec3(2.2));
    vec3 v = normalize(pc.camPos.xyz - vWorld);
    vec3 n = normalize(vNormal);
    if (dot(n, v) < 0.0) n = -n;                       // two-sided

    vec3 col = mix(L.ambGround.rgb, L.ambSky.rgb, n.y * 0.5 + 0.5) * albedo;
    col += shade(albedo, n, v, normalize(-L.sunDir.xyz), L.sunColor.rgb);
    for (int i = 0; i < int(L.counts.x); ++i) {
        vec3 d = L.pts[i].posRange.xyz - vWorld;
        float dist = length(d);
        float r = L.pts[i].posRange.w;
        float w = clamp(1.0 - pow(dist / r, 4.0), 0.0, 1.0);
        float att = w * w / (dist * dist + 1.0);
        col += shade(albedo, n, v, d / max(dist, 1e-4), L.pts[i].colorIntensity.rgb * L.pts[i].colorIntensity.w * att);
    }
    outColor = vec4(pow(aces(col), vec3(1.0 / 2.2)), vColor.a);
}
