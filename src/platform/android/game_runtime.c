#if defined(__ANDROID__) && defined(QLCG_GAME_ACTIVITY)
#include "app/app_state.h"
#include "egl_backend.h"
#include "ui/pages/game/view_atlas_layers.h"
#include "ui/ui_touch_gesture.h"
#include "app/frame_pacer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <time.h>
#include <string.h>
#include <stdatomic.h>

static float pointer_x(const GameActivityPointerAxes *p) { return p->axisValues[AMOTION_EVENT_AXIS_X]; }
static float pointer_y(const GameActivityPointerAxes *p) { return p->axisValues[AMOTION_EVENT_AXIS_Y]; }
static atomic_int system_edges[4],density_percent=100;
static atomic_bool back_requested;
JNIEXPORT void JNICALL Java_org_qianlinchaguan_MainActivity_back(JNIEnv *env,jclass cls) {
    (void)env;(void)cls;atomic_store(&back_requested,true);
}
JNIEXPORT void JNICALL Java_org_qianlinchaguan_MainActivity_configure(JNIEnv *env,jclass cls,jfloat density,jint left,jint top,jint right,jint bottom) {
    (void)env;(void)cls;atomic_store(&density_percent,(int)(density*100));
    atomic_store(&system_edges[0],left);atomic_store(&system_edges[1],top);
    atomic_store(&system_edges[2],right);atomic_store(&system_edges[3],bottom);
}
static struct android_app *host;
static QlEgl egl;
static bool initialized,graphics,resumed,focused,save_requested;
static int mouse_pointer=-1;
static float pinch,scroll_remainder;
static UiTouchGesture gesture;
static bool pan_canvas,horizontal_drag;
static double last_ms;

void app_apply_hidpi_scale(void) { app_sync_layout_axes(); }
void app_set_mouse_lock(bool on) { (void)on;g_app.terrain.mouse_captured=false; }
bool qlcg_platform_key_down(unsigned key) { (void)key;return false; }
void qlcg_platform_prepare(void) {}
void qlcg_platform_capture(void) {}
void qlcg_platform_poll(void) {}
bool qlcg_platform_window_open(int w,int h) { return qlcg_egl_open(&egl,host->window,w,h); }
void qlcg_platform_window_close(void) { qlcg_egl_close(&egl); }
void qlcg_platform_present(void) {
    if(!qlcg_egl_present(&egl)) {
        app_graphics_release(true);qlcg_egl_close(&egl);graphics=false;
    }
}
void qlcg_frame_pacer_init(FramePacer *p) { p->timer=NULL; }
void qlcg_frame_pacer_shutdown(FramePacer *p) { (void)p; }
void qlcg_frame_pacer_wait(FramePacer *p,double start,int fps) {
    (void)p;double left=1000.0/(fps>0?fps:60)-(qlcg_time_ms()-start);
    if(left>0) { struct timespec t={(time_t)(left/1000),(long)(fmod(left,1000)*1000000)};nanosleep(&t,NULL); }
}
static void clear_input(void) {
    for(int i=0;i<ACT_COUNT;i++)app_action_set_virtual(i,false);
    app_key_track_clear();ui_touch_reset(&g_app.touch);
    g_app.ui.mouse_down=g_app.ui.right_down=g_app.ui.touch_active=false;
    g_app.atlas.dragging=false;mouse_pointer=-1;pinch=scroll_remainder=0;
    g_app.river_craft.drawing=false;
    memset(&gesture,0,sizeof(gesture));pan_canvas=horizontal_drag=false;
    g_app.ui.mouse_clicked=g_app.ui.mouse_dblclicked=false;
    g_app.ui.touch_double_tap=g_app.ui.touch_drag_horizontal=false;
}
static void background_save(void) {
    clear_input();
    save_requested=initialized && g_app.world;
}
static void command(struct android_app *app,int32_t cmd) {
    switch(cmd) {
    case APP_CMD_INIT_WINDOW:
        if(!app->window)break;
        if(!initialized) {
            if(!app_initialize()) { GameActivity_finish(app->activity);break; }
            initialized=graphics=true;g_app.ui.pointer_touch=true;
        } else if(!graphics) {
            if(!qlcg_egl_open(&egl,app->window,0,0))break;
            graphics=app_graphics_restore();
        }
        last_ms=qlcg_time_ms();clear_input();break;
    case APP_CMD_TERM_WINDOW:
        if(resumed)background_save();else clear_input();
        if(graphics)app_graphics_release(false);
        graphics=false;qlcg_egl_close(&egl);break;
    case APP_CMD_EDITOR_ACTION: app_input_key_down(QL_KEY_RETURN,false);break;
    case APP_CMD_RESUME: resumed=true;last_ms=qlcg_time_ms();clear_input();break;
    case APP_CMD_PAUSE: resumed=false;background_save();break;
    case APP_CMD_GAINED_FOCUS: focused=true;last_ms=qlcg_time_ms();clear_input();break;
    case APP_CMD_LOST_FOCUS: focused=false;clear_input();break;
    case APP_CMD_LOW_MEMORY:
        if(initialized && graphics) { qlcg_atlas_release_world(&g_app.atlas);qlcg_terrain_release_scene(&g_app.terrain); }
        break;
    default:break;
    }
}
static uint32_t text_id;
static JNIEnv *game_env;
static uint32_t byte_offset(const char *text,int units) {
    uint32_t at=0;int n=0;
    while(text[at] && n<units) { uint32_t next=ui_u8_next(text,at);n+=next-at==4?2:1;at=next; }
    return at;
}
static int utf16_offset(const char *text,uint32_t bytes) {
    uint32_t at=0;int count=0;
    while(text[at] && at<bytes) { uint32_t next=ui_u8_next(text,at);count+=next-at==4?2:1;at=next; }
    return count;
}
static void text_received(void *unused,const GameTextInputState *state) {
    (void)unused;UiEditField *f=app_text_target();if(!f || !state->text_UTF8)return;
    jstring str=(*game_env)->NewStringUTF(game_env,state->text_UTF8);if(!str)return;
    const jchar *u=(*game_env)->GetStringChars(game_env,str,NULL);
    jsize length=(*game_env)->GetStringLength(game_env,str);
    wchar_t wide[4096];size_t used=0;
    for(jsize i=0;u && i<length && used<4095;i++) {
        uint32_t cp=u[i];
        if(cp>=0xD800 && cp<=0xDBFF && i+1<length && u[i+1]>=0xDC00 && u[i+1]<=0xDFFF)
            cp=0x10000+((cp-0xD800)<<10)+u[++i]-0xDC00;
        if(cp>=32 && !(cp>=0xD800 && cp<=0xDFFF))wide[used++]=(wchar_t)cp;
    }
    wide[used]=0;char utf8[16384];
    if(qlcg_wide_to_utf8(wide,utf8,sizeof(utf8))) {
        ui_u8_copy(f->buf,f->cap,utf8);
        if(f->max_chars && ui_u8_count(f->buf)>f->max_chars) {
            uint32_t at=0;for(uint32_t i=0;i<f->max_chars;i++)at=ui_u8_next(f->buf,at);f->buf[at]=0;
        }
        f->ed->caret=byte_offset(f->buf,state->selection.end);
        f->ed->anchor=byte_offset(f->buf,state->selection.start);g_app.ui.input_dirty=true;
    }
    if(u)(*game_env)->ReleaseStringChars(game_env,str,u);(*game_env)->DeleteLocalRef(game_env,str);
}
static void sync_text(void) {
    static char previous[16384];static uint32_t caret,anchor,request_frame;
    bool incoming=host->textInputState!=0;
    if(incoming) { host->textInputState=0;GameActivity_getTextInputState(host->activity,text_received,NULL); }
    UiEditField *f=app_text_target();uint32_t id=g_app.ui.input_id?g_app.ui.input_id:(g_app.chat_open?0xFFFFFFFFu:0);
    bool focus_changed=id!=text_id;
    bool requested=f && request_frame!=g_app.ui.ime_request_frame;
    request_frame=g_app.ui.ime_request_frame;
    bool selection_changed=f && (f->ed->caret!=caret || f->ed->anchor!=anchor || strcmp(previous,f->buf));
    if(focus_changed || (!incoming && selection_changed)) {
        text_id=id;
        if(f) {
            wchar_t wide[4096];jchar units[8192];size_t n=0;
            qlcg_utf8_to_wide(f->buf,wide,4096);
            for(size_t i=0;wide[i];i++) {
                uint32_t cp=wide[i];if(cp>0xFFFF) { cp-=0x10000;units[n++]=0xD800+(cp>>10);units[n++]=0xDC00+(cp&1023); }
                else units[n++]=(jchar)cp;
            }
            jstring value=(*game_env)->NewString(game_env,units,(jsize)n);
            const char *text=(*game_env)->GetStringUTFChars(game_env,value,NULL);
            GameTextInputState state={text,(int32_t)strlen(text),{utf16_offset(f->buf,f->ed->anchor),utf16_offset(f->buf,f->ed->caret)},{-1,-1}};
            GameActivity_setTextInputState(host->activity,&state);
            (*game_env)->ReleaseStringUTFChars(game_env,value,text);(*game_env)->DeleteLocalRef(game_env,value);
        } else GameActivity_hideSoftInput(host->activity,0);
    }
    if(f && (focus_changed || requested))GameActivity_showSoftInput(host->activity,0);
    if(f) { ui_u8_copy(previous,sizeof(previous),f->buf);caret=f->ed->caret;anchor=f->ed->anchor; }
}
static bool all_motion(const GameActivityMotionEvent *event) { (void)event;return true; }
static void motion(const GameActivityMotionEvent *event) {
    UiContext *ui=&g_app.ui;
    int action=event->action&AMOTION_EVENT_ACTION_MASK;
    int index=(event->action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    float density=fmaxf(1,ui->device_density);
    if(action==AMOTION_EVENT_ACTION_CANCEL) { clear_input();return; }
    if(event->pointerCount>=2 && pan_canvas && g_app.view_mode==VIEW_MODE_ATLAS && !g_app.esc_paused && g_app.world) {
        float x0=pointer_x(&event->pointers[0]),y0=pointer_y(&event->pointers[0]);
        float x1=pointer_x(&event->pointers[1]),y1=pointer_y(&event->pointers[1]);
        float length=hypotf(x0-x1,y0-y1);
        if(pinch>0 && length>0)qlcg_atlas_zoom_step(&g_app.atlas,g_app.world,logf(length/pinch)/logf(1.42f),
            (x0+x1)*.5f,(y0+y1)*.5f,g_app.atlas.last_vp_x,g_app.atlas.last_vp_y,g_app.atlas.last_vp_w,g_app.atlas.last_vp_h);
        pinch=length;ui_gesture_cancel(&gesture);
        g_app.atlas.dragging=false;ui->mouse_down=ui->mouse_clicked=false;
        ui->touch_double_tap=ui->touch_drag_horizontal=false;
    } else pinch=0;
    for(uint32_t i=0;i<event->pointerCount;i++) {
        int id=event->pointers[i].id;
        float x=pointer_x(&event->pointers[i]),y=pointer_y(&event->pointers[i]);
        bool down=(action==AMOTION_EVENT_ACTION_DOWN || action==AMOTION_EVENT_ACTION_POINTER_DOWN) && (int)i==index;
        bool up=(action==AMOTION_EVENT_ACTION_UP || action==AMOTION_EVENT_ACTION_POINTER_UP) && (int)i==index;
        if(down) {
            ui->mouse_x=(int)x;ui->mouse_y=(int)y;
            bool taken=false;
            if(!game_scene_over_ui(ui))taken=ui_touch_down(&g_app.touch,ui,id,x,y);
            if(taken && id==g_app.touch.look_pointer && g_app.terrain.eng_tool!=ENG_TOOL_NONE) {
                ui_touch_up(&g_app.touch,ui,id);taken=false;
            }
            if(!taken && mouse_pointer<0) {
                mouse_pointer=id;ui->mouse_down=true;ui->mouse_clicked=false;
                ui_gesture_down(&gesture,x,y,qlcg_time_ms());
                horizontal_drag=false;ui->touch_drag_horizontal=false;scroll_remainder=0;
                pan_canvas=g_app.screen==SCREEN_IN_GAME && g_app.view_mode==VIEW_MODE_ATLAS &&
                    g_app.world && !game_atlas_over_ui(ui) && !g_app.river_craft.active && g_app.atlas.eng_tool==ENG_TOOL_NONE;
                app_river_craft_pointer(true,true);
            }
        } else if(action==AMOTION_EVENT_ACTION_MOVE) {
            ui_touch_move(&g_app.touch,ui,id,x,y);
            if(id==mouse_pointer && !gesture.cancelled) {
                bool was_dragged=gesture.dragged;
                bool dragged=ui_gesture_move(&gesture,x,y,density);
                float dx=x-(was_dragged?ui->mouse_x:gesture.start_x);
                float dy=y-(was_dragged?ui->mouse_y:gesture.start_y);
                ui->mouse_x=(int)x;ui->mouse_y=(int)y;
                if(dragged) {
                    if(pan_canvas) {
                        g_app.atlas.dragging=true;
                        qlcg_atlas_drag(&g_app.atlas,g_app.world,x,y,dx,dy);
                    } else if(g_app.screen!=SCREEN_IN_GAME || game_scene_over_ui(ui) ||
                              (g_app.view_mode==VIEW_MODE_ATLAS && game_atlas_over_ui(ui))) {
                        if(!was_dragged)horizontal_drag=fabsf(x-gesture.start_x)>fabsf(y-gesture.start_y)*1.2f;
                        ui->touch_drag_horizontal=horizontal_drag;
                        if(!horizontal_drag) {
                            scroll_remainder+=dy*120/(48*fmaxf(1,ui->met.size));
                            int wheel=(int)scroll_remainder;ui->wheel_delta+=wheel;scroll_remainder-=wheel;
                        }
                    }
                    app_river_craft_pointer(false,ui->mouse_down);
                }
            }
        }
        if(up) {
            ui_touch_up(&g_app.touch,ui,id);
            if(id==mouse_pointer) {
                ui->mouse_x=(int)x;ui->mouse_y=(int)y;
                ui->mouse_clicked=ui_gesture_up(&gesture,x,y,qlcg_time_ms(),density);
                ui->touch_double_tap=ui->mouse_clicked && gesture.double_tap;
                ui->mouse_down=ui->touch_drag_horizontal=false;
                app_river_craft_pointer(false,false);g_app.atlas.dragging=false;mouse_pointer=-1;pan_canvas=false;
            }
        }
    }
}

void android_main(struct android_app *app) {
    initialized=graphics=resumed=focused=save_requested=false;text_id=0;
    host=app;(*app->activity->vm)->AttachCurrentThread(app->activity->vm,&game_env,NULL);app->onAppCmd=command;android_app_set_motion_event_filter(app,all_motion);
    FramePacer pacer;qlcg_frame_pacer_init(&pacer);
    while(!app->destroyRequested) {
        struct android_poll_source *source;int events;
        bool active=initialized && graphics && resumed && focused;
        int timeout=active?0:((initialized && app->window && resumed && focused)?250:-1);
        int result=ALooper_pollOnce(timeout,NULL,&events,(void **)&source);
        while(result>=0) {
            if(source)source->process(app,source);
            if(app->destroyRequested)break;
            result=ALooper_pollOnce(0,NULL,&events,(void **)&source);
        }
        if(save_requested) { save_requested=false;app_save_current(SAVE_SLOT_AUTO,false); }
        if(app->destroyRequested)break;
        if(initialized && !graphics && app->window && resumed && focused) {
            if(qlcg_egl_open(&egl,app->window,0,0)) {
                graphics=app_graphics_restore();last_ms=qlcg_time_ms();clear_input();
                if(!graphics)qlcg_egl_close(&egl);
            }
        }
        if(!initialized || !graphics || !resumed || !focused)continue;
        struct android_input_buffer *input=android_app_swap_input_buffers(app);
        if(input) {
            for(uint64_t i=0;i<input->motionEventsCount;i++)motion(&input->motionEvents[i]);
            for(uint64_t i=0;i<input->keyEventsCount;i++) {
                GameActivityKeyEvent *k=&input->keyEvents[i];
                if(k->action==AKEY_EVENT_ACTION_DOWN && k->keyCode==AKEYCODE_BACK) {
                    if(app->softwareKeyboardVisible)GameActivity_hideSoftInput(app->activity,0);
                    else app_input_key_down(QL_KEY_ESCAPE,false);
                }
            }
            android_app_clear_motion_events(input);android_app_clear_key_events(input);
        }
        g_app.win_w=ANativeWindow_getWidth(app->window);g_app.win_h=ANativeWindow_getHeight(app->window);
        double start=qlcg_time_ms();float dt=qlcg_clampf((float)((start-last_ms)*.001),.001f,.05f);last_ms=start;
        g_app.ui.device_density=atomic_load(&density_percent)/100.f;
        for(int i=0;i<4;i++)g_app.ui.system_edges[i]=(float)atomic_load(&system_edges[i]);
        if(atomic_exchange(&back_requested,false)) {
            if(app->softwareKeyboardVisible)GameActivity_hideSoftInput(app->activity,0);
            else app_input_key_down(QL_KEY_ESCAPE,false);
        }
        sync_text();app_update(dt);app_render_frame(dt);
        if(!g_app.running) { GameActivity_finish(app->activity);break; }
        qlcg_frame_pacer_wait(&pacer,start,g_app.settings.frame_rate);
    }
    if(initialized) { if(g_app.world)app_save_current(SAVE_SLOT_AUTO,false);app_shutdown(); }
    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
}
#endif
