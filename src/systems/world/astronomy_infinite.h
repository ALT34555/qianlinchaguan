#ifndef QLCG_ASTRONOMY_INFINITE_H
#define QLCG_ASTRONOMY_INFINITE_H
#include "astronomy_eclipses.h"
AstraEclipse qlcg_infinite_eclipse(uint32_t seed,double time);
void qlcg_infinite_astra(const WorldCreatorOptions *opt,const CalendarClock *clock,AstraFrame *frame);
#endif
