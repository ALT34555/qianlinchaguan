#ifdef __ANDROID__
#include "bridge.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/ui_backend.h"
#include "core/png_image.h"
#include "ui/ui_font_mod.h"
#include "ui/ui_font_manifest.h"
#include <jni.h>
#include <android/bitmap.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static JavaVM *vm;
static jclass platform_class;
static jobject activity_ref;
static jmethodID text_method,image_method,png_method,font_method,clear_method,import_method,export_method,poll_method;
struct QlTextBackend { int ready; };
typedef struct { JNIEnv *env; bool attached; } BridgeEnv;
static BridgeEnv bridge_env(void) {
    BridgeEnv result={0}; if(!vm) return result;
    jint status=(*vm)->GetEnv(vm,(void **)&result.env,JNI_VERSION_1_6);
    if(status==JNI_EDETACHED) {
        if((*vm)->AttachCurrentThread(vm,&result.env,NULL)!=JNI_OK) result.env=NULL;
        else result.attached=true;
    } else if(status!=JNI_OK) result.env=NULL;
    return result;
}
static bool bridge_ok(JNIEnv *env) {
    if(!(*env)->ExceptionCheck(env)) return true;
    (*env)->ExceptionClear(env); return false;
}
static void bridge_done(BridgeEnv call) { if(call.attached) (*vm)->DetachCurrentThread(vm); }
static jstring java_string(JNIEnv *env,const char *text) {
    size_t n=strlen(text)+1; wchar_t *wide=malloc(n*sizeof(*wide)); jchar *units=malloc(n*2*sizeof(*units));
    if(!wide || !units) { free(wide); free(units); return NULL; }
    size_t used=0;
    if(!qlcg_utf8_to_wide(text,wide,n)) { free(wide); free(units); return NULL; }
    for(size_t i=0;wide[i];i++) {
        uint32_t cp=(uint32_t)wide[i];
        if(cp>=0x10000) { cp-=0x10000; units[used++]=(jchar)(0xD800+(cp>>10)); units[used++]=(jchar)(0xDC00+(cp&1023)); }
        else units[used++]=(jchar)cp;
    }
    jstring result=(*env)->NewString(env,units,(jsize)used); free(wide); free(units); return result;
}
static bool native_string(JNIEnv *env,jstring text,char *out,size_t cap) {
    if(!text || !cap) return false; out[0]=0;
    jsize n=(*env)->GetStringLength(env,text); const jchar *units=(*env)->GetStringChars(env,text,NULL);
    wchar_t *wide=calloc((size_t)n+1,sizeof(*wide)); size_t used=0; bool ok=units && wide;
    for(jsize i=0;ok && i<n;i++) {
        uint32_t cp=units[i];
        if(cp>=0xD800 && cp<=0xDBFF) {
            if(i+1>=n || units[i+1]<0xDC00 || units[i+1]>0xDFFF) { ok=false; break; }
            cp=0x10000+((cp-0xD800)<<10)+units[++i]-0xDC00;
        }
        wide[used++]=(wchar_t)cp;
    }
    if(ok) ok=qlcg_wide_to_utf8(wide,out,cap)>0;
    if(units) (*env)->ReleaseStringChars(env,text,units); free(wide); return ok;
}
JNIEXPORT jboolean JNICALL Java_org_qianlinchaguan_QlPlatform_bind(JNIEnv *env,jclass cls,jobject activity,jstring resources,jstring data,jstring cache) {
    char r[2048],d[2048],c[2048];
    if(!native_string(env,resources,r,sizeof(r)) || !native_string(env,data,d,sizeof(d)) || !native_string(env,cache,c,sizeof(c))) return JNI_FALSE;
    (*env)->GetJavaVM(env,&vm);
    if(platform_class) (*env)->DeleteGlobalRef(env,platform_class);
    if(activity_ref) (*env)->DeleteGlobalRef(env,activity_ref);
    platform_class=(*env)->NewGlobalRef(env,cls); activity_ref=(*env)->NewGlobalRef(env,activity);
    text_method=(*env)->GetStaticMethodID(env,cls,"text","(Ljava/lang/String;Ljava/lang/String;IZ)Landroid/graphics/Bitmap;");
    image_method=(*env)->GetStaticMethodID(env,cls,"image","(Ljava/lang/String;)Landroid/graphics/Bitmap;");
    png_method=(*env)->GetStaticMethodID(env,cls,"png","(Ljava/lang/String;[III)Z");
    font_method=(*env)->GetStaticMethodID(env,cls,"addFont","(Ljava/lang/String;Ljava/lang/String;)Z");
    clear_method=(*env)->GetStaticMethodID(env,cls,"clearFonts","()V");
    import_method=(*env)->GetStaticMethodID(env,cls,"requestImport","(Landroid/app/Activity;)V");
    export_method=(*env)->GetStaticMethodID(env,cls,"requestExport","(Landroid/app/Activity;Ljava/lang/String;)V");
    poll_method=(*env)->GetStaticMethodID(env,cls,"pollDocument","()[Ljava/lang/String;");
    return bridge_ok(env) && platform_class && activity_ref && qlcg_paths_configure(r,d,c);
}
static bool bitmap_pixels(JNIEnv *env,jobject bitmap,uint8_t **pixels,int *width,int *height,bool bgra) {
    AndroidBitmapInfo info; void *raw=NULL; bool ok=false;
    if(bitmap && AndroidBitmap_getInfo(env,bitmap,&info)==ANDROID_BITMAP_RESULT_SUCCESS && info.format==ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        info.width>0 && info.height>0 && (uint64_t)info.width*info.height<=16777216 && AndroidBitmap_lockPixels(env,bitmap,&raw)==ANDROID_BITMAP_RESULT_SUCCESS) {
        uint8_t *copy=malloc((size_t)info.width*info.height*4);
        if(copy) {
            for(uint32_t y=0;y<info.height;y++) memcpy(copy+(size_t)y*info.width*4,(uint8_t *)raw+(size_t)y*info.stride,(size_t)info.width*4);
            if(bgra) for(size_t i=0;i<(size_t)info.width*info.height;i++) { uint8_t t=copy[4*i]; copy[4*i]=copy[4*i+2]; copy[4*i+2]=t; }
            *pixels=copy; *width=(int)info.width; *height=(int)info.height; ok=true;
        }
        AndroidBitmap_unlockPixels(env,bitmap);
    }
    if(bitmap) (*env)->DeleteLocalRef(env,bitmap);
    if(!bridge_ok(env)) { if(ok) { free(*pixels); *pixels=NULL; } return false; }
    return ok;
}
bool qlcg_platform_font_init(UiContext *ui) {
    ui->text_backend=calloc(1,sizeof(*ui->text_backend));
    wcscpy(ui->family_display,L"sans-serif"); wcscpy(ui->family_body,L"sans-serif"); wcscpy(ui->family_mono,L"monospace");
    return ui->text_backend!=NULL;
}
void qlcg_platform_font_shutdown(UiContext *ui) { free(ui->text_backend); ui->text_backend=NULL; }
void qlcg_platform_font_rebuild(UiContext *ui) { for(int i=0;i<UI_CACHE_SLOTS;i++) ui->slots[i].used=false; }
bool qlcg_platform_text_raster(UiContext *ui,int face,int px,bool bold,const char *text,uint8_t **pixels,int *width,int *height) {
    *pixels=NULL; BridgeEnv call=bridge_env(); if(!call.env) return false;
    JNIEnv *env=call.env; char family[256];
    const wchar_t *name=face==FONT_MONO?ui->family_mono:face<=FONT_SUBTITLE?ui->family_display:ui->family_body;
    if(!qlcg_wide_to_utf8(name,family,sizeof(family))) { bridge_done(call); return false; }
    jstring t=java_string(env,text),f=java_string(env,family); jobject bitmap=NULL;
    if(t && f) bitmap=(*env)->CallStaticObjectMethod(env,platform_class,text_method,t,f,px,bold);
    bool ok=bridge_ok(env) && bitmap_pixels(env,bitmap,pixels,width,height,false);
    if(t) (*env)->DeleteLocalRef(env,t); if(f) (*env)->DeleteLocalRef(env,f); bridge_done(call); return ok;
}
bool qlcg_png_load_bgra(const wchar_t *path,uint8_t **pixels,int *width,int *height) {
    if(!pixels || !width || !height) return false; *pixels=NULL; *width=*height=0;
    char logical[2048],resolved[4096];
    if(!qlcg_wide_to_utf8(path,logical,sizeof(logical)) || !qlcg_path_resolve(logical,false,resolved,sizeof(resolved))) return false;
    BridgeEnv call=bridge_env(); if(!call.env) return false;
    JNIEnv *env=call.env; jstring p=java_string(env,resolved); jobject bitmap=p?(*env)->CallStaticObjectMethod(env,platform_class,image_method,p):NULL;
    bool ok=bridge_ok(env) && bitmap_pixels(env,bitmap,pixels,width,height,true);
    if(p) (*env)->DeleteLocalRef(env,p); bridge_done(call); return ok;
}
bool qlcg_png_save_bgra(const wchar_t *path,const uint8_t *pixels,int width,int height) {
    if(!pixels || width<1 || height<1 || (uint64_t)width*height>16777216) return false;
    char logical[2048],resolved[4096];
    if(!qlcg_wide_to_utf8(path,logical,sizeof(logical)) || !qlcg_path_resolve(logical,true,resolved,sizeof(resolved))) return false;
    BridgeEnv call=bridge_env(); if(!call.env) return false;
    JNIEnv *env=call.env; size_t count=(size_t)width*height; jint *argb=malloc(count*4); bool ok=false;
    jstring p=java_string(env,resolved); jintArray array=(*env)->NewIntArray(env,(jsize)count);
    if(argb && p && array) {
        for(size_t i=0;i<count;i++) argb[i]=(jint)(((uint32_t)pixels[4*i+3]<<24)|((uint32_t)pixels[4*i+2]<<16)|((uint32_t)pixels[4*i+1]<<8)|pixels[4*i]);
        (*env)->SetIntArrayRegion(env,array,0,(jsize)count,argb);
        if(bridge_ok(env)) ok=(*env)->CallStaticBooleanMethod(env,platform_class,png_method,p,array,width,height)==JNI_TRUE;
    }
    ok=bridge_ok(env) && ok; if(p) (*env)->DeleteLocalRef(env,p); if(array) (*env)->DeleteLocalRef(env,array);
    free(argb); bridge_done(call); return ok;
}
int ui_font_mod_scan(const wchar_t *dir,UiModFontList *list) {
    if(!list) return 0; list->count=0; char logical[2048];
    if(!qlcg_wide_to_utf8(dir,logical,sizeof(logical))) return 0;
    BridgeEnv call=bridge_env(); if(!call.env) return 0; JNIEnv *env=call.env;
    UiFontManifest manifest[UI_FONT_MANIFEST_CAP];
    int count=ui_font_manifest_read(logical,manifest,UI_FONT_MANIFEST_CAP);
    QlDirectory *scan=qlcg_platform_dir_open(logical); QlDirectoryEntry entry;
    while(list->count<UI_FONT_MOD_MAX && qlcg_platform_dir_next(scan,&entry)) {
        const char *ext=strrchr(entry.name,'.');
        if(entry.directory || !ext || (qlcg_stricmp(ext,".ttf") && qlcg_stricmp(ext,".otf") && qlcg_stricmp(ext,".ttc"))) continue;
        UiModFont font={0}; char path[4096],resolved[4096];
        if(strlen(entry.name)>55) continue;
        snprintf(path,sizeof(path),"%s/%s",logical,entry.name);
        if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) continue;
        snprintf(font.id,sizeof(font.id),"mod:%s",entry.name);
        const char *label=entry.name;
        for(int i=0;i<count;i++) if(!qlcg_stricmp(manifest[i].file,entry.name)) {
            if(manifest[i].id[0]) snprintf(font.id,sizeof(font.id),"%s",manifest[i].id);
            if(manifest[i].label[0]) label=manifest[i].label;
            break;
        }
        char family[64]; snprintf(family,sizeof(family),"qlcg-font-%d",list->count);
        if(!qlcg_utf8_to_wide(family,font.family,64) || !qlcg_utf8_to_wide(label,font.label,48) || !qlcg_utf8_to_wide(resolved,font.file,260)) continue;
        jstring id=java_string(env,family),file=java_string(env,resolved);
        bool ok=id && file && (*env)->CallStaticBooleanMethod(env,platform_class,font_method,id,file)==JNI_TRUE;
        ok=bridge_ok(env) && ok;
        if(id) (*env)->DeleteLocalRef(env,id); if(file) (*env)->DeleteLocalRef(env,file);
        if(ok) list->items[list->count++]=font;
    }
    qlcg_platform_dir_close(scan); bridge_done(call); return list->count;
}
const UiModFont *ui_font_mod_find(const UiModFontList *list,const char *id) {
    if(list && id) for(int i=0;i<list->count;i++) if(!strcmp(list->items[i].id,id)) return &list->items[i];
    return NULL;
}
void ui_font_mod_unload(void) {
    BridgeEnv call=bridge_env(); if(!call.env) return;
    (*call.env)->CallStaticVoidMethod(call.env,platform_class,clear_method); bridge_ok(call.env); bridge_done(call);
}
bool qlcg_android_import_request(void) {
    BridgeEnv call=bridge_env(); if(!call.env) return false;
    (*call.env)->CallStaticVoidMethod(call.env,platform_class,import_method,activity_ref);
    bool ok=bridge_ok(call.env); bridge_done(call); return ok;
}
bool qlcg_platform_dark(void) {
    BridgeEnv call=bridge_env(); if(!call.env) return false; JNIEnv *env=call.env;
    jmethodID method=(*env)->GetStaticMethodID(env,platform_class,"dark","(Landroid/app/Activity;)Z");
    bool dark=method && (*env)->CallStaticBooleanMethod(env,platform_class,method,activity_ref)==JNI_TRUE;
    dark=bridge_ok(env) && dark; bridge_done(call); return dark;
}
bool qlcg_platform_clip_get(char *out,size_t cap) {
    BridgeEnv call=bridge_env(); if(!call.env) return false; JNIEnv *env=call.env;
    jmethodID method=(*env)->GetStaticMethodID(env,platform_class,"clipboard","(Landroid/app/Activity;)Ljava/lang/String;");
    jstring text=method?(*env)->CallStaticObjectMethod(env,platform_class,method,activity_ref):NULL;
    bool ok=bridge_ok(env) && native_string(env,text,out,cap);
    if(text) (*env)->DeleteLocalRef(env,text); bridge_done(call); return ok;
}
bool qlcg_platform_clip_set(const char *text) {
    BridgeEnv call=bridge_env(); if(!call.env) return false; JNIEnv *env=call.env;
    jmethodID method=(*env)->GetStaticMethodID(env,platform_class,"clipboardSet","(Landroid/app/Activity;Ljava/lang/String;)V");
    jstring value=java_string(env,text);
    if(method && value) (*env)->CallStaticVoidMethod(env,platform_class,method,activity_ref,value);
    bool ok=bridge_ok(env) && method && value;
    if(value) (*env)->DeleteLocalRef(env,value); bridge_done(call); return ok;
}
bool qlcg_android_export_request(const char *path) {
    char resolved[4096]; if(!qlcg_path_resolve(path,false,resolved,sizeof(resolved))) return false;
    BridgeEnv call=bridge_env(); if(!call.env) return false; jstring p=java_string(call.env,resolved);
    if(p) (*call.env)->CallStaticVoidMethod(call.env,platform_class,export_method,activity_ref,p);
    bool ok=bridge_ok(call.env) && p; if(p) (*call.env)->DeleteLocalRef(call.env,p); bridge_done(call); return ok;
}
bool qlcg_android_document_poll(char *kind,size_t kc,char *path,size_t pc,char *error,size_t ec) {
    BridgeEnv call=bridge_env(); if(!call.env) return false; JNIEnv *env=call.env;
    jobjectArray result=(*env)->CallStaticObjectMethod(env,platform_class,poll_method); bool ok=bridge_ok(env) && result;
    char *outs[]={kind,path,error}; size_t caps[]={kc,pc,ec};
    for(int i=0;ok && i<3;i++) { jstring value=(*env)->GetObjectArrayElement(env,result,i); ok=native_string(env,value,outs[i],caps[i]); if(value) (*env)->DeleteLocalRef(env,value); }
    if(result) (*env)->DeleteLocalRef(env,result); bridge_done(call); return ok;
}
#endif
