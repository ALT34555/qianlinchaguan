#include "systems/world/save_fields.h"
#include "platform/platform.h"
#include "game_settings.h"
#include "settings_legacy_v4.h"
#include "math_util.h"
#include "i18n.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>

VegetationDensity qlcg_vegetation_density(int level) {
    static const VegetationDensity levels[QLCG_MAX_VEGETATION_DENSITY + 1] = {
        { 1.0f, 1.0f }, { 1.2f, 1.0f }, { 2.0f, 1.0f },
        { 4.0f, 1.0f }, { 8.0f, 1.0f }, { 16.0f, 1.0f },
        { 20.0f, 1.5f }, { 24.0f, 2.0f }, { 28.0f, 2.5f }, { 32.0f, 3.0f }
    };
    return levels[qlcg_clampi(level, 0, QLCG_MAX_VEGETATION_DENSITY)];
}

/* 返回默认游戏设置（对齐 base） */
GameSettings qlcg_default_settings(void) {
    GameSettings s;
    memset(&s, 0, sizeof(s));
    strncpy(s.profile.name, "赤", sizeof(s.profile.name) - 1);
    s.profile.height_cm  = 170;
    s.profile.main_color = 0xCB3A56u;
    s.profile.trim_color = 0x5D3F51u;
    strncpy(s.ren_ming, "赤", sizeof(s.ren_ming) - 1);
    s.measurement_system    = MEAS_METRIC;
    s.theme                 = THEME_AUTO;
    s.show_top_bar          = true;
    s.crosshair             = CROSSHAIR_DOT;
    s.profile_palette_to_ui = true;
    s.ui_scale              = 1.0f;

    s.safe_x = 0.0f;
    s.safe_y = 0.0f;
    s.text_scale          = 1.0f;
    strncpy(s.font_id, "system", sizeof(s.font_id) - 1);
    s.reduce_motion = false;
    s.binds_magic = QLCG_BINDS_MAGIC;
    qlcg_binds_default(s.binds);
    s.interface_main_color = 0xCB3A56u;
    s.interface_trim_color = 0x5D3F51u;
    s.mirror_reflections   = false;
    strncpy(s.resolution, "auto", sizeof(s.resolution) - 1);
    strncpy(s.locale, "zh-CN", sizeof(s.locale) - 1);
    s.hidpi_scale = 1.0f;
    s.touch_controls = false;
    s.joystick_style = 0;
    s.touch_opacity  = 1.0f;
    qlcg_touch_layout_default(s.touch_layout);
    s.invert_look_x = false;
    s.invert_look_y = false;

    s.graphics_preset     = PRESET_HIGH;
    s.fov                 = 70;
    s.render_distance     = 12;
    s.vegetation_distance = 10;
    s.vegetation_form     = VEG_FORM_RICH;
    s.dense_vegetation    = 2;
    s.frame_rate          = 0;
    s.antialias           = true;
    s.water_reflections   = true;
    s.water_waves         = true;
    s.particles           = true;
    s.weather_atmosphere  = true;
    s.decorative_clouds   = true;
    s.natural_physics     = true;
    s.status_feedback     = true;

    s.view_mode           = VIEW_MODE_2D;
#ifdef __ANDROID__
    s.touch_controls=true;s.frame_rate=30;s.render_distance=4;s.vegetation_distance=0;
    s.antialias=false;s.dense_vegetation=0;s.vegetation_form=VEG_FORM_SIMPLE;
    s.water_reflections=false;
#endif
    return s;
}

/* 恢复默认但保留界面语言 */
void qlcg_reset_settings_keep_locale(GameSettings *s) {
    if (!s) return;
    char keep[sizeof(s->locale)];
    snprintf(keep, sizeof(keep), "%s", qlcg_i18n_get_locale());
    *s = qlcg_default_settings();
    /* 语言包缺失时回退 zh-CN */
    qlcg_i18n_set_locale(keep);
    snprintf(s->locale, sizeof(s->locale), "%s", qlcg_i18n_get_locale());
}

/* 应用 6 档画质预设联动参数（对齐 base） */
void qlcg_apply_graphics_preset(GameSettings *s, GraphicsPreset preset) {
    s->graphics_preset = preset;
    bool vis = (preset == PRESET_BALANCED || preset == PRESET_HIGH || preset == PRESET_EXTREME);
    s->antialias          = vis;
    s->water_reflections  = vis;
    s->water_waves        = (preset == PRESET_EXTREME);
    s->weather_atmosphere = vis;
    s->natural_physics    = vis;
    s->decorative_clouds  = vis;
    s->mirror_reflections = false;
    s->particles          = vis;
    s->status_feedback    = (preset != PRESET_MINIMAL);
    s->vegetation_form    = (preset == PRESET_EXTREME || preset == PRESET_HIGH)
                          ? VEG_FORM_RICH : (vis ? VEG_FORM_NORMAL : VEG_FORM_SIMPLE);
    switch (preset) {
        case PRESET_MINIMAL:
            strncpy(s->resolution, "320x240", sizeof(s->resolution) - 1);
            s->render_distance = 4; s->vegetation_distance = 4;
            s->dense_vegetation = 0; s->frame_rate = 24;
            break;
        case PRESET_LOW:
            strncpy(s->resolution, "1280x720", sizeof(s->resolution) - 1);
            s->render_distance = 6; s->vegetation_distance = 4;
            s->dense_vegetation = 0; s->frame_rate = 30;
            break;
        case PRESET_SMOOTH:
            strncpy(s->resolution, "960x540", sizeof(s->resolution) - 1);
            s->render_distance = 6; s->vegetation_distance = 4;
            s->dense_vegetation = 0; s->frame_rate = 60;
            break;
        case PRESET_BALANCED:
            strncpy(s->resolution, "auto", sizeof(s->resolution) - 1);
            s->render_distance = 12; s->vegetation_distance = 6;
            s->dense_vegetation = 1; s->frame_rate = 60;
            break;
        case PRESET_HIGH:
            strncpy(s->resolution, "1920x1080", sizeof(s->resolution) - 1);
            s->render_distance = 16; s->vegetation_distance = 10;
            s->dense_vegetation = 3; s->frame_rate = 60;
            break;
        case PRESET_EXTREME:
            strncpy(s->resolution, "2560x1440", sizeof(s->resolution) - 1);
            s->render_distance = 20; s->vegetation_distance = 16;
            s->dense_vegetation = 5; s->frame_rate = 120;
            break;
    }
}

void qlcg_touch_layout_default(float out[4][3]) {
    static const float kDefault[4][3] = {
        { 0.0400f, 0.7400f, 1.0f },   /* 摇杆 */
        { 0.8768f, 0.8103f, 1.0f },   /* 冲刺 */
        { 0.9672f, 0.6775f, 1.0f },   /* 跳跃 */
        { 0.8933f, 0.9512f, 1.0f }    /* 下蹲 */
    };
    if (!out) return;
    memcpy(out, kDefault, sizeof(kDefault));
}

static unsigned char s_settings_raw[sizeof(GameSettings)];

/* v4 旧档搬到当前布局；短文件只补可用字节 */
static void settings_migrate_v4(GameSettings *dst, size_t n) {
    const size_t pre = offsetof(GameSettingsV4, binds);
    const size_t old_tail = offsetof(GameSettingsV4, interface_main_color);
    const size_t new_tail = offsetof(GameSettings, interface_main_color);
    const size_t tail_cap = sizeof(GameSettingsV4) - old_tail;
    if (n > pre) memcpy(dst, s_settings_raw, pre);
    size_t bind_avail = n > pre ? n - pre : 0;
    if (bind_avail > QLCG_LEGACY_ACT_COUNT * sizeof(QlBind))
        bind_avail = QLCG_LEGACY_ACT_COUNT * sizeof(QlBind);
    if (bind_avail) memcpy(dst->binds, s_settings_raw + pre, bind_avail);
    if (n > old_tail) {
        size_t tail_avail = n - old_tail;
        if (tail_avail > tail_cap) tail_avail = tail_cap;
        memcpy((unsigned char *)dst + new_tail, s_settings_raw + old_tail, tail_avail);
    }
    dst->binds[ACT_QUICKSAVE].key  = 'P';
    dst->binds[ACT_QUICKSAVE].mods = QL_MOD_CTRL;
}

void qlcg_load_settings_disk(GameSettings *s) {
    if (!s) return;
    FILE *fp = qlcg_platform_fopen("userdata/settings.bin", "rb");
    if (!fp) return;
    /* 以传入值为底：旧文件读不满时，末尾新增字段保留默认 */
    GameSettings tmp = *s;
    char magic[8] = { 0 };
    size_t got = fread(magic, 1, sizeof(magic), fp);
    if (got != sizeof(magic)) {
        fclose(fp);
        return;
    }
    bool old_scale=memcmp(magic,QLCG_SETTINGS_MAGIC,8)!=0;
    if (!memcmp(magic,QLCG_SETTINGS_MAGIC,8) || !memcmp(magic,QLCG_SETTINGS_MAGIC_V6,8)) {
        long end,size=qlcg_file_size(fp);
        bool ok=size>8 && size<65536 && qlcg_section_open(fp,"SETT",size,&end) && end==size;
        QlCodec io=qlcg_codec(fp,true,65536);
        if(ok) qlcg_fields_settings(&io,&tmp);
        ok=ok && io.ok && ftell(fp)==size; fclose(fp);
        if(!ok) return;
    } else {
#ifdef _WIN32
        size_t n=fread(s_settings_raw,1,sizeof(s_settings_raw),fp); fclose(fp);
        if(!memcmp(magic,QLCG_SETTINGS_MAGIC_V4,8)) settings_migrate_v4(&tmp,n);
        else if(!memcmp(magic,QLCG_SETTINGS_MAGIC_V5,8)) { if(n) memcpy(&tmp,s_settings_raw,n); }
        else return;
        tmp.profile.name[sizeof(tmp.profile.name)-1]=0; tmp.ren_ming[sizeof(tmp.ren_ming)-1]=0;
        tmp.font_id[sizeof(tmp.font_id)-1]=0; tmp.locale[sizeof(tmp.locale)-1]=0;
        tmp.resolution[sizeof(tmp.resolution)-1]=0;
#else
        fclose(fp); return;
#endif
    }

    tmp.fov = qlcg_clampi(tmp.fov, 30, 120);
    tmp.render_distance = qlcg_clampi(tmp.render_distance, 4, 32);
    tmp.vegetation_distance = qlcg_clampi(tmp.vegetation_distance, 0, tmp.render_distance);
    tmp.dense_vegetation = qlcg_clampi(tmp.dense_vegetation, 0, QLCG_MAX_VEGETATION_DENSITY);
    tmp.safe_x = qlcg_clampf(tmp.safe_x, 0.0f, 35.0f);
    tmp.safe_y = qlcg_clampf(tmp.safe_y, 0.0f, 35.0f);
    tmp.text_scale  = qlcg_clampf(tmp.text_scale,  0.6f, 4.0f);
    tmp.ui_scale    = qlcg_clampf(tmp.ui_scale,    0.25f, 4.0f);
    if(old_scale) tmp.hidpi_scale*=0.5f;
    tmp.hidpi_scale = qlcg_clampf(tmp.hidpi_scale, 0.5f, 4.0f);
    tmp.touch_opacity = qlcg_clampf(tmp.touch_opacity, 0.1f, 1.0f);
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            float v = tmp.touch_layout[i][k];
            if (v != v) v = 0.0f;                       /* NaN 保护 */
            tmp.touch_layout[i][k] = (k < 2) ? qlcg_clampf(v, 0.0f, 1.0f)
                                             : qlcg_clampf(v, 0.5f, 2.0f);
        }
    }
    tmp.joystick_style = qlcg_clampi(tmp.joystick_style, 0, 7);
    tmp.invert_look_x = tmp.invert_look_x != 0;
    tmp.invert_look_y = tmp.invert_look_y != 0;
    if (tmp.text_scale <= 0.0f) tmp.text_scale = 1.0f;
    {
        bool sane = tmp.font_id[0] != '\0';
        for (const char *p = tmp.font_id; sane && *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7F) sane = false;
        }
        if (!sane) strncpy(tmp.font_id, "system", sizeof(tmp.font_id) - 1);
    }
    {
        bool sane = tmp.locale[0] != '\0';
        for (const char *p = tmp.locale; sane && *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7F) sane = false;
        }
        if (!sane) strncpy(tmp.locale, "zh-CN", sizeof(tmp.locale) - 1);
    }
    /* binds_magic 不符则整表重置为默认 */
    if (tmp.binds_magic != QLCG_BINDS_MAGIC) {
        tmp.binds_magic = QLCG_BINDS_MAGIC;
        qlcg_binds_default(tmp.binds);
    }
    QlBind defaults[ACT_COUNT];
    qlcg_binds_default(defaults);
    for (int i=0;i<ACT_COUNT;i++) {
        if (tmp.binds[i].key==0xE5 || tmp.binds[i].key==0xE7) {
            tmp.binds[i].key=defaults[i].key;
            tmp.binds[i].mods=defaults[i].mods;
        }
    }
    *s = tmp;
}

static bool read_portable_settings(const char *path) {
    FILE *fp=qlcg_platform_fopen(path,"rb"); if(!fp) return false;
    char magic[8]; long end,size=qlcg_file_size(fp);
    bool ok=size>8 && size<65536 && fread(magic,1,8,fp)==8 && !memcmp(magic,QLCG_SETTINGS_MAGIC,8) &&
        qlcg_section_open(fp,"SETT",size,&end) && end==size;
    GameSettings tmp={0}; QlCodec io=qlcg_codec(fp,true,65536);
    if(ok) qlcg_fields_settings(&io,&tmp);
    ok=ok && io.ok && ftell(fp)==size; fclose(fp); return ok;
}
bool qlcg_settings_write(const char *path, const GameSettings *s) {
    if(!path || !s) return false;
    char temporary[1024];
    int n=snprintf(temporary,sizeof(temporary),"%s.new",path);
    if(n<0 || n>=(int)sizeof(temporary)) return false;
    FILE *fp=qlcg_platform_fopen(temporary,"w+b"); if(!fp) return false;
    bool ok=fwrite(QLCG_SETTINGS_MAGIC,1,8,fp)==8;
    long start=qlcg_section_begin(fp,"SETT");
    GameSettings tmp=*s; QlCodec io=qlcg_codec(fp,false,65536);
    qlcg_fields_settings(&io,&tmp);
    ok=ok && start==8 && io.ok && qlcg_section_finish(fp,start) && qlcg_platform_sync(fp);
    if(fclose(fp)) ok=false;
    if(ok) ok=read_portable_settings(temporary);
    if(ok && qlcg_platform_exists(path)) {
        char magic[8]={0}; fp=qlcg_platform_fopen(path,"rb");
        if(!fp || fread(magic,1,8,fp)!=8) ok=false;
        if(fp) fclose(fp);
        if(ok && (!memcmp(magic,QLCG_SETTINGS_MAGIC_V4,8) || !memcmp(magic,QLCG_SETTINGS_MAGIC_V5,8) || !memcmp(magic,QLCG_SETTINGS_MAGIC_V6,8))) {
            bool copied=false; char backup[1024];
            for(int i=0;i<100 && !copied;i++) {
                n=snprintf(backup,sizeof(backup),"%s.legacy-%d",path,i);
                if(n<0 || n>=(int)sizeof(backup)) break;
                if(!qlcg_platform_exists(backup)) { copied=qlcg_platform_copy(path,backup); break; }
            }
            ok=copied;
        }
    }
    if(ok) ok=qlcg_platform_replace(temporary,path)==0;
    if(!ok) qlcg_platform_remove(temporary);
    return ok;
}
void qlcg_save_settings_disk(const GameSettings *s) {
    qlcg_platform_mkdir("userdata");
    qlcg_settings_write("userdata/settings.bin",s);
}

const char *qlcg_graphics_preset_name(GraphicsPreset p) {
    static const char *const KEYS[] = {
        "settings.graphicsPreset.minimal", "settings.graphicsPreset.low",
        "settings.graphicsPreset.smooth",  "settings.graphicsPreset.balanced",
        "settings.graphicsPreset.high",    "settings.graphicsPreset.extreme"
    };
    return tr(KEYS[qlcg_clampi((int)p, 0, 5)]);
}

const char *qlcg_veg_form_name(VegetationForm f) {
    static const char *const KEYS[] = {
        "settings.vegetationForm.simple", "settings.vegetationForm.normal",
        "settings.vegetationForm.rich"
    };
    return tr(KEYS[qlcg_clampi((int)f, 0, 2)]);
}

const char *qlcg_crosshair_name(CrosshairStyle c) {
    static const char *const KEYS[] = {
        "settings.crosshair.none", "settings.crosshair.dot", "settings.crosshair.cross",
        "settings.crosshair.aim",  "settings.crosshair.ring"
    };
    return tr(KEYS[qlcg_clampi((int)c, 0, 4)]);
}

const char *qlcg_theme_name(ThemeMode t) {
    static const char *const KEYS[] = {
        "settings.theme.light", "settings.theme.dark", "settings.theme.auto"
    };
    return tr(KEYS[qlcg_clampi((int)t, 0, 2)]);
}
