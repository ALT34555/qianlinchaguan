#include "frame_workers.h"
#include <stdbool.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct {
    QlcgFrameWork work;
    void *context;
    int index;
#ifdef _WIN32
    HANDLE thread,ready,done;
#else
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    bool running;
#endif
} Worker;

struct QlcgFrameWorkers {
    Worker workers[QLCG_FRAME_LANES-1];
    int count;
};

#ifdef _WIN32
static DWORD WINAPI worker_loop(void *context) {
    Worker *worker=context;
    for(;;) {
        WaitForSingleObject(worker->ready,INFINITE);
        if(!worker->work) break;
        worker->work(worker->context,worker->index);
        SetEvent(worker->done);
    }
    return 0;
}

static bool worker_start(Worker *worker) {
    worker->ready=CreateEventW(NULL,FALSE,FALSE,NULL);
    worker->done=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(worker->ready&&worker->done) worker->thread=CreateThread(NULL,0,worker_loop,worker,0,NULL);
    if(worker->thread) return true;
    if(worker->ready) CloseHandle(worker->ready);
    if(worker->done) CloseHandle(worker->done);
    return false;
}

static void worker_submit(Worker *worker,QlcgFrameWork work,void *context) {
    worker->work=work;
    worker->context=context;
    SetEvent(worker->ready);
}

static void worker_join(Worker *worker) { WaitForSingleObject(worker->done,INFINITE); }

static void worker_stop(Worker *worker) {
    worker_submit(worker,NULL,NULL);
    WaitForSingleObject(worker->thread,INFINITE);
    CloseHandle(worker->thread);
    CloseHandle(worker->ready);
    CloseHandle(worker->done);
}
#else
static void *worker_loop(void *context) {
    Worker *worker=context;
    pthread_mutex_lock(&worker->lock);
    for(;;) {
        while(!worker->running) pthread_cond_wait(&worker->ready,&worker->lock);
        if(!worker->work) break;
        pthread_mutex_unlock(&worker->lock);
        worker->work(worker->context,worker->index);
        pthread_mutex_lock(&worker->lock);
        worker->running=false;
        pthread_cond_signal(&worker->ready);
    }
    pthread_mutex_unlock(&worker->lock);
    return NULL;
}

static bool worker_start(Worker *worker) {
    if(pthread_mutex_init(&worker->lock,NULL)) return false;
    if(!pthread_cond_init(&worker->ready,NULL)) {
        if(!pthread_create(&worker->thread,NULL,worker_loop,worker)) return true;
        pthread_cond_destroy(&worker->ready);
    }
    pthread_mutex_destroy(&worker->lock);
    return false;
}

static void worker_submit(Worker *worker,QlcgFrameWork work,void *context) {
    pthread_mutex_lock(&worker->lock);
    worker->work=work;
    worker->context=context;
    worker->running=true;
    pthread_cond_signal(&worker->ready);
    pthread_mutex_unlock(&worker->lock);
}

static void worker_join(Worker *worker) {
    pthread_mutex_lock(&worker->lock);
    while(worker->running) pthread_cond_wait(&worker->ready,&worker->lock);
    pthread_mutex_unlock(&worker->lock);
}

static void worker_stop(Worker *worker) {
    worker_submit(worker,NULL,NULL);
    pthread_join(worker->thread,NULL);
    pthread_cond_destroy(&worker->ready);
    pthread_mutex_destroy(&worker->lock);
}
#endif

QlcgFrameWorkers *qlcg_frame_workers_create(void) {
    QlcgFrameWorkers *pool=calloc(1,sizeof(*pool));
    if(!pool) return NULL;
    for(int i=0;i<QLCG_FRAME_LANES-1;i++) {
        pool->workers[i].index=i+1;
        if(!worker_start(&pool->workers[i])) break;
        pool->count++;
    }
    return pool;
}

/* 汇合后再修改世界 */
void qlcg_frame_workers_run(QlcgFrameWorkers *pool,QlcgFrameWork work,void *context,int lanes) {
    int parallel=pool?pool->count:0;
    if(parallel>=lanes) parallel=lanes-1;
    for(int i=0;i<parallel;i++) worker_submit(&pool->workers[i],work,context);
    work(context,0);
    for(int i=0;i<parallel;i++) worker_join(&pool->workers[i]);
    for(int i=parallel+1;i<lanes;i++) work(context,i);
}

void qlcg_frame_workers_free(QlcgFrameWorkers *pool) {
    if(!pool) return;
    for(int i=0;i<pool->count;i++) worker_stop(&pool->workers[i]);
    free(pool);
}
