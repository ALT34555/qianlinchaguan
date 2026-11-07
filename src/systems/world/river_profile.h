#ifndef QLCG_RIVER_PROFILE_H
#define QLCG_RIVER_PROFILE_H

typedef struct { double height, lake; int sea; } RiverGround;
typedef RiverGround (*RiverGroundAt)(void *context, double arc);
typedef struct { int count, stations, samples; double arc; float *levels; } RiverProfile;

int qlcg_river_profile_build(RiverProfile *out, double arc, RiverGroundAt sample, void *context);
double qlcg_river_profile_level(const RiverProfile *profile, double arc);
void qlcg_river_profile_free(RiverProfile *profile);

#endif
