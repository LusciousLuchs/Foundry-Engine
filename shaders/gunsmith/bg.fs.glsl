#version 330
uniform vec4 rect; uniform vec3 cInner; uniform vec3 cOuter; uniform float radial; uniform float grain;
out vec4 finalColor;
void main(){
 vec2 uv=(gl_FragCoord.xy-rect.xy)/rect.zw;
 float asp=rect.z/rect.w;
 vec2 d=(uv-vec2(0.5,0.56))*vec2(asp,1.0);
 float r=length(d)/length(vec2(asp,1.0)*0.5);
 float t=mix(1.0-uv.y, smoothstep(0.05,1.05,r), radial);
 vec3 c=mix(cInner,cOuter,t);
 float n=fract(sin(dot(floor(gl_FragCoord.xy),vec2(12.9898,78.233)))*43758.5453);
 c+=(n-0.5)*grain;
 finalColor=vec4(c,1.0); }