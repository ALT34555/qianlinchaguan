#ifndef QLCG_FRAME_WORKERS_H
#define QLCG_FRAME_WORKERS_H

enum { QLCG_FRAME_LANES=4 };

typedef struct QlcgFrameWorkers QlcgFrameWorkers;
typedef void (*QlcgFrameWork)(void *context,int lane);

QlcgFrameWorkers *qlcg_frame_workers_create(void);
void qlcg_frame_workers_run(QlcgFrameWorkers *workers,QlcgFrameWork work,void *context,int lanes);
void qlcg_frame_workers_free(QlcgFrameWorkers *workers);

#endif
