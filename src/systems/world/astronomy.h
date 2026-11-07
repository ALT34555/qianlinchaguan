#ifndef QLCG_ASTRONOMY_H
#define QLCG_ASTRONOMY_H
#include "planet_macro.h"
#define ASTRA_VERSION 1u
#define ASTRA_BODIES 15
#define ASTRA_STARS 12000
#define ASTRA_LINES 12000
typedef struct { float p[3], color[3], size; } AstraVertex;
typedef struct { float a[3], b[3], color[3], size; double phase, period; } AstraOrbit;
typedef struct {
    uint32_t seed, version;
    int star_count, line_count, constellation_count, nebula_count;
    AstraVertex stars[ASTRA_STARS], lines[ASTRA_LINES];
    AstraOrbit bodies[ASTRA_BODIES];
    float nebula[31][8];
} AstraCatalog;
typedef struct {
    float east[3], up[3], south[3];
    float sun[3], moon[3], sun_lon, sun_lat, phase;
    float sun_world[3], moon_world[3], shadow_dir[3];
    float sun_radius, moon_radius, solar_cover, lunar_cover, lunar_penumbra;
    float shadow_radius, penumbra_radius, moon_light;
    int solar_type, lunar_type;
    float occluder[3],occluder_radius;
    AstraVertex bodies[ASTRA_BODIES];
} AstraFrame;
double qlcg_astra_overlap(double radius, double cover_radius, double separation);
int qlcg_astra_solar_at(const float *sun, const float *moon, const float *observer, float *coverage);
void qlcg_astra_generate(AstraCatalog *out, uint32_t seed, uint32_t version);
void qlcg_astra_sample(const WorldCreatorOptions *opt, const CalendarClock *clock,
                       double longitude, double latitude, const AstraCatalog *catalog, AstraFrame *out);
void qlcg_astra_background(const AstraCatalog *catalog, unsigned char *rgb, int width, int height);
#endif
