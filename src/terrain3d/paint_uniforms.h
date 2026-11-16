#ifndef QLCG_PAINT_UNIFORMS_H
#define QLCG_PAINT_UNIFORMS_H
typedef struct {
    float paper[3], sky[3], snow[3], snow_far[3], fog[2];
} TerrainPaintUniforms;
void qlcg_paint_uniforms(TerrainPaintUniforms *out,int palette,const float tint[3],float daylight,
    float wet,float haze,float snow,float dust,const float fog[2]);
#endif
