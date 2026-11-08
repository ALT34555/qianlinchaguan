#include "ui_core.h"
#include "ui_sprite.h"
#include "../core/math_util.h"
#include "platform/platform.h"
#include "platform/ui_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>


static void ui_page_tick(UiContext *ui);


static const char *UI_VS_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aColor;\n"
    "layout(location=3) in float aMode;\n"
    "uniform vec2 uScreen;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "out float vMode;\n"
    "void main() {\n"
    "    vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreen.y) * 2.0);\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    vUV = aUV;\n"
    "    vColor = aColor;\n"
    "    vMode = aMode;\n"
    "}\n";

static const char *UI_FS_SRC =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "in float vMode;\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uSprite;\n"
    "uniform sampler2D uIcon;\n"
    "uniform sampler2D uBackdrop;\n"
    "uniform vec2 uScreen;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    if (vMode < 0.5) {\n"
    "        FragColor = vColor;\n"
    "    } else if (vMode < 1.5) {\n"
    "        float a = texture(uTex, vUV).a;\n"
    "        FragColor = vec4(vColor.rgb, vColor.a * a);\n"
    "    } else if (vMode < 2.5) {\n"
    "        FragColor = texture(uTex, vUV) * vColor;\n"
    "    } else if (vMode < 3.5) {\n"
    "        FragColor = texture(uSprite, vUV) * vColor;\n"
    "    } else if (vMode < 4.5) {\n"
    "        FragColor = texture(uIcon, vUV) * vColor;\n"
    "    } else if (vMode < 5.5) {\n"
    "        vec2 suv = gl_FragCoord.xy / uScreen;\n"
    "        vec3 bg = texture(uBackdrop, suv).rgb;\n"
    "        float lum = dot(bg, vec3(0.2126, 0.7152, 0.0722));\n"
    "        bg = mix(vec3(lum), bg, 1.28);\n"
    "        FragColor = vec4(mix(bg, vColor.rgb, vColor.a), 1.0);\n"
    "    } else {\n"
    "        vec2 d = (vUV - 0.5) * uScreen;\n"
    "        float t = clamp((length(d) / length(uScreen * 0.5) - 0.5) * 2.0, 0.0, 1.0);\n"
    "        FragColor = vec4(vColor.rgb, vColor.a * t);\n"
    "    }\n"
    "}\n";




/* 令牌派生后映射到调色板快捷字段 */
static void sync_palette_from_tokens(UiContext *ui) {
    const QlColor *c = ui->tk.c;
    ui->pal.bg_canvas      = (uint32_t)ql_to_rgba(c[QL_BG]);
    ui->pal.bg_rail        = (uint32_t)ql_to_rgba(c[QL_BG]);
    ui->pal.bg_card        = (uint32_t)ql_to_rgba(c[QL_SURFACE]);
    ui->pal.bg_card_hover  = (uint32_t)ql_to_rgba(c[QL_SURFACE_HI]);
    ui->pal.bg_panel_glass = (uint32_t)ql_to_rgba(c[QL_SURFACE_GLASS]);
    ui->pal.border_soft    = (uint32_t)ql_to_rgba(c[QL_LINE_SOFT]);
    ui->pal.border_gold    = (uint32_t)ql_to_rgba(c[QL_LINE]);
    ui->pal.text_main      = (uint32_t)ql_to_rgba(c[QL_INK]);
    ui->pal.text_sub       = (uint32_t)ql_to_rgba(c[QL_INK_2]);
    ui->pal.text_muted     = (uint32_t)ql_to_rgba(c[QL_INK_3]);
    ui->pal.accent_tea     = (uint32_t)ql_to_rgba(c[QL_PRIMARY_UI]);
    ui->pal.accent_gold    = (uint32_t)ql_to_rgba(c[QL_GOLD]);
    ui->pal.accent_crimson = (uint32_t)ql_to_rgba(c[QL_HIGHLIGHT]);
}

/* 读系统深浅色偏好（对齐 auto 主题） */


/* 切换茶席日间/夜间配色主题（令牌重算） */
void ui_apply_theme(UiContext *ui, ThemeMode mode) {
    ui->theme = mode;
    bool dark;
    if (mode == THEME_DARK)        dark = true;
    else if (mode == THEME_LIGHT)  dark = false;
    else if (ui->night_override >= 0) dark = ui->night_override != 0;  /* 山川按区块时刻 */
    else                           dark = qlcg_platform_dark();
    if (ui->primary.a <= 0.0f) ui->primary = ql_rgb(0xCB3A56);
    if (ui->accent.a  <= 0.0f) ui->accent  = ql_rgb(0x5D3F51);

    /* 首次直接落地，之后存起点供逐帧插值 */
    if (!ui->tk_ready || ui->reduce_motion) {
        ql_tokens_build(&ui->tk, dark, ui->primary, ui->accent);
        ui->tk_to   = ui->tk;
        ui->tk_from = ui->tk;
        ui->tk_ready = true;
        ui->theme_t  = 1.0f;
    } else {
        ui->tk_from = ui->tk;
        ql_tokens_build(&ui->tk_to, dark, ui->primary, ui->accent);
        ui->theme_t = 0.0f;
    }
    ql_metrics_build(&ui->met, ui->win_w, ui->win_h,
                     ui->user_scale > 0.0f ? ui->user_scale : 1.0f,
                     ui->user_size  > 0.0f ? ui->user_size  : 1.0f,
                     ui->hidpi_mul);
    sync_palette_from_tokens(ui);
}

/* 主辅色接管界面主色（同 adoptPalette） */
void ui_set_palette(UiContext *ui, uint32_t primary_hex, uint32_t accent_hex) {
    ui->primary = ql_rgb(primary_hex);
    ui->accent  = ql_rgb(accent_hex);
    ui_apply_theme(ui, ui->theme);
}

/* 昼夜覆盖：值变了才重算，靠插值过渡 */
void ui_set_night_override(UiContext *ui, int v) {
    v = (v < 0) ? -1 : (v ? 1 : 0);
    if (ui->tk_ready && ui->night_override == v) return;
    ui->night_override = v;
    ui_apply_theme(ui, ui->theme);
}

bool ui_gpu_restore(UiContext *ui) {
    ui->prog = qlcg_gl_compile_program(UI_VS_SRC, UI_FS_SRC);
    if(!ui->prog) return false;
    ui_backdrop_init(ui);
    glGenVertexArrays(1, &ui->vao);
    glGenBuffers(1, &ui->vbo);
    glBindVertexArray(ui->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ui->verts), NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void *)offsetof(UiVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void *)offsetof(UiVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), (void *)offsetof(UiVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void *)offsetof(UiVertex, mode));

    glGenTextures(1, &ui->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, ui->atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, UI_ATLAS_W, UI_ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


    ui->shelf_x = 4;
    ui->shelf_y = 4;
    ui->shelf_row_h = 0;

    qlcg_platform_teacup(ui, L"content\\assets\\ui\\icons\\qianlin_teacup.png");
    ui_sprite_load_all(ui);

    return true;
}

/* 初始化 UI 上下文与字体图集 */
bool ui_init(UiContext *ui, int w, int h) {
    memset(ui, 0, sizeof(*ui));
    ui->drag_slider_id = -1;
    ui->night_override = -1;   /* 默认不覆盖：跟随设置/系统 */
    ui->win_w = w;
    ui->win_h = h;
    ui_apply_theme(ui, THEME_LIGHT);

    if(!qlcg_platform_font_init(ui) || !ui_gpu_restore(ui)) return false;
    ui->page_enter=ui->global_alpha=1.0f;
    ui->capture_action=-1;
    /* 初始化飘落花瓣粒子 */
    for (int i = 0; i < UI_PETAL_COUNT; i++) {
        uint32_t hsh = qlcg_hash32((uint32_t)(i + 101));
        ui->petals[i].x     = (float)(hsh % (uint32_t)(w > 0 ? w : 1280));
        ui->petals[i].y     = (float)((hsh >> 8) % (uint32_t)(h > 0 ? h : 720));
        ui->petals[i].vx    = -12.0f - (float)(hsh & 15);
        ui->petals[i].vy    = 18.0f + (float)((hsh >> 4) & 15);
        ui->petals[i].size  = 4.0f + (float)(hsh & 3);
        ui->petals[i].rot   = (float)(hsh & 255) * 0.02f;
        ui->petals[i].vrot  = 0.8f + (float)(hsh & 7) * 0.2f;
        ui->petals[i].alpha = 0.25f + (float)(hsh & 7) * 0.05f;
    }
    return true;
}

/* 释放 UI 资源 */
void ui_gpu_release(UiContext *ui) {
    if (ui->atlas_tex) qlcg_gl_delete_textures(1, &ui->atlas_tex);
    ui_sprite_release();
    ui_backdrop_release(ui);
    if (ui->vbo) qlcg_gl_delete_buffers(1, &ui->vbo);
    if (ui->vao) qlcg_gl_delete_vertex_arrays(1, &ui->vao);
    if (ui->prog) qlcg_gl_delete_program(ui->prog);
    ui->prog=ui->vao=ui->vbo=ui->atlas_tex=ui->cur_sprite_tex=0;
    ui->has_teacup=false; ui->vert_count=0;
    memset(ui->slots,0,sizeof(ui->slots));
}
void ui_shutdown(UiContext *ui) {
    ui_gpu_release(ui);
    qlcg_platform_font_shutdown(ui);
}

/* 开始一帧 UI 批处理 */
void ui_begin_frame(UiContext *ui, int w, int h) {
    ui->pix_w = (w > 1) ? w : 1;
    ui->pix_h = (h > 1) ? h : 1;
    ui->win_w = ui->pix_w;
    ui->win_h = ui->pix_h;
    /* 背景模糊只在场景画完后重抓，跨帧不复用，避免残影 */
    ui->backdrop_ready = false;
    ui_backdrop_resize(ui, ui->win_w, ui->win_h);
    ui->safe_x_px = (float)ui->win_w * qlcg_clampf(ui->safe_x_pct, 0.0f, 35.0f) * 0.01f;
    ui->safe_y_px = (float)ui->win_h * qlcg_clampf(ui->safe_y_pct, 0.0f, 35.0f) * 0.01f;
    ui->safe_edges[0]=ui->safe_x_px+ui->system_edges[0];
    ui->safe_edges[1]=ui->safe_y_px+ui->system_edges[1];
    ui->safe_edges[2]=ui->safe_x_px+ui->system_edges[2];
    ui->safe_edges[3]=ui->safe_y_px+ui->system_edges[3];
    ui->safe_x_px=ui->safe_edges[0];
    ui->safe_y_px=ui->safe_edges[1];
    ui->vert_count = 0;
    /* 裁剪栈与滚动偏移每帧归零，否则整片被裁/位移 */
    ui->clip_depth = 0;
    ui->input_blocked = false;
    ui->select_input = false;
    ui->toast_blocked = false;
    ui->ime_caret_valid = false;
    ui->global_dx  = 0.0f;
    ui->global_dy  = 0.0f;
    ui->frame_idx++;
    ui->slider_seq = 0;
    if (!ui->mouse_down) ui->drag_slider_id = -1;
    double now = qlcg_now_ms();
    if (ui->last_ms <= 0.0) ui->last_ms = now;
    float dt = (float)((now - ui->last_ms) * 0.001);
    ui->last_ms = now;
    if (ui->fixed_dt > 0.0f) dt = ui->fixed_dt;
    ui->dt = qlcg_clampf(dt, 0.0005f, 0.1f);
    ui->time += ui->dt;
    /* 双击判定：350ms 内、位移 6px 以内 */
    ui->mouse_dblclicked = ui->pointer_touch && ui->touch_double_tap;
    if (!ui->pointer_touch && ui->mouse_clicked) {
        bool close_in_time = (now - ui->last_click_ms) < 350.0 &&
                             abs(ui->mouse_x - ui->last_click_x) < 6 &&
                             abs(ui->mouse_y - ui->last_click_y) < 6;
        if (close_in_time && ui->last_click_ms > 0.0) {
            ui->mouse_dblclicked = true;
            ui->last_click_ms = 0.0;   /* 抑制三击连触 */
        } else {
            ui->last_click_ms = now;
        }
        ui->last_click_x = ui->mouse_x;
        ui->last_click_y = ui->mouse_y;
    }
    /* 主题过渡：令牌插值420ms，取色自动跟随 */
    if (ui->theme_t < 1.0f) {
        ui->theme_t += ui->dt / 0.42f;
        if (ui->theme_t > 1.0f) ui->theme_t = 1.0f;
        float k = ui_ease(ui->theme_t);
        for (int i = 0; i < QL_TOKEN_COUNT; i++) {
            ui->tk.c[i] = ql_mix(ui->tk_from.c[i], 1.0f - k, ui->tk_to.c[i], k);
        }
        ui->tk.dark = (k > 0.5f) ? ui->tk_to.dark : ui->tk_from.dark;
        sync_palette_from_tokens(ui);
    }
    ui_page_tick(ui);
    ui_sprite_poll_reload(ui);
    ql_metrics_build(&ui->met, ui->win_w, ui->win_h,
                     ui->user_scale > 0.0f ? ui->user_scale : 1.0f,
                     ui->user_size  > 0.0f ? ui->user_size  : 1.0f,
                     ui->hidpi_mul);
}

/* 提交当前批次顶点 */
void ui_flush(UiContext *ui) {
    if (ui->vert_count <= 0) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(ui->prog);
    glUniform2f(qlcg_gl_uniform(ui->prog, "uScreen"), (float)ui->win_w, (float)ui->win_h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ui->atlas_tex);
    glUniform1i(qlcg_gl_uniform(ui->prog, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, ui->cur_sprite_tex ? ui->cur_sprite_tex : ui->atlas_tex);
    glUniform1i(qlcg_gl_uniform(ui->prog, "uSprite"), 1);
    /* 2 号纹理单元当前无用途，绑字形图集避免未绑定采样 */
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, ui->atlas_tex);
    glUniform1i(qlcg_gl_uniform(ui->prog, "uIcon"), 2);
    /* 磨砂采样图；未就绪退回图集防越界 */
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, ui->backdrop_tex ? ui->backdrop_tex : ui->atlas_tex);
    glUniform1i(qlcg_gl_uniform(ui->prog, "uBackdrop"), 3);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(ui->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(ui->vert_count * sizeof(UiVertex)), ui->verts);
    glDrawArrays(GL_TRIANGLES, 0, ui->vert_count);
    ui->vert_count = 0;
}

/* 切换 sprite 纹理：与当前不同则先提交已攒批次 */
void ui_use_sprite_tex(UiContext *ui, GLuint tex) {
    if (!tex || ui->cur_sprite_tex == tex) return;
    ui_flush(ui);
    ui->cur_sprite_tex = tex;
}

/* 结束一帧 UI 批处理 */
void ui_end_frame(UiContext *ui) {
    ui_flush(ui);
    ui->mouse_clicked = false;
    ui->right_clicked = false;
    ui->touch_double_tap=false;
    ui->wheel_delta   = 0;
}

/* 全局透明度（入场淡入） */
static inline uint32_t ui_fade(UiContext *ui, uint32_t c) {
    if (ui->global_alpha >= 0.999f) return c;
    uint32_t a = (uint32_t)((float)((c >> 24) & 0xFF) * ui->global_alpha);
    return (c & 0x00FFFFFFu) | (a << 24);
}

/* mode：6 径向，5 玻璃，其余见绘制函数 */
static void push_quad(UiContext *ui, float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1,
                      uint32_t c_top, uint32_t c_bot, float mode) {
    if (ui->vert_count + 6 >= UI_MAX_VERTS) ui_flush(ui);
    x0 += ui->global_dx; x1 += ui->global_dx;
    y0 += ui->global_dy; y1 += ui->global_dy;
    c_top = ui_fade(ui, c_top);
    c_bot = ui_fade(ui, c_bot);
    UiVertex *v = &ui->verts[ui->vert_count];
    v[0] = (UiVertex){x0, y0, u0, v0, c_top, mode};
    v[1] = (UiVertex){x1, y0, u1, v0, c_top, mode};
    v[2] = (UiVertex){x1, y1, u1, v1, c_bot, mode};
    v[3] = (UiVertex){x0, y0, u0, v0, c_top, mode};
    v[4] = (UiVertex){x1, y1, u1, v1, c_bot, mode};
    v[5] = (UiVertex){x0, y1, u0, v1, c_bot, mode};
    ui->vert_count += 6;
}

void ui_draw_rect(UiContext *ui, float x, float y, float w, float h, uint32_t color) {
    push_quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, color, color, 0.0f);
}

/* 玻璃底板：有模糊就用，否则半透明 */
void ui_draw_rect_glass(UiContext *ui, float x, float y, float w, float h, uint32_t tint) {
    bool glass = ui->backdrop_ready && ui->backdrop_tex && !ui->reduce_motion;
    float mode = glass ? 5.0f : 0.0f;
    push_quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, tint, tint, mode);
}

/* 绕中心旋转的四边形：图标用旋转代替多方向素材 */
static void push_quad_rot(UiContext *ui, float cx, float cy, float hw, float hh,
                          float cos_a, float sin_a,
                          float u0, float v0, float u1, float v1,
                          uint32_t color, float mode) {
    if (ui->vert_count + 6 >= UI_MAX_VERTS) ui_flush(ui);
    cx += ui->global_dx;
    cy += ui->global_dy;
    color = ui_fade(ui, color);
    const float lx[4] = { -hw,  hw, hw, -hw };
    const float ly[4] = { -hh, -hh, hh,  hh };
    float px[4], py[4];
    for (int i = 0; i < 4; i++) {
        px[i] = cx + lx[i] * cos_a - ly[i] * sin_a;
        py[i] = cy + lx[i] * sin_a + ly[i] * cos_a;
    }
    UiVertex *v = &ui->verts[ui->vert_count];
    v[0] = (UiVertex){px[0], py[0], u0, v0, color, mode};
    v[1] = (UiVertex){px[1], py[1], u1, v0, color, mode};
    v[2] = (UiVertex){px[2], py[2], u1, v1, color, mode};
    v[3] = (UiVertex){px[0], py[0], u0, v0, color, mode};
    v[4] = (UiVertex){px[2], py[2], u1, v1, color, mode};
    v[5] = (UiVertex){px[3], py[3], u0, v1, color, mode};
    ui->vert_count += 6;
}

void ui_draw_sprite_rect_rot(UiContext *ui, float cx, float cy, float w, float h,
                             float rad, float u0, float v0, float u1, float v1,
                             uint32_t tint) {
    push_quad_rot(ui, cx, cy, w * 0.5f, h * 0.5f, cosf(rad), sinf(rad),
                  u0, v0, u1, v1, tint, 3.0f);
}

void ui_draw_rect_grad(UiContext *ui, float x, float y, float w, float h, uint32_t c_top, uint32_t c_bot) {
    push_quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, c_top, c_bot, 0.0f);
}

/* 径向晕影：中心 50% 透明，向外渐深 */
void ui_draw_rect_radial(UiContext *ui, float x, float y, float w, float h, uint32_t color) {
    push_quad(ui, x, y, x + w, y + h, 0, 0, 1, 1, color, color, 6.0f);
}

void ui_draw_border(UiContext *ui, float x, float y, float w, float h, float thick, uint32_t color) {
    ui_draw_rect(ui, x, y, w, thick, color);
    ui_draw_rect(ui, x, y + h - thick, w, thick, color);
    ui_draw_rect(ui, x, y + thick, thick, h - 2.0f * thick, color);
    ui_draw_rect(ui, x + w - thick, y + thick, thick, h - 2.0f * thick, color);
}

void ui_draw_line(UiContext *ui, float x0, float y0, float x1, float y1, float thick, uint32_t color) {
    x0 += ui->global_dx; x1 += ui->global_dx;
    y0 += ui->global_dy; y1 += ui->global_dy;
    color = ui_fade(ui, color);
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = hypotf(dx, dy);
    if (len < 0.01f) return;
    float nx = -dy / len * (thick * 0.5f);
    float ny =  dx / len * (thick * 0.5f);
    if (ui->vert_count + 6 >= UI_MAX_VERTS) ui_flush(ui);
    UiVertex *v = &ui->verts[ui->vert_count];
    v[0] = (UiVertex){x0 + nx, y0 + ny, 0, 0, color, 0.0f};
    v[1] = (UiVertex){x1 + nx, y1 + ny, 0, 0, color, 0.0f};
    v[2] = (UiVertex){x1 - nx, y1 - ny, 0, 0, color, 0.0f};
    v[3] = (UiVertex){x0 + nx, y0 + ny, 0, 0, color, 0.0f};
    v[4] = (UiVertex){x1 - nx, y1 - ny, 0, 0, color, 0.0f};
    v[5] = (UiVertex){x0 - nx, y0 - ny, 0, 0, color, 0.0f};
    ui->vert_count += 6;
}

/* 凸多边形（三角扇），用于书签形等非矩形 */
void ui_draw_poly(UiContext *ui, const float *xs, const float *ys, int n, uint32_t color) {
    if (n < 3) return;
    for (int i = 1; i + 1 < n; i++) {
        if (ui->vert_count + 3 >= UI_MAX_VERTS) ui_flush(ui);
        UiVertex *v = &ui->verts[ui->vert_count];
        uint32_t c = ui_fade(ui, color);
        v[0] = (UiVertex){xs[0] + ui->global_dx, ys[0] + ui->global_dy, 0, 0, c, 0.0f};
        v[1] = (UiVertex){xs[i] + ui->global_dx, ys[i] + ui->global_dy, 0, 0, c, 0.0f};
        v[2] = (UiVertex){xs[i + 1] + ui->global_dx, ys[i + 1] + ui->global_dy, 0, 0, c, 0.0f};
        ui->vert_count += 3;
    }
}

void ui_draw_circle(UiContext *ui, float cx, float cy, float r, uint32_t color) {
    cx += ui->global_dx;
    cy += ui->global_dy;
    color = ui_fade(ui, color);
    int segs = (r > 18.0f) ? 20 : 16;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i * 6.283185f / (float)segs;
        float a1 = (float)(i + 1) * 6.283185f / (float)segs;
        if (ui->vert_count + 3 >= UI_MAX_VERTS) ui_flush(ui);
        UiVertex *v = &ui->verts[ui->vert_count];
        v[0] = (UiVertex){cx, cy, 0, 0, color, 0.0f};
        v[1] = (UiVertex){cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, color, 0.0f};
        v[2] = (UiVertex){cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, color, 0.0f};
        ui->vert_count += 3;
    }
}

/* 绘制封面茶杯图标与三缕袅袅茶烟（对齐 t */
void ui_draw_teacup(UiContext *ui, float x, float y, float w, float h, float anim_t) {
    /* 绘制三缕茶烟 */
    for (int i = 0; i < 3; i++) {
        float base_x = x + w * (0.36f + (float)i * 0.14f);
        float base_y = y + h * 0.16f;
        for (int s = 0; s < 10; s++) {
            float t0 = (float)s / 10.0f;
            float t1 = (float)(s + 1) / 10.0f;
            float sx0 = base_x + sinf(anim_t * 1.8f + (float)i * 1.5f + t0 * 4.2f) * (5.0f + t0 * 10.0f);
            float sy0 = base_y - t0 * 52.0f;
            float sx1 = base_x + sinf(anim_t * 1.8f + (float)i * 1.5f + t1 * 4.2f) * (5.0f + t1 * 10.0f);
            float sy1 = base_y - t1 * 52.0f;
            uint8_t a = (uint8_t)((1.0f - t0) * 85.0f);
            uint32_t sc = ui->tk.dark ? ui_rgba(210, 204, 192, a) : ui_rgba(148, 136, 118, a);
            ui_draw_line(ui, sx0, sy0, sx1, sy1, 2.2f * (1.0f - t0 * 0.5f), sc);
        }
    }

    if (ui->has_teacup) {
        push_quad(ui, x, y, x + w, y + h,
                  ui->cup_u0, ui->cup_v0, ui->cup_u1, ui->cup_v1,
                  ui_rgba(255, 255, 255, 245), ui_rgba(255, 255, 255, 245), 2.0f);
    } else {
        ui_draw_circle(ui, x + w * 0.5f, y + h * 0.55f, w * 0.34f, ui->pal.accent_tea);
    }
}

/* 更新并绘制飘落花瓣粒子 */
void ui_update_and_draw_petals(UiContext *ui, float dt, float area_x, float area_y,
                               float area_w, float area_h) {
    for (int i = 0; i < UI_PETAL_COUNT; i++) {
        TeaPetal *pt = &ui->petals[i];
        pt->x += pt->vx * dt;
        pt->y += pt->vy * dt;
        pt->rot += pt->vrot * dt;
        if (pt->y > area_y + area_h + 12.0f || pt->x < area_x - 12.0f) {
            pt->x = area_x + (float)((qlcg_hash32(ui->frame_idx + (uint32_t)i * 31u)) % (uint32_t)(area_w > 10 ? area_w : 800));
            pt->y = area_y - 8.0f;
        }
        uint8_t a = (uint8_t)(pt->alpha * 255.0f);
        uint32_t c = ui_rgba((uint8_t)qlcg_clampf(ui->tk.c[QL_PETAL].r * 255.0f, 0, 255),
                             (uint8_t)qlcg_clampf(ui->tk.c[QL_PETAL].g * 255.0f, 0, 255),
                             (uint8_t)qlcg_clampf(ui->tk.c[QL_PETAL].b * 255.0f, 0, 255), a);
        float dx = cosf(pt->rot) * pt->size;
        float dy = sinf(pt->rot) * pt->size;
        ui_draw_line(ui, pt->x - dx, pt->y - dy, pt->x + dx, pt->y + dy, pt->size * 0.85f, c);
    }
}

/* 按当前字体族重建固定字号并清空烘焙缓存 */


/* 切换字体族：重建字体、作废旧烘焙区 */
void ui_set_font_family(UiContext *ui, const wchar_t *display,
                        const wchar_t *body, const wchar_t *mono) {
    if (display && display[0]) swprintf(ui->family_display, 64, L"%ls", display);
    if (body    && body[0])    swprintf(ui->family_body, 64, L"%ls", body);
    if (mono    && mono[0])    swprintf(ui->family_mono, 64, L"%ls", mono);
    qlcg_platform_font_rebuild(ui);
    ui->shelf_x = 4;
    ui->shelf_y = ui->has_teacup ? (ui->cup_h + 12) : 4;
    ui->shelf_row_h = 0;
}

/* 按族/字号/字重取字体（含缓存） */


/* 旧字体角色映射到字号 */
static void legacy_font_map(const UiContext *ui, UiFontId id, int *face, int *px, bool *bold) {
    float s = ui->met.scale > 0.0f ? ui->met.scale : 1.0f;
    switch (id) {
        case FONT_TITLE:    *face = UI_FACE_DISPLAY; *px = (int)(29.0f * s); *bold = false; break;
        case FONT_SUBTITLE: *face = UI_FACE_DISPLAY; *px = (int)(18.0f * s); *bold = false; break;
        case FONT_BODY:     *face = UI_FACE_BODY;    *px = (int)(13.5f * s); *bold = false; break;
        case FONT_BOLD:     *face = UI_FACE_BODY;    *px = (int)(15.0f * s); *bold = true;  break;
        case FONT_SMALL:    *face = UI_FACE_BODY;    *px = (int)(11.5f * s); *bold = false; break;
        case FONT_MONO:
        default:            *face = UI_FACE_MONO;    *px = (int)(10.0f * s); *bold = false; break;
    }
}

/* 查找或烘焙 UTF-8 文本到共享图集 */
static UiTextSlot *get_text_slot(UiContext *ui, int face, int px, bool bold, const char *text) {
    if (!text || !text[0]) return NULL;
    uint32_t h = qlcg_hash_str(text) ^ ((uint32_t)face * 0x9E3779B9u)
               ^ ((uint32_t)px * 0x85EBCA6Bu) ^ (bold ? 0xC2B2AE35u : 0u);
    int idx = (int)(h % UI_CACHE_SLOTS);
    for (int probe = 0; probe < 8; probe++) {
        int slot_i = (idx + probe) % UI_CACHE_SLOTS;
        UiTextSlot *s = &ui->slots[slot_i];
        if (s->used && s->hash == h && s->face == face && s->px == px && s->bold == bold &&
            strcmp(s->text, text) == 0) {
            s->last_frame = ui->frame_idx;
            return s;
        }
    }

    int tw = 0, th = 0;
    uint8_t *upload = NULL;
    if (!qlcg_platform_text_raster(ui, face, px, bold, text, &upload, &tw, &th)) return NULL;

    if (ui->shelf_x + tw + 2 >= UI_ATLAS_W) {
        ui->shelf_x = 4;
        ui->shelf_y += ui->shelf_row_h + 2;
        ui->shelf_row_h = 0;
    }
    if (ui->shelf_y + th + 2 >= UI_ATLAS_H) {
        ui_flush(ui);
        memset(ui->slots, 0, sizeof(ui->slots));
        ui->shelf_x = 4;
        ui->shelf_y = ui->has_teacup ? (ui->cup_h + 12) : 4;
        ui->shelf_row_h = 0;
    }

    glBindTexture(GL_TEXTURE_2D, ui->atlas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, ui->shelf_x, ui->shelf_y, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, upload);
    free(upload);

    UiTextSlot *target = &ui->slots[idx];
    target->used = true;
    target->hash = h;
    target->face = face;
    target->px   = px;
    target->bold = bold;
    snprintf(target->text, sizeof(target->text), "%s", text);
    target->ax = ui->shelf_x;
    target->ay = ui->shelf_y;
    target->aw = tw;
    target->ah = th;
    target->u0 = (float)ui->shelf_x / (float)UI_ATLAS_W;
    target->v0 = (float)ui->shelf_y / (float)UI_ATLAS_H;
    target->u1 = (float)(ui->shelf_x + tw) / (float)UI_ATLAS_W;
    target->v1 = (float)(ui->shelf_y + th) / (float)UI_ATLAS_H;
    target->last_frame = ui->frame_idx;

    ui->shelf_x += tw + 2;
    if (th > ui->shelf_row_h) ui->shelf_row_h = th;
    return target;
}

int ui_text_width_px(UiContext *ui, UiFaceKind face, float px, bool bold, const char *text) {
    UiTextSlot *s = get_text_slot(ui, (int)face, (int)(px + 0.5f), bold, text);
    return s ? s->aw : 0;
}

int ui_text_width(UiContext *ui, UiFontId font, const char *text) {
    int face, px;
    bool bold;
    legacy_font_map(ui, font, &face, &px, &bold);
    return ui_text_width_px(ui, (UiFaceKind)face, (float)px, bold, text);
}

void ui_draw_text_px(UiContext *ui, UiFaceKind face, float px, bool bold, float x, float y,
                     const char *text, uint32_t color) {
    UiTextSlot *s = get_text_slot(ui, (int)face, (int)(px + 0.5f), bold, text);
    if (!s) return;
    float rx = floorf(x);
    float ry = floorf(y);
    float bottom = ry + (float)s->ah;
    uint32_t top_color = color, bottom_color = color;
    float fade_span = ui->text_fade_bottom - ui->text_fade_top;
    if (fade_span > 0.0f && bottom > ui->text_fade_top) {
        float top_alpha = qlcg_clampf((ui->text_fade_bottom - ry) / fade_span, 0.0f, 1.0f);
        float bottom_alpha = qlcg_clampf((ui->text_fade_bottom - bottom) / fade_span, 0.0f, 1.0f);
        uint32_t alpha = color >> 24;
        uint32_t rgb = color & 0x00FFFFFFu;
        top_color = rgb | ((uint32_t)(alpha * top_alpha) << 24);
        bottom_color = rgb | ((uint32_t)(alpha * bottom_alpha) << 24);
    }
    push_quad(ui, rx, ry, rx + (float)s->aw, bottom,
              s->u0, s->v0, s->u1, s->v1, top_color, bottom_color, 1.0f);
}

void ui_draw_text(UiContext *ui, UiFontId font, float x, float y, const char *text, uint32_t color) {
    int face, px;
    bool bold;
    legacy_font_map(ui, font, &face, &px, &bold);
    ui_draw_text_px(ui, (UiFaceKind)face, (float)px, bold, x, y, text, color);
}

void ui_draw_text_px_center(UiContext *ui, UiFaceKind face, float px, bool bold, float cx, float y,
                            const char *text, uint32_t color) {
    int w = ui_text_width_px(ui, face, px, bold, text);
    ui_draw_text_px(ui, face, px, bold, cx - (float)w * 0.5f, y, text, color);
}

void ui_draw_text_px_right(UiContext *ui, UiFaceKind face, float px, bool bold, float rx, float y,
                           const char *text, uint32_t color) {
    int w = ui_text_width_px(ui, face, px, bold, text);
    ui_draw_text_px(ui, face, px, bold, rx - (float)w, y, text, color);
}

float ui_line_height(UiFaceKind face, float px) {
    return px * (face == UI_FACE_DISPLAY ? 1.22f : 1.62f);
}

void ui_draw_text_center(UiContext *ui, UiFontId font, float cx, float y, const char *text, uint32_t color) {
    int face, px;
    bool bold;
    legacy_font_map(ui, font, &face, &px, &bold);
    ui_draw_text_px_center(ui, (UiFaceKind)face, (float)px, bold, cx, y, text, color);
}

void ui_draw_text_right(UiContext *ui, UiFontId font, float rx, float y, const char *text, uint32_t color) {
    int face, px;
    bool bold;
    legacy_font_map(ui, font, &face, &px, &bold);
    ui_draw_text_px_right(ui, (UiFaceKind)face, (float)px, bold, rx, y, text, color);
}

void ui_draw_atlas_rect(UiContext *ui, float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1, uint32_t tint) {
    push_quad(ui, x, y, x + w, y + h, u0, v0, u1, v1, tint, tint, 2.0f);
}

/* sprite 矩形：mode 3.0 与字形图集区分 */
void ui_draw_sprite_rect(UiContext *ui, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1, uint32_t tint) {
    push_quad(ui, x, y, x + w, y + h, u0, v0, u1, v1, tint, tint, 3.0f);
}

/* PNG 原尺寸上传为纹理，不缩放 */


/* 内部折行实现：draw=false 仅统计行数 */
static int text_wrapped_impl(UiContext *ui, UiFaceKind face, float px, bool bold,
                             float x, float y, float max_w, float line_h,
                             const char *text, uint32_t color, bool draw) {
    if (!text || !text[0] || max_w <= 0.0f) return 0;
    char line[256];
    int lines = 0;
    const char *p = text;
    float cy = y;
    while (*p && lines < 64) {
        int len = 0;
        int space_len = -1;
        const char *space_p = NULL;
        float wid = 0.0f;
        line[0] = '\0';
        while (*p) {
            unsigned char c = (unsigned char)*p;
            if (c == '\n') {
                p++;
                break;
            }
            int step = 1;
            if ((c & 0xE0) == 0xC0) step = 2;
            else if ((c & 0xF0) == 0xE0) step = 3;
            else if ((c & 0xF8) == 0xF0) step = 4;
            char glyph[5] = {0};
            memcpy(glyph, p, (size_t)step);
            /* 扣除图集单字槽 3px 边距误差 */
            float gw = qlcg_maxf(1.0f, (float)ui_text_width_px(ui, face, px, bold, glyph) - 3.0f);
            if (wid + gw > max_w && len > 0) {
                /* 英文单词跨行时回退到最近空格 */
                if (step == 1 && isalnum(c) && space_len > 0 && space_p) {
                    len = space_len;
                    line[len] = '\0';
                    p = space_p;
                }
                break;
            }
            for (int i = 0; i < step && len < 250; i++) line[len++] = p[i];
            line[len] = '\0';
            wid += gw;
            p += step;
            if (c == ' ') {
                space_len = len - 1;
                space_p   = p;
            }
        }
        if (len > 0 || *p == '\0') {
            if (draw && len > 0) {
                ui_draw_text_px(ui, face, px, bold, x, cy, line, color);
            }
            cy += line_h;
            lines++;
            if (!*p) break;
        } else {
            break;
        }
        while (*p == ' ') p++;
    }
    return lines;
}

int ui_measure_text_wrapped(UiContext *ui, UiFaceKind face, float px, bool bold,
                            float max_w, const char *text) {
    return text_wrapped_impl(ui, face, px, bold, 0.0f, 0.0f, max_w, 0.0f, text, 0, false);
}

int ui_draw_text_wrapped(UiContext *ui, UiFaceKind face, float px, bool bold,
                         float x, float y, float max_w, float line_h,
                         const char *text, uint32_t color) {
    return text_wrapped_impl(ui, face, px, bold, x, y, max_w, line_h, text, color, true);
}

/* 单行超宽截断并追加省略号 */
void ui_draw_text_ellipsis(UiContext *ui, UiFaceKind face, float px, bool bold,
                           float x, float y, float max_w, int align,
                           const char *text, uint32_t color) {
    if (!text || !text[0] || max_w <= 2.0f) return;
    int full_w = ui_text_width_px(ui, face, px, bold, text);
    if ((float)full_w <= max_w + 2.0f) {
        if (align == 1) ui_draw_text_px_center(ui, face, px, bold, x + max_w * 0.5f, y, text, color);
        else if (align == 2) ui_draw_text_px_right(ui, face, px, bold, x + max_w, y, text, color);
        else ui_draw_text_px(ui, face, px, bold, x, y, text, color);
        return;
    }
    char buf[256];
    int len = 0;
    const char *p = text;
    float dots_w = (float)ui_text_width_px(ui, face, px, bold, "…");
    float limit = qlcg_maxf(0.0f, max_w - dots_w);
    float wid = 0.0f;
    while (*p && len < 248) {
        unsigned char c = (unsigned char)*p;
        int step = 1;
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        char glyph[5] = {0};
        memcpy(glyph, p, (size_t)step);
        float gw = qlcg_maxf(1.0f, (float)ui_text_width_px(ui, face, px, bold, glyph) - 3.0f);
        if (wid + gw > limit && len > 0) break;
        if (wid + gw > max_w) break;
        for (int i = 0; i < step && len < 248; i++) buf[len++] = p[i];
        wid += gw;
        p += step;
    }
    if (len > 0 && limit > 2.0f) {
        memcpy(buf + len, "\xE2\x80\xA6", 4);
    } else {
        buf[len] = '\0';
    }
    if (!buf[0]) return;
    if (align == 1) ui_draw_text_px_center(ui, face, px, bold, x + max_w * 0.5f, y, buf, color);
    else if (align == 2) ui_draw_text_px_right(ui, face, px, bold, x + max_w, y, buf, color);
    else ui_draw_text_px(ui, face, px, bold, x, y, buf, color);
}

/* 动效内核：曲线与时长对齐 base */

/* cubic-bezier(.16,1,.3,1)≈ */
float ui_ease_out(float t) {
    t = qlcg_clampf(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u * u;
}

/* cubic-bezier(.2,.8,.2,1)≈ */
float ui_ease(float t) {
    t = qlcg_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t) * 0.35f + t * 0.65f;
}

/* 回弹 bezier(.34,1.42,.52,1)≈ */
float ui_ease_spring(float t) {
    t = qlcg_clampf(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u * u + sinf(t * QLCG_PI) * 0.12f;
}

/* 按 id 取控件过渡状态 */
UiMotion *ui_motion(UiContext *ui, uint32_t id) {
    uint32_t slot = id % UI_MOTION_SLOTS;
    for (int i = 0; i < 8; i++) {
        UiMotion *m = &ui->motions[(slot + (uint32_t)i) % UI_MOTION_SLOTS];
        if (m->id == id) return m;
        if (m->id == 0) {
            m->id = id;
            m->hover = m->press = m->toggle = 0.0f;
            return m;
        }
    }
    return &ui->motions[slot];
}

/* 指数平滑逼近目标 */
static float approach(float v, float target, float dt, float speed) {
    float k = 1.0f - expf(-dt * speed);
    return v + (target - v) * k;
}

/* 悬停/按下过渡值 */
UiMotion *ui_motion_step(UiContext *ui, uint32_t id, bool hovered, bool pressed, bool toggled) {
    UiMotion *m = ui_motion(ui, id);
    if (ui->reduce_motion) {                 /* 直接跳到目标，不做过渡 */
        m->hover  = hovered ? 1.0f : 0.0f;
        m->press  = pressed ? 1.0f : 0.0f;
        m->toggle = toggled ? 1.0f : 0.0f;
        return m;
    }
    m->hover = approach(m->hover, hovered ? 1.0f : 0.0f, ui->dt, 18.0f);
    m->press = approach(m->press, pressed ? 1.0f : 0.0f, ui->dt, 30.0f);
    m->toggle = approach(m->toggle, toggled ? 1.0f : 0.0f, ui->dt, 16.0f);
    return m;
}

/* 压入一枚水波 */
void ui_ripple_push(UiContext *ui, float x, float y, float radius) {
    if (ui->reduce_motion) return;
    for (int i = 0; i < UI_RIPPLE_SLOTS; i++) {
        if (!ui->ripples[i].active) {
            ui->ripples[i].active = true;
            ui->ripples[i].x = x;
            ui->ripples[i].y = y;
            ui->ripples[i].t = 0.0f;
            ui->ripples[i].radius = radius;
            return;
        }
    }
}

/* 绘制存活的水波（620ms） */
void ui_ripple_draw(UiContext *ui, uint32_t color) {
    for (int i = 0; i < UI_RIPPLE_SLOTS; i++) {
        UiRipple *r = &ui->ripples[i];
        if (!r->active) continue;
        r->t += ui->dt / 0.62f;
        if (r->t >= 1.0f) {
            r->active = false;
            continue;
        }
        float k = ui_ease_out(r->t);
        float rad = r->radius * (0.15f + k * 0.95f);
        uint32_t a = (uint32_t)((color >> 24) & 0xFF);
        uint8_t aa = (uint8_t)((float)a * (1.0f - r->t));
        uint32_t c = (color & 0x00FFFFFFu) | ((uint32_t)aa << 24);
        ui_draw_circle(ui, r->x, r->y, rad, c);
    }
}

/* 页面入场：重新开始 */
void ui_page_enter(UiContext *ui) {
    ui->select_id = 0;
    ui->drag_slider_id = -1;
    ui->page_enter = ui->reduce_motion ? 1.0f : 0.0f;
    ui->capture_action = -1;   /* 翻页时取消键位捕获 */
}

/* 每帧推进入场：380ms，上移 10px 淡入 */
static void ui_page_tick(UiContext *ui) {
    if (ui->page_enter < 1.0f) {
        ui->page_enter += ui->dt / 0.38f;
        if (ui->page_enter > 1.0f) ui->page_enter = 1.0f;
    }
    float k = ui_ease_out(ui->page_enter);
    ui->global_alpha = 0.35f + 0.65f * k;
    ui->global_dy = 0.0f;
}

