/* 连续世界：圆形视距与区块生命周期 */
#include "surface_stream.h"
#include "surface_internal.h"
#include "surface_worker.h"
#include "world_civ_economy.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>

int qlcg_stream_slot(int cx, int cz) {
    int x = (cx % QLCG_STREAM_SIDE + QLCG_STREAM_SIDE) % QLCG_STREAM_SIDE;
    int z = (cz % QLCG_STREAM_SIDE + QLCG_STREAM_SIDE) % QLCG_STREAM_SIDE;
    return z * QLCG_STREAM_SIDE + x;
}

void qlcg_region_stream_view(RegionStream *st,float yaw,float half_fov) {
    float view_cos=cosf(half_fov);
    if(!st->view_ready||cosf(yaw-st->view_yaw)<.60f||fabsf(view_cos-st->view_cos)>.08f) {
        st->view_yaw=yaw;st->view_cos=view_cos;st->view_ready=true;st->priority_dirty=true;
    }
}
static int offset_cmp(const void *a, const void *b) {
    const RegionStreamOffset *p=a,*q=b;
    int d=p->distance2-q->distance2;
    if (d) return d;
    return p->z!=q->z?p->z-q->z:p->x-q->x;
}

static void prioritize_offsets(RegionStream *st) {
    int front=0,back=st->offset_count;
    float fx=sinf(st->view_yaw),fz=-cosf(st->view_yaw);
    float cos2=st->view_cos*st->view_cos;
    for(int i=0;i<st->offset_count;i++) {
        RegionStreamOffset p=st->offsets[i];
        float facing=p.x*fx+p.z*fz+1.5f;
        bool visible=st->view_cos>=0 ? facing>=0&&facing*facing>=p.distance2*cos2 :
            facing>=0||facing*facing<=p.distance2*cos2;
        if(!st->view_ready||p.distance2<=2||visible) st->order[front++]=i;
        else st->order[--back]=i;
    }
    for(int a=front,b=st->offset_count-1;a<b;a++,b--) {
        int swap=st->order[a];st->order[a]=st->order[b];st->order[b]=swap;
    }
    st->cursor=0;
    st->priority_dirty=false;
}

static void release_slot(RegionStream *st, int slot) {
    RegionChunkSlot *c = &st->slots[slot];
    if (!c->data) return;
    qlcg_region_free(c->data);
    c->data = NULL;
    c->refresh_surface=c->refresh_appearance=false;
    int last = st->active[--st->count];
    st->active[c->active_index] = last;
    st->slots[last].active_index = c->active_index;
    st->unloaded_total++;
    st->revision++;
}

static void install_chunk(RegionStream *st,int slot,RegionData *data) {
    RegionChunkSlot *c=&st->slots[slot];
    if(c->data&&c->data->center_cx==data->center_cx&&c->data->center_cz==data->center_cz) {
        qlcg_region_free(c->data);
    } else {
        release_slot(st,slot);
        c->active_index=st->count;
        st->active[st->count++]=slot;
        st->loaded_total++;
    }
    c->data=data;
    c->serial=++st->next_serial;
    c->appearance=qlcg_chunk_territory_appearance(data,st->world);
    c->refresh_surface=c->refresh_appearance=false;
    st->revision++;
}

static void refresh_chunk(RegionStream *st,RegionChunkSlot *slot) {
    RegionData *chunk=slot->data;
    if(slot->refresh_surface) qlcg_chunk_resample(chunk,st->world,st->season,&st->settings);
    else {
        qlcg_region_recolor(chunk,st->world,st->season);
        qlcg_region_scatter_props(chunk,st->world,st->season,&st->settings);
    }
    chunk->climate_season=st->season;
    slot->appearance=qlcg_chunk_territory_appearance(chunk,st->world);
    slot->refresh_surface=slot->refresh_appearance=false;
    st->revision++;
}

static RegionStreamTask *task_at(RegionStream *st,int x,int z) {
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        RegionStreamTask *task=&st->tasks[i];
        if(task->job&&task->x==x&&task->z==z) return task;
    }
    return NULL;
}

static void cancel_task(RegionStreamTask *task) {
    qlcg_chunk_build_cancel(task->job);task->job=NULL;
}

static bool ensure_chunk(RegionStream *st, int cx, int cz) {
    int slot = qlcg_stream_slot(cx, cz);
    RegionChunkSlot *c = &st->slots[slot];
    RegionStreamTask *task=task_at(st,cx,cz);
    if (c->data && c->data->center_cx == cx && c->data->center_cz == cz) {
        if(task) cancel_task(task);
        if(c->refresh_surface||c->refresh_appearance) refresh_chunk(st,c);
        return true;
    }
    RegionData *data=NULL;
    if(task&&qlcg_chunk_build_valid(task->job,st->world,st->season,&st->settings)) {
        while(!qlcg_chunk_build_step(task->job)) {}
        data=qlcg_chunk_build_take(task->job);task->job=NULL;task->serial=false;
    } else {
        if(task) cancel_task(task);
        data=qlcg_chunk_sample(st->world,cx,cz,st->season,&st->settings);
    }
    if (!data) return false;
    install_chunk(st,slot,data);
    return true;
}

RegionData *qlcg_region_chunk_at(const RegionData *reg, float lx, float lz,
                                float *chunk_lx, float *chunk_lz) {
    if (!reg) return NULL;
    RegionData *chunk = (RegionData *)reg;
    if (reg->stream) {
        int cx = reg->center_cx + (int)floorf((lx+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
        int cz = reg->center_cz + (int)floorf((lz+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
        chunk = reg->stream->slots[qlcg_stream_slot(cx, cz)].data;
        if (!chunk || chunk->center_cx != cx || chunk->center_cz != cz) return NULL;
    }
    if (chunk_lx) *chunk_lx = lx + (reg->center_cx-chunk->center_cx)*CHUNK_SIZE;
    if (chunk_lz) *chunk_lz = lz + (reg->center_cz-chunk->center_cz)*CHUNK_SIZE;
    return chunk;
}

static bool affects_chunk(const MacroWorld *w,const QlcgSurfaceBounds *bounds,const RegionData *c) {
    double half_x=(c->width-1)*c->step*.5,half_z=(c->height-1)*c->step*.5;
    return qlcg_surface_bounds_overlap(w,bounds,c->center_x-half_x,c->center_z-half_z,
        c->center_x+half_x,c->center_z+half_z);
}

static void collect_changes(RegionStream *st,int season,const GameSettings *s) {
    const MacroWorld *w=st->world;
    if(st->delta_revision!=w->delta_version) qlcg_surface_deltas_prepare((MacroWorld *)w);
    QlcgSurfaceBounds changes[QLCG_SURFACE_DIRTY_CAP];
    int count=qlcg_world_surface_changes(w,st->delta_revision,st->surface_revision,changes);
    uint32_t farms=qlcg_civ_farms_revision(w);
    bool territory=st->territory_revision!=w->territory_revision||st->farm_revision!=farms||
        st->artificial_enabled!=w->opt.artificial_enabled;
    for(int i=0;i<count;i++) territory|=changes[i].appearance_only;
    bool appearance=season!=st->season||s->dense_vegetation!=st->settings.dense_vegetation||
        s->vegetation_form!=st->settings.vegetation_form;
    if(count||territory||appearance) st->cursor=0;
    for(int i=0;(count||territory||appearance)&&i<st->count;i++) {
        RegionChunkSlot *slot=&st->slots[st->active[i]];
        slot->refresh_surface|=count<0;
        for(int j=0;j<count&&!slot->refresh_surface;j++)
            slot->refresh_surface=affects_chunk(w,&changes[j],slot->data);
        slot->refresh_appearance|=appearance;
        if(territory&&!slot->refresh_surface&&!slot->refresh_appearance)
            slot->refresh_appearance=slot->appearance!=qlcg_chunk_territory_appearance(slot->data,w);
    }
    st->settings=*s;st->season=season;
    st->artificial_enabled=w->opt.artificial_enabled;
    st->delta_revision=w->delta_version;st->surface_revision=w->surface_revision;
    st->territory_revision=w->territory_revision;st->farm_revision=farms;
}

static bool slot_needs_update(const RegionChunkSlot *slot,int x,int z) {
    return !slot->data||slot->data->center_cx!=x||slot->data->center_cz!=z||
        slot->refresh_surface||slot->refresh_appearance;
}

static void advance_cursor(RegionStream *st) {
    while(st->cursor<st->offset_count) {
        RegionStreamOffset p=st->offsets[st->order[st->cursor]];
        int x=st->center_cx+p.x,z=st->center_cz+p.z;
        if(slot_needs_update(&st->slots[qlcg_stream_slot(x,z)],x,z)) break;
        st->cursor++;
    }
}

typedef struct {
    RegionStream *stream;
    RegionStreamTask *tasks[QLCG_FRAME_LANES];
    double deadline;
} SurfaceBatch;

static void build_parallel(void *context,int lane) {
    SurfaceBatch *batch=context;
    RegionStreamTask *task=batch->tasks[lane];
    const MacroWorld *w=qlcg_surface_worker_begin(&task->sampler,batch->stream->world);
    if(w) {
        qlcg_chunk_build_surface(task->job,w,batch->deadline);
        if(qlcg_surface_worker_end(&task->sampler)) return;
    }
    cancel_task(task);
    task->serial=true;
}

static void enqueue_tasks(RegionStream *st,double deadline) {
    int cursor=st->cursor;
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        RegionStreamTask *task=&st->tasks[i];
        while(!task->job&&cursor<st->offset_count) {
            RegionStreamOffset p=st->offsets[st->order[cursor++]];
            int x=st->center_cx+p.x,z=st->center_cz+p.z;
            RegionChunkSlot *slot=&st->slots[qlcg_stream_slot(x,z)];
            if(!slot_needs_update(slot,x,z)||task_at(st,x,z)) continue;
            if(qlcg_time_ms()>=deadline) return;
            if(slot->data&&slot->data->center_cx==x&&slot->data->center_cz==z&&!slot->refresh_surface) {
                refresh_chunk(st,slot);
                continue;
            }
            task->serial=(task->serial&&task->x==x&&task->z==z)||(qlcg_world_flat(st->world)&&st->world->planes);
            task->x=x;task->z=z;
            task->job=qlcg_chunk_build_begin(st->world,x,z,st->season,&st->settings);
            if(!task->job) return;
        }
    }
}

static int step_tasks(RegionStream *st,double deadline) {
    SurfaceBatch batch={.stream=st,.deadline=deadline};
    int lanes=0,built=0;
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        RegionStreamTask *task=&st->tasks[i];
        if(!task->job) continue;
        if(!task->serial&&qlcg_chunk_build_needs_surface(task->job)) {
            batch.tasks[lanes++]=task;
            continue;
        }
        double now=qlcg_time_ms();
        double share=fmin(deadline,now+.35);
        while(now<share) {
            if(qlcg_chunk_build_step(task->job)) {
                RegionData *data=qlcg_chunk_build_take(task->job);
                task->job=NULL;task->serial=false;
                install_chunk(st,qlcg_stream_slot(task->x,task->z),data);
                built++;
                break;
            }
            now=qlcg_time_ms();
        }
    }
    if(lanes&&qlcg_time_ms()<deadline) {
        if(!st->workers&&lanes>1) st->workers=qlcg_frame_workers_create();
        qlcg_frame_workers_run(st->workers,build_parallel,&batch,lanes);
    }
    return built;
}

void qlcg_region_stream_refresh(RegionData *reg,float wx0,float wz0,float wx1,float wz1,
                                int season,const GameSettings *s) {
    if(!reg||!reg->stream) return;
    RegionStream *st=reg->stream;
    collect_changes(st,season,s);
    QlcgSurfaceBounds bounds={wx0,wz0,wx1,wz1,false};
    for (int i = 0; i < st->count; i++) {
        RegionChunkSlot *slot=&st->slots[st->active[i]];
        if(!affects_chunk(st->world,&bounds,slot->data)) continue;
        slot->refresh_surface=true;
    }
    st->cursor=0;
    qlcg_region_stream_sync_changed(reg,season,s);
}

void qlcg_region_stream_sync_changed(RegionData *reg,int season,const GameSettings *s) {
    if(!reg||!reg->stream) return;
    RegionStream *st=reg->stream;
    collect_changes(st,season,s);
    for(int i=0,refreshed=0;i<st->offset_count&&refreshed<9;i++) {
        RegionStreamOffset p=st->offsets[i];
        int x=st->center_cx+p.x,z=st->center_cz+p.z;
        RegionChunkSlot *slot=&st->slots[qlcg_stream_slot(x,z)];
        if(!slot->data||slot->data->center_cx!=x||slot->data->center_cz!=z) continue;
        if(slot->refresh_surface||slot->refresh_appearance) { refresh_chunk(st,slot);refreshed++; }
    }
}

void qlcg_region_stream_update(RegionData *reg, float lx, float lz,
                               int season, const GameSettings *s, double budget_ms) {
    if (!reg || !reg->stream) return;
    double t0 = qlcg_time_ms();
    RegionStream *st = reg->stream;
    int cx = reg->center_cx + (int)floorf((lx+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
    int cz = reg->center_cz + (int)floorf((lz+CHUNK_SIZE*0.5f)/CHUNK_SIZE);
    int rd = qlcg_clampi(s->render_distance, MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE);
    bool radius_changed = rd != st->radius;
    bool moved = cx != st->center_cx || cz != st->center_cz;
    if (radius_changed) {
        st->radius = rd;
        st->offset_count = 0;
        for (int z = -rd; z <= rd; z++) for (int x = -rd; x <= rd; x++) {
            if (x*x + z*z > (rd + 0.5f)*(rd + 0.5f)) continue;
            st->offsets[st->offset_count++]=(RegionStreamOffset){x,z,x*x+z*z};
        }
        qsort(st->offsets,st->offset_count,sizeof(st->offsets[0]),offset_cmp);
    }
    if(radius_changed||st->priority_dirty) prioritize_offsets(st);
    if (radius_changed || moved) {
        st->center_cx = cx;
        st->center_cz = cz;
        st->cursor = 0;
        float limit = rd + (radius_changed ? 0.5f : 1.5f);
        for (int i = st->count - 1; i >= 0; i--) {
            RegionData *c = st->slots[st->active[i]].data;
            float dx = (float)c->center_cx - cx, dz = (float)c->center_cz - cz;
            if (dx*dx + dz*dz > limit*limit) release_slot(st, st->active[i]);
        }
        st->revision++;
    }
    collect_changes(st,season,s);
    ensure_chunk(st,cx,cz);
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        RegionStreamTask *task=&st->tasks[i];
        if(!task->job) continue;
        double dx=(double)task->x-cx,dz=(double)task->z-cz;
        if(!qlcg_chunk_build_valid(task->job,st->world,season,s)||dx*dx+dz*dz>(rd+.5)*(rd+.5)) {
            cancel_task(task);task->serial=false;
        }
    }
    int built = 0;
    double now=qlcg_time_ms(),deadline=t0+budget_ms;
    advance_cursor(st);
    while(st->cursor<st->offset_count&&built<8&&now<deadline) {
        enqueue_tasks(st,deadline);
        built+=step_tasks(st,deadline);
        advance_cursor(st);
        now=qlcg_time_ms();
    }
    RegionData *here = st->slots[qlcg_stream_slot(cx, cz)].data;
    if (here) {
        reg->water_form = here->water_form;
        reg->water_freeze = here->water_freeze;
        reg->site_cell = here->site_cell;
        reg->site_chunk_type = here->site_chunk_type;
        reg->site_natural_chunk_type = here->site_natural_chunk_type;
        reg->site_height = here->site_height;
    }
    if(budget_ms>0) st->overrun_ms=fmax(st->overrun_ms,fmax(0,now-deadline));
    st->update_ms = qlcg_time_ms() - t0;
}

RegionData *qlcg_region_sample(const MacroWorld *w, int cx, int cz,
                               int season, const GameSettings *s) {
    RegionData *reg = calloc(1, sizeof(*reg));
    RegionStream *st = calloc(1, sizeof(*st));
    if (!reg || !st) { free(reg); free(st); return NULL; }
    reg->stream = st;
    reg->center_cx = cx; reg->center_cz = cz;
    reg->center_x = (cx + 0.5f) * CHUNK_SIZE;
    reg->center_z = (cz + 0.5f) * CHUNK_SIZE;
    st->world = w;
    st->settings = *s; st->season = season;
    st->center_cx = cx; st->center_cz = cz;
    st->delta_revision = w->delta_version;
    st->surface_revision = w->surface_revision;
    st->territory_revision = w->territory_revision;
    st->farm_revision = qlcg_civ_farms_revision(w);
    st->artificial_enabled=w->opt.artificial_enabled;
    qlcg_surface_deltas_prepare((MacroWorld *)w);
    qlcg_region_stream_update(reg, 0, 0, season, s, 2.0);
    if (!qlcg_region_chunk_at(reg, 0, 0, NULL, NULL)) { qlcg_region_free(reg); return NULL; }
    reg->sample_ms = st->update_ms;
    return reg;
}

void qlcg_region_stream_free(RegionStream *st) {
    if (!st) return;
    qlcg_frame_workers_free(st->workers);
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        cancel_task(&st->tasks[i]);
        qlcg_surface_worker_free(st->tasks[i].sampler);
    }
    while (st->count > 0) release_slot(st, st->active[st->count-1]);
    free(st);
}

void qlcg_region_stream_require(RegionData *reg,float x,float z) {
    if(!reg||!reg->stream)return;
    int cx=reg->center_cx+(int)floorf((x+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    int cz=reg->center_cz+(int)floorf((z+CHUNK_SIZE*.5f)/CHUNK_SIZE);
    RegionStream *st=reg->stream;
    collect_changes(st,st->season,&st->settings);
    ensure_chunk(st,cx,cz);
}
