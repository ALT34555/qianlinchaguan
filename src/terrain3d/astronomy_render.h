#ifndef QLCG_ASTRONOMY_RENDER_H
#define QLCG_ASTRONOMY_RENDER_H
#include "gl_loader.h"
#include "../systems/world/astronomy.h"
#include "sky_climate.h"
#include "../systems/world/astronomy_events.h"
typedef struct {
    AstraCatalog *catalog;
    GLuint texture, vao, vbo, moving_vao, moving_vbo, program;
    GLuint event_vao, event_vbo;
    AstraVertex *event_vertices;
    bool observing;
} AstraRenderer;
bool qlcg_astra_upload(AstraRenderer *r,const WorldCreatorOptions *opt);
void qlcg_astra_release(AstraRenderer *r);
void qlcg_astra_uniforms(GLuint program,const AstraFrame *f);
void qlcg_astra_draw_events(AstraRenderer *r,AstraEventState *state,const CalendarClock *clock,const AstraFrame *frame);
void qlcg_astra_draw(AstraRenderer *r,const AstraFrame *f,const SkyClimate *sky,
    const float *forward,const float *right,const float *up,float aspect,float tan_fov,float time,
    float cloud_cov,float rain_alpha);
#endif
