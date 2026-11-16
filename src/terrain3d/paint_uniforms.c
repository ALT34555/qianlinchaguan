#include "paint_uniforms.h"
#include "core/math_util.h"

static float blend(float a,float b,float t) { return a*(1.0f-t)+b*t; }
static float smooth(float a,float b,float x) {
    float t=qlcg_clampf((x-a)/(b-a),0,1);
    return t*t*(3.0f-2.0f*t);
}
void qlcg_paint_uniforms(TerrainPaintUniforms *out,int palette,const float tint[3],float daylight,
    float wet,float haze,float snow,float dust,const float fog[2]) {
    const float ink[3]={207.0f/255.0f,204.0f/255.0f,201.0f/255.0f};
    const float silk[2][3]={{.902f,.870f,.775f},{.916f,.886f,.810f}};
    const float gray[3]={.695f,.682f,.664f},dust_ink[3]={.75f,.71f,.64f};
    const float cool[3]={.80f,.83f,.85f},sunset[3]={1.08f,.84f,.68f},dust_color[3]={.84f,.75f,.56f};
    const float snow_day[2][3]={{.964f,.970f,.982f},{.942f,.946f,.952f}},snow_night[3]={.72f,.76f,.84f};
    float dawn=qlcg_clampf((tint[0]-tint[2])*2.4f,0,.85f)*smooth(.12f,.78f,daylight);
    float overcast=qlcg_clampf(wet*.48f+haze*.36f+snow*.36f,0,1);
    float weather=qlcg_clampf(wet*.46f+haze*.36f+snow*.44f,0,.92f);
    float night=1.0f-smooth(.08f,.38f,daylight);
    for(int i=0;i<3;i++) {
        float paper=palette==2?ink[i]:silk[palette==1][i],sky=paper;
        if(palette==2) {
            float warm=blend(ink[i],gray[i],overcast*.52f);
            sky=blend(sky,warm,overcast);
            sky=blend(sky,blend(ink[i],dust_ink[i],.45f),qlcg_clampf(dust*.75f,0,.85f));
        } else {
            sky=blend(sky,blend(paper,cool[i],.62f),weather*.64f);
            sky=blend(sky,blend(paper,paper*sunset[i],.78f),dawn*(1.0f-weather*.65f));
            sky=blend(sky,blend(paper,dust_color[i],.50f),qlcg_clampf(dust*.78f,0,.85f));
        }
        out->paper[i]=paper;out->sky[i]=sky;
        out->snow[i]=blend(snow_day[palette==2][i],snow_night[i],night*.55f);
        out->snow_far[i]=blend(sky,out->snow[i],.82f);
    }
    float h=qlcg_clampf(haze*.88f,0,.92f);
    out->fog[0]=blend(fog[0],fog[0]*.34f,h);
    out->fog[1]=blend(fog[1],fog[1]*.44f,h);
}
