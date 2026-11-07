/* 人工河线按格分段保存 */
typedef struct { int cell; SourceRiverNetwork *network; } CraftStage;
typedef struct { int cell,count,capacity; RiverVectorPoint *points; } CraftPiece;
static SourceRiverNetwork *craft_stage(const MacroWorld *w,int cell,CraftStage *stages,int *count) {
    for(int k=0;k<*count;k++) if(stages[k].cell==cell) return stages[k].network;
    if(*count>=SOURCE_POINT_LIMIT*2) return NULL;
    const SourceRiverNetwork *old=qlcg_source_network(w,cell,1); SourceRiverNetwork empty={0};
    SourceRiverNetwork *n=calloc(1,sizeof(*n)); if(!n) return NULL;
    n->cell=cell; n->frame=qlcg_surface_frame(w,cell); RiverBuffer copy={0};
    int ok=network_record(old?(SourceRiverNetwork *)old:&empty,&copy);
    RiverBuffer read={copy.data,copy.size,0,0,1,0}; if(ok) ok=network_record(n,&read);
    qlcg_river_buffer_free(&copy); if(!ok) { network_free(n); return NULL; }
    stages[*count]=(CraftStage){cell,n}; (*count)++; return n;
}
static const SourceRiverEdge *craft_edge(const SourceRiverNetwork *n,int source) {
    if(n) for(int k=0;k<n->edge_count;k++) if(n->edges[k].source==source) return n->edges+k;
    return NULL;
}
static const SourceRiverEdge *craft_head(const MacroWorld *w,const SourceRiverEdge *edge) {
    if(!edge||!edge->craft_id) return NULL;
    const SourceRiverNetwork *n=qlcg_source_network(w,edge->craft_owner,0);
    if(n) for(int k=0;k<n->edge_count;k++) if(n->edges[k].craft_id==edge->craft_id&&n->edges[k].craft_order==0) return n->edges+k;
    return NULL;
}
int qlcg_river_craft_read(const MacroWorld *w,const SourceRiverNetwork *n,int source,RiverEdit *edit) {
    const SourceRiverEdge *head=craft_head(w,craft_edge(n,source)); if(!head) return 0;
    edit->cell=head->craft_owner; edit->source=head->source; edit->kind=RIVER_RECORD_SOURCE; edit->count=0;
    int total=0;
    for(int j=0;j<head->craft_count;j++) {
        const SourceRiverNetwork *part=qlcg_source_network(w,head->craft_cells[j],0); if(!part) return 0;
        for(int k=0;k<part->edge_count;k++) if(part->edges[k].craft_id==head->craft_id&&part->edges[k].craft_order==j) total+=part->edges[k].count-1;
    }
    int stride=(int)ceil((total+1)/(double)(SOURCE_POINT_LIMIT-1)),seen=0; if(stride<1) stride=1;
    for(int j=0;j<head->craft_count;j++) {
        const SourceRiverNetwork *part=qlcg_source_network(w,head->craft_cells[j],0);
        for(int k=0;k<part->edge_count;k++) {
            const SourceRiverEdge *e=part->edges+k;
            if(e->craft_id!=head->craft_id||e->craft_order!=j) continue;
            for(int v=j?1:0;v<e->count;v++,seen++) {
                if(seen%stride&&!(j==head->craft_count-1&&v==e->count-1)) continue;
                const RiverVectorPoint *p=e->points+v;
                if(edit->count<SOURCE_POINT_LIMIT) edit->nodes[edit->count++]=(RiverPathPoint){p->wx,p->wz,p->arc,p->level,p->depth,p->width};
            }
        }
    }
    return edit->count>0;
}
static void craft_remove_source(SourceRiverNetwork *n,int id,int replacing) {
    for(int k=0;k<n->edge_count;k++) {
        SourceRiverEdge *e=n->edges+k;
        if(e->source==id) {
            qlcg_river_index_free(&e->spatial); free(e->points); free(e->craft_cells);
            memmove(e,e+1,(size_t)(n->edge_count-k-1)*sizeof(*e)); n->edge_count--; k--; continue;
        }
        if(e->target_kind==1&&e->target==id) { e->target_kind=SOURCE_TARGET_POOL; e->target=replacing?-1:n->cell; e->join=(RiverJunction){0}; }
        else if(e->target_kind==1&&e->target>id) e->target--;
        if(e->source>id) e->source--;
    }
    memmove(n->sources+id,n->sources+id+1,(size_t)(n->source_count-id-1)*sizeof(*n->sources)); n->source_count--;
}
static int craft_push(CraftPiece *piece,RiverVectorPoint point) {
    if(piece->count&&hypot(point.wx-piece->points[piece->count-1].wx,point.wz-piece->points[piece->count-1].wz)<.001) return 1;
    if(piece->count==piece->capacity) {
        int capacity=piece->capacity?piece->capacity*2:16;
        RiverVectorPoint *p=realloc(piece->points,(size_t)capacity*sizeof(*p)); if(!p) return 0;
        piece->points=p; piece->capacity=capacity;
    }
    piece->points[piece->count++]=point; return 1;
}
static RiverVectorPoint craft_between(const SurfaceFrame *frame,RiverVectorPoint a,RiverVectorPoint b,double t) {
    RiverVectorPoint p=a; p.x=mix(a.x,b.x,t); p.z=mix(a.z,b.z,t); p.level=mix(a.level,b.level,t);
    p.width=mix(a.width,b.width,t); p.depth=mix(a.depth,b.depth,t);
    SurfacePoint q=qlcg_frame_coordinates(frame,p.x,p.z); p.wx=q.x; p.wz=q.z; return p;
}
static int craft_pieces(const MacroWorld *w,const RiverEdit *edit,CraftPiece *pieces,int *count) {
    SurfaceFrame frame=qlcg_surface_frame(w,edit->cell); double total=0;
    for(int k=1;k<edit->count;k++) {
        SurfacePoint a=qlcg_frame_local(&frame,edit->nodes[k-1].wx,edit->nodes[k-1].wz),b=qlcg_frame_local(&frame,edit->nodes[k].wx,edit->nodes[k].wz);
        total+=hypot(b.x-a.x,b.z-a.z);
    }
    double spacing=fmin(w->circumference/w->columns*.2,fmax(32,total/768));
    RiverVectorPoint previous={0}; *count=0;
    for(int k=0;k<edit->count;k++) {
        RiverPathPoint v=edit->nodes[k]; SurfacePoint q=qlcg_frame_local(&frame,v.wx,v.wz);
        RiverVectorPoint end={v.wx,v.wz,q.x,q.z,0,fmin(16,fmax(2.8,v.half_width)),0,v.level,fmin(2.5,v.depth)};
        int steps=k?(int)fmax(1,ceil(hypot(end.x-previous.x,end.z-previous.z)/fmax(1,spacing))):1;
        RiverVectorPoint start=previous;
        for(int j=1;j<=steps;j++) {
            RiverVectorPoint p=k?craft_between(&frame,start,end,j/(double)steps):end;
            int cell=qlcg_world_cell_at_wxz(w,(float)p.wx,(float)p.wz);
            if(!*count) { pieces[0].cell=cell; *count=1; }
            CraftPiece *piece=pieces+*count-1;
            if(cell!=piece->cell) {
                if(*count>=SOURCE_POINT_LIMIT*2) return 0;
                double lo=0,hi=1;
                for(int it=0;it<32;it++) {
                    double t=(lo+hi)*.5; RiverVectorPoint mid=craft_between(&frame,previous,p,t);
                    if(qlcg_world_cell_at_wxz(w,(float)mid.wx,(float)mid.wz)==piece->cell) lo=t; else hi=t;
                }
                RiverVectorPoint border=craft_between(&frame,previous,p,(lo+hi)*.5);
                if(!craft_push(piece,border)) return 0;
                piece=pieces+(*count)++; piece->cell=cell; if(!craft_push(piece,border)) return 0;
            }
            if(!craft_push(piece,p)) return 0;
            previous=p;
        }
    }
    return 1;
}
int qlcg_river_craft_source(MacroWorld *w,const RiverEdit *edit,int remove) {
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<1) return RIVER_EDIT_INVALID;
    CraftStage *stages=calloc(SOURCE_POINT_LIMIT*2,sizeof(*stages));
    CraftPiece *pieces=calloc(SOURCE_POINT_LIMIT*2,sizeof(*pieces));
    RiverArchiveUpdate *updates=calloc(SOURCE_POINT_LIMIT*2,sizeof(*updates));
    if(!stages||!pieces||!updates) { free(stages); free(pieces); free(updates); return RIVER_EDIT_MEMORY; }
    int status=RIVER_EDIT_OK,stage_count=0,piece_count=0;
    uint64_t id=qlcg_river_archive_next_id(w); if(!id) status=RIVER_EDIT_MEMORY;
    const SourceRiverNetwork *old=qlcg_source_network(w,edit->cell,1);
    if(edit->source>=0) {
        if(!old||edit->source>=old->source_count) status=RIVER_EDIT_INVALID;
        const SourceRiverEdge *head=craft_head(w,craft_edge(old,edit->source));
        if(head) {
            id=head->craft_id;
            for(int j=0;!status&&j<head->craft_count;j++) {
                SourceRiverNetwork *n=craft_stage(w,head->craft_cells[j],stages,&stage_count);
                if(!n) { status=RIVER_EDIT_MEMORY; break; }
                for(int k=n->edge_count-1;k>=0;k--) if(n->edges[k].craft_id==id) craft_remove_source(n,n->edges[k].source,!remove);
            }
        } else if(!status) {
            SourceRiverNetwork *n=craft_stage(w,edit->cell,stages,&stage_count);
            if(!n) status=RIVER_EDIT_MEMORY; else craft_remove_source(n,edit->source,!remove);
        }
    } else if(remove) status=RIVER_EDIT_INVALID;
    if(!status&&!remove&&!craft_pieces(w,edit,pieces,&piece_count)) status=RIVER_EDIT_MEMORY;
    int owner=piece_count?pieces[0].cell:edit->cell,head_source=-1,finished=0;
    double previous_level=INFINITY;
    for(int j=0;!status&&j<piece_count&&!finished;j++) {
        CraftPiece *piece=pieces+j; SourceRiverNetwork *n=craft_stage(w,piece->cell,stages,&stage_count);
        if(!n) { status=RIVER_EDIT_MEMORY; break; }
        RiverSource *sources=realloc(n->sources,(size_t)(n->source_count+1)*sizeof(*sources));
        if(!sources) { status=RIVER_EDIT_MEMORY; break; } n->sources=sources;
        SourceRiverEdge *edges=realloc(n->edges,(size_t)(n->edge_count+1)*sizeof(*edges));
        if(!edges) { status=RIVER_EDIT_MEMORY; break; } n->edges=edges;
        int source=n->source_count++; RiverSource *s=n->sources+source; memset(s,0,sizeof(*s));
        s->seed=(uint32_t)id; s->spring_form=4; s->mirror=1; s->planned=j?-1:0;
        s->half_width=piece->points[0].width; s->depth=piece->points[0].depth; s->discharge=.4;
        qlcg_source_material_set(s,2);
        if(!j&&edit->source>=0&&old&&edit->source<old->source_count) *s=old->sources[edit->source];
        SourceRiverEdge edge={0}; edge.points=piece->points; piece->points=NULL; edge.count=piece->count;
        edge.source=source; edge.craft_id=id; edge.craft_owner=owner; edge.craft_order=j;
        for(int k=0;k<edge.count;k++) {
            RiverVectorPoint *p=edge.points+k; SurfacePoint q=qlcg_frame_local(&n->frame,p->wx,p->wz); p->x=q.x; p->z=q.z;
            p->arc=k?edge.points[k-1].arc+hypot(p->x-edge.points[k-1].x,p->z-edge.points[k-1].z):0;
            double natural=qlcg_sample_natural_height(w,(float)p->wx,(float)p->wz);
            p->level=fmin(previous_level,edit->source>=0?p->level:fmin(p->level,natural-.12)); previous_level=p->level;
        }
        s->x=edge.points[0].x; s->z=edge.points[0].z; s->level=s->surface_level=edge.points[0].level;
        s->direction_x=1; s->spring_radius=4;
        if(!j) head_source=source;
        if(edge.count<2) {
            qlcg_source_pool_shape(s); s->pool=3; s->pool_level=s->level; s->pool_depth=1.5;
            free(edge.points); piece_count=j+1; finished=1; continue;
        }
        MainEdge *main=calloc(25,sizeof(*main)); RiverDetailCandidates candidates={0}; int main_count=0;
        if(!main) { free(edge.points); status=RIVER_EDIT_MEMORY; break; }
        qlcg_river_detail_candidates(w,n->cell,&candidates);
        for(int k=0;k<candidates.count;k++) if(read_main_edge(w,&n->frame,candidates.edges[k],main+main_count)) main_count++;
        Target hit={0}; int attached=intersection(&edge,main,main_count,n,&hit);
        if(!attached&&j==piece_count-1) attached=endpoint_target(w,n,&edge,main,main_count,&hit);
        free(main);
        for(int k=0;k<edge.count;k++) {
            RiverVectorPoint *p=edge.points+k;
            p->arc=k?edge.points[k-1].arc+hypot(p->x-edge.points[k-1].x,p->z-edge.points[k-1].z):0;
            SurfacePoint q=qlcg_frame_coordinates(&n->frame,p->x,p->z); p->wx=q.x; p->wz=q.z;
        }
        if(attached&&hit.p.level>=s->level-.001) { finished=1; piece_count=j+1; }
        if(attached&&edge.count>=2&&hit.p.level<s->level-.001) {
            edge.target_kind=hit.source; edge.target=hit.edge; finished=1; piece_count=j+1;
            edge.points[edge.count-1].level=hit.p.level;
            for(int k=edge.count-2;k>=0;k--) edge.points[k].level=fmax(edge.points[k].level,edge.points[k+1].level);
            configure_join(&edge,&hit,s->seed);
        } else {
            edge.target_kind=j==piece_count-1?SOURCE_TARGET_POOL:SOURCE_TARGET_CONTINUATION; edge.target=n->cell;
        }
        edge.profile.influence=512; edge.profile.count=2; edge.profile.path=edge.points; edge.profile.path_count=edge.count;
        for(int k=0;k<edge.count;k++) {
            RiverVectorPoint *p=edge.points+k; p->fall=0; p->slope=0;
            p->width=clamp(p->width,2.8,16); p->depth=clamp(p->depth,.5,2.5);
            if(k) p->level=fmin(p->level,edge.points[k-1].level);
        }
        for(int k=0;k<edge.count;k++) {
            RiverVectorPoint *p=edge.points+k;
            if(k+1<edge.count) p->slope=(edge.points[k+1].level-p->level)/fmax(.01,edge.points[k+1].arc-p->arc);
        }
        const SourceRiverNetwork *original=edit->source>=0?qlcg_source_network(w,n->cell,0):NULL;
        for(int b=0;original&&b<original->edge_count;b++) {
            const SourceRiverEdge *prior=original->edges+b;
            if(prior->craft_id!=id&&!(n->cell==edit->cell&&prior->source==edit->source)) continue;
            for(int k=0;k<edge.count;k++) {
                RiverVectorPoint *p=edge.points+k; double distance,t;
                int at=qlcg_river_index_nearest(&prior->spatial,prior->points,p->x,p->z,&distance,&t);
                if(at<0||distance>.0001) continue;
                if(t>1-1e-7) at++;
                else if(t>1e-7) continue;
                if(fabs(p->level-prior->points[at].level)<.0001) { p->fall=prior->points[at].fall; p->slope=prior->points[at].slope; }
            }
        }
        s->connected=1;
        if(edge.count>SOURCE_POINT_LIMIT||!qlcg_river_index_build(&edge.spatial,edge.points,edge.count,edge.profile.influence)) {
            free(edge.points); status=RIVER_EDIT_MEMORY; break;
        }
        n->edges[n->edge_count++]=edge;
    }
    if(!status&&!remove&&piece_count>0) {
        SourceRiverNetwork *n=craft_stage(w,owner,stages,&stage_count);
        SourceRiverEdge *head=(SourceRiverEdge *)craft_edge(n,head_source);
        if(head) {
            head->craft_count=piece_count; head->craft_cells=malloc((size_t)piece_count*sizeof(int));
            if(!head->craft_cells) status=RIVER_EDIT_MEMORY;
            else for(int j=0;j<piece_count;j++) head->craft_cells[j]=pieces[j].cell;
        }
    }
    for(int k=0;!status&&k<stage_count;k++) {
        SourceRiverNetwork *n=stages[k].network;
        for(int a=0;a<n->edge_count;a++) {
            SourceRiverEdge *child=n->edges+a;
            if(child->target_kind!=SOURCE_TARGET_POOL||child->target!=-1) continue;
            child->target=n->cell;
            RiverVectorPoint *end=child->points+child->count-1;
            for(int b=0;b<n->edge_count;b++) {
                SourceRiverEdge *parent=n->edges+b; if(parent->craft_id!=id) continue;
                double distance,t; int at=qlcg_river_index_nearest(&parent->spatial,parent->points,end->x,end->z,&distance,&t);
                if(at<0||distance>mix(parent->points[at].width,parent->points[at+1].width,t)) continue;
                double level=qlcg_source_profile_value(&parent->profile,mix(parent->points[at].arc,parent->points[at+1].arc,t),NULL,NULL);
                if(level>end->level+.02) continue;
                child->target_kind=1; child->target=parent->source; break;
            }
        }
        updates[k].kind=RIVER_RECORD_SOURCE; updates[k].cell=stages[k].cell;
        if(!network_record(stages[k].network,&updates[k].buffer)) status=RIVER_EDIT_MEMORY;
    }
    if(!status&&stage_count&&!qlcg_river_archive_commit(w,updates,stage_count)) status=RIVER_EDIT_MEMORY;
    for(int k=0;k<stage_count;k++) { network_free(stages[k].network); qlcg_river_buffer_free(&updates[k].buffer); }
    for(int k=0;k<SOURCE_POINT_LIMIT*2;k++) free(pieces[k].points);
    free(stages); free(pieces); free(updates); if(!status) w->surface_revision++;
    return status;
}
