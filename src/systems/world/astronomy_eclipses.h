#ifndef QLCG_ASTRONOMY_ECLIPSES_H
#define QLCG_ASTRONOMY_ECLIPSES_H
#include "astronomy.h"
#define ASTRA_CHART_W 512
#define ASTRA_CHART_H 256
typedef struct {
    double start, peak, end;
    int kind, type;
} AstraEclipse;
bool qlcg_astra_eclipse_find(const WorldCreatorOptions *opt,const CalendarClock *clock,AstraEclipse *out);
void qlcg_astra_eclipse_chart(const WorldCreatorOptions *opt,const CalendarClock *clock,
                             const AstraEclipse *event,unsigned char *rgba);
#endif
