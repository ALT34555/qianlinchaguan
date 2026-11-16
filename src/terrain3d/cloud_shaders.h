#ifndef QLCG_CLOUD_SHADERS_H
#define QLCG_CLOUD_SHADERS_H
#define QLCG_CLOUD_GLSL \
    "uniform sampler2D uCloudNoise,uCloudNear,uCloudFar;\n" \
    "uniform vec4 uCloudNearMap,uCloudFarMap,uCloudNoiseMap;\n" \
    "uniform float uCloudHeight,uCloudPulse;\n" \
    "uniform int uCloudLinear;\n" \
    "vec4 cloud_weather(sampler2D tex,vec2 uv){\n" \
    "  if(uCloudLinear!=0)return texture(tex,uv);\n" \
    "  vec2 p=uv*33.0-0.5;ivec2 i=ivec2(floor(p));vec2 f=fract(p);\n" \
    "  return mix(mix(texelFetch(tex,clamp(i,ivec2(0),ivec2(32)),0),texelFetch(tex,clamp(i+ivec2(1,0),ivec2(0),ivec2(32)),0),f.x),\n" \
    "    mix(texelFetch(tex,clamp(i+ivec2(0,1),ivec2(0),ivec2(32)),0),texelFetch(tex,clamp(i+ivec2(1,1),ivec2(0),ivec2(32)),0),f.x),f.y);\n" \
    "}\n" \
    "vec3 cloud_paint(vec3 rd,vec3 sky,int palette,float daylight,float flash,float special,float dusk){\n" \
    "  if(abs(rd.y)<0.0001)return sky;\n" \
    "  float travel=uCloudHeight/rd.y;if(travel<=0.0)return sky;\n" \
    "  vec2 offset=rd.xz*travel;float distance=length(offset);\n" \
    "  if(distance>=7400.0)return sky;\n" \
    "  float nearWeight=1.0-smoothstep(1200.0,1750.0,max(abs(offset.x),abs(offset.y)));\n" \
    "  vec4 weather;\n" \
    "  if(nearWeight>=1.0){\n" \
    "    vec2 nearUV=(uCloudNearMap.xy+offset*uCloudNearMap.z)*uCloudNearMap.w;\n" \
    "    weather=cloud_weather(uCloudNear,nearUV);\n" \
    "  }else{\n" \
    "    vec2 farUV=(uCloudFarMap.xy+offset*uCloudFarMap.z)*uCloudFarMap.w;\n" \
    "    weather=cloud_weather(uCloudFar,farUV);\n" \
    "    if(nearWeight>0.0){\n" \
    "      vec2 nearUV=(uCloudNearMap.xy+offset*uCloudNearMap.z)*uCloudNearMap.w;\n" \
    "      weather=mix(weather,cloud_weather(uCloudNear,nearUV),nearWeight);\n" \
    "    }\n" \
    "  }\n" \
    "  if(weather.x<=0.0)return sky;\n" \
    "  vec2 uv=uCloudNoiseMap.xy+offset*uCloudNoiseMap.zw;\n" \
    "  vec2 drift=weather.zw*uCloudNoiseMap.zw;\n" \
    "  float a=texture(uCloudNoise,uv-drift).r;\n" \
    "  float b=texture(uCloudNoise,uv*2.0-drift*1.37+vec2(0.37,0.61)).g;\n" \
    "  float field=clamp((a*0.65+b*0.35-0.5)*1.6+0.5,0.0,1.0);\n" \
    "  float pulse=sin(uCloudPulse+uv.x*6.2831853+sin(uv.y*6.2831853))*0.035;\n" \
    "  float cover=clamp(weather.x+pulse*(1.0-smoothstep(2.5,3.6,weather.y))*sin(weather.x*3.14159265),0.0,1.0);\n" \
    "  float density=smoothstep(mix(1.18,-0.18,cover)-0.13,mix(1.18,-0.18,cover)+0.13,field);\n" \
    "  float alpha=(1.0-exp(-density*weather.y*1.6))*(1.0-smoothstep(5800.0,7400.0,distance));\n" \
    "  float storm=smoothstep(1.6,3.7,weather.y);\n" \
    "  vec3 low=mix(vec3(0.77,0.80,0.81),vec3(0.10),storm);\n" \
    "  vec3 high=mix(vec3(1.0),vec3(0.24,0.22,0.25),storm);\n" \
    "  low=mix(low,vec3(0.29,0.15,0.40),special);\n" \
    "  high=mix(high,vec3(0.54,0.43,0.55),special);\n" \
    "  if(palette==2){low=vec3(mix(0.86,0.18,storm));high=vec3(mix(0.98,0.30,storm));}\n" \
    "  if(palette<0){\n" \
    "    vec3 base=mix(vec3(0.220,0.267,0.373),vec3(1.0),daylight);\n" \
    "    base=mix(base,vec3(1.0,0.698,0.467),dusk*0.55);\n" \
    "    low=mix(base*0.84,vec3(0.33,0.39,0.47)*mix(0.36,1.0,daylight),storm);\n" \
    "    high=mix(base,vec3(0.64,0.70,0.77)*mix(0.36,1.0,daylight),storm);\n" \
    "  }\n" \
    "  float shade=palette<0?clamp(field*0.50+storm*0.10,0.0,0.65):clamp(field*0.70+weather.y*0.09,0.0,1.0);\n" \
    "  vec3 color=mix(high,low,shade);\n" \
    "  if(palette>=0)color*=mix(0.30,1.0,daylight);\n" \
    "  color=mix(color,high,flash*0.5);\n" \
    "  return mix(sky,color,alpha*uCloudAmount);\n" \
    "}\n"
#endif
