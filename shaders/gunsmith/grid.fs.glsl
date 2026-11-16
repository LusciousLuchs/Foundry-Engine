#version 330
in vec3 fragPos; out vec4 finalColor;
uniform int plane; uniform vec3 viewPos;
uniform vec4 cBase; uniform vec4 cFine; uniform vec4 cMinor; uniform vec4 cMajor;
uniform vec3 steps; uniform vec2 centre; uniform float radius;
float gridLine(vec2 p, float s, float w){
 vec2 c=p/s; vec2 fw=max(fwidth(c),vec2(1e-6));
 vec2 g=abs(fract(c-0.5)-0.5)/fw;
 float a=1.0-clamp(min(g.x,g.y)-(w-1.0)*0.5,0.0,1.0);
 float px=1.0/max(fw.x,fw.y);
 return a*smoothstep(4.0,12.0,px);
}
vec4 over(vec4 top, vec4 under){
 float a=top.a+under.a*(1.0-top.a);
 if(a<1e-5) return vec4(0.0);
 return vec4((top.rgb*top.a+under.rgb*under.a*(1.0-top.a))/a, a);
}
void main(){
 vec2 p = plane==0 ? fragPos.xy : fragPos.xz;
 vec3 nrm = plane==0 ? vec3(0.0,0.0,1.0) : vec3(0.0,1.0,0.0);
 float facing=dot(normalize(viewPos-fragPos),nrm);
 vec4 c=cBase;
 c=over(vec4(cFine.rgb, cFine.a*gridLine(p,steps.x,1.0)), c);
 c=over(vec4(cMinor.rgb, cMinor.a*gridLine(p,steps.y,1.0)), c);
 c=over(vec4(cMajor.rgb, cMajor.a*gridLine(p,steps.z,1.4)), c);
 float r=length(p-centre);
 c.a*=(1.0-smoothstep(radius*0.45,radius,r))*smoothstep(0.0,0.2,facing);
 finalColor=c; }