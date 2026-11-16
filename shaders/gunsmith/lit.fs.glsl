#version 330
in vec2 fragTexCoord; in vec3 fragNormal; in vec3 fragPos;
uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 viewPos; uniform float spec;
uniform vec3 lightDir; uniform vec3 lightCol; uniform vec3 fillDir; uniform vec3 fillCol;
uniform vec3 ambSky; uniform vec3 ambGround; uniform vec3 fogColor; uniform vec3 fogParam;
uniform int nLamps; uniform vec4 lampPos[16]; uniform vec4 lampCol[16]; uniform vec4 lampDir[16];
out vec4 finalColor;
void main(){ vec4 tex=texture(texture0,fragTexCoord); vec3 n=normalize(fragNormal);
 vec3 V=normalize(viewPos-fragPos);
 if(dot(n,V)<0.0) n=-n;
 vec3 L=normalize(lightDir); vec3 F=normalize(fillDir);
 float d=max(dot(n,L),0.0), fill=max(dot(n,F),0.0);
 float hemi=0.5+0.5*n.y; vec3 amb=mix(ambGround,ambSky,hemi);
 vec3 base=tex.rgb*colDiffuse.rgb;
 vec3 c=base*(amb*0.70+lightCol*d*0.58+fillCol*fill*0.16);
 vec3 H=normalize(L+V);
 float s=pow(max(dot(n,H),0.0),mix(24.0,64.0,spec))*spec*(0.25+0.75*d);
 float rim=pow(1.0-max(dot(n,V),0.0),3.0)*0.14;
 c+=lightCol*s+amb*rim;
 vec3 lampAcc=vec3(0.0); vec3 lampSpec=vec3(0.0);
 for(int i=0;i<nLamps;i++){   // the map's lamps, a gun's light, a muzzle flash: xyz + reach; colour x power; aim + cone
  vec3 Lv=lampPos[i].xyz-fragPos; float dl=length(Lv); float r=lampPos[i].w;
  if(dl>r) continue;
  vec3 l=Lv/max(dl,1e-4); float q=dl/r; float w=max(0.0,1.0-q*q*q*q);
  float att=w*w/(1.0+dl*dl);
  if(lampDir[i].w>-1.5){ float cs=dot(-l,lampDir[i].xyz); float inner=min(1.0,lampDir[i].w+0.08); att*=smoothstep(lampDir[i].w,inner,cs); }
  float ld=max(dot(n,l),0.0);
  vec3 hl=normalize(l+V); float ls=pow(max(dot(n,hl),0.0),mix(24.0,64.0,spec))*spec*ld;
  lampAcc+=lampCol[i].rgb*att*ld; lampSpec+=lampCol[i].rgb*att*ls;
 }
 c+=base*0.9*lampAcc/(1.0+0.45*lampAcc)+lampSpec/(1.0+0.6*lampSpec);   // (bright close up, without burning out to white)
 float dist=length(viewPos-fragPos); float f=clamp((dist-fogParam.x)/fogParam.y,0.0,fogParam.z); c=mix(c,fogColor,f);
 finalColor=vec4(c,tex.a*colDiffuse.a); }