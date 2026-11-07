#ifndef QLCG_ASTRONOMY_EVENTS_H
#define QLCG_ASTRONOMY_EVENTS_H
#include "astronomy.h"
#include "astronomy_eclipses.h"
#include <stdio.h>
#define ASTRA_EVENT_VERSION 1u
#define ASTRA_METEORS 384
#define ASTRA_EVENT_VERTICES 16384
#define ASTRA_NOTICE_SKIPPED 1
#define ASTRA_NOTICE_ALL 2
#define ASTRA_MAX_STEP_MS 7200000.0
typedef struct { double peak, width; float rate, radiant[3]; } AstraShower;
typedef struct { double period, epoch, window; float a[3], b[3]; } AstraComet;
typedef struct {
    uint64_t id;
    double start;
    float flight, train, speed, path, origin[3], tangent[3], color[3];
    unsigned char kind, shower, decided, skipped;
} AstraMeteor;
typedef struct { int64_t hour; int count; AstraMeteor meteors[ASTRA_METEORS]; } AstraMeteorHour;
typedef struct AstraEventState {
    uint32_t seed, version;
    int shower_count, comet_count;
    AstraShower showers[6];
    AstraComet comets[4];
    AstraMeteorHour hours[2];
    double cursor, frame_from, frame_to, rendered_to, high_water;
    bool initialized,flat;
    bool broadcast_ready, broadcast_prograde, broadcast_flat;
    CalendarMode broadcast_mode;
    float broadcast_tilt;
    double broadcast_cursor, shower_start[6], comet_start[4], eclipse_refresh;
    AstraEclipse broadcast_eclipse;
} AstraEventState;
typedef enum { ASTRA_START_SHOWER, ASTRA_START_COMET, ASTRA_START_ECLIPSE } AstraStartKind;
typedef struct { AstraStartKind kind; double time; int index, eclipse_kind, eclipse_type; } AstraStart;
typedef void (*AstraStartNotice)(const AstraStart *event, void *context);
typedef void (*AstraNotice)(const AstraMeteor *meteor, void *context);
AstraEventState *qlcg_astra_events_create(uint32_t seed);
double qlcg_astra_flat_shower(const AstraEventState *state,int index,int64_t slot,double *half);
double qlcg_astra_flat_comet(const AstraEventState *state,int index,int64_t slot);
bool qlcg_astra_meteor_skip(double lifetime, double speed);
void qlcg_astra_events_advance(AstraEventState *state, double from, double to, double speed,
                              int policy, AstraNotice notice, void *context);
void qlcg_astra_starts_notify(AstraEventState *state,const WorldCreatorOptions *opt,
                              const CalendarClock *clock,double from,AstraStartNotice notice,void *context);
int qlcg_astra_event_vertices(AstraEventState *state, const CalendarClock *clock,
                              const AstraFrame *frame, AstraVertex *out, int capacity, int *comet_end);
bool qlcg_astra_events_write(FILE *file, const MacroWorld *world);
bool qlcg_astra_events_read(FILE *file, MacroWorld *world, long limit);
#endif
