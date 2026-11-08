/* 背景磨砂：降采样1/4，两趟可分离高斯 */
#include "ui_core.h"
#include "../terrain3d/gl_loader.h"
#include <stdio.h>
#include <string.h>

#define BD_DIV 4   /* 降采样倍数：宽高各缩到 1/4，像素数 1/16 */

static const char *BD_VS_SRC =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    vUV = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* 9-tap 高斯：权重和 1，步长由 uStep 决定方向 */
static const char *BD_FS_SRC =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uStep;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec3 c = texture(uSrc, vUV).rgb * 0.2270270270;\n"
    "    c += texture(uSrc, vUV + uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(uSrc, vUV - uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(uSrc, vUV + uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture(uSrc, vUV - uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    FragColor = vec4(c, 1.0);\n"
    "}\n";

static void bd_free_targets(UiContext *ui) {
    if (ui->bd_tex[0]) { qlcg_gl_delete_textures(1, &ui->bd_tex[0]); ui->bd_tex[0] = 0; }
    if (ui->bd_tex[1]) { qlcg_gl_delete_textures(1, &ui->bd_tex[1]); ui->bd_tex[1] = 0; }
    if (ui->bd_fbo[0]) { qlcg_gl_delete_framebuffers(1, &ui->bd_fbo[0]); ui->bd_fbo[0] = 0; }
    if (ui->bd_fbo[1]) { qlcg_gl_delete_framebuffers(1, &ui->bd_fbo[1]); ui->bd_fbo[1] = 0; }
    ui->backdrop_tex   = 0;
    ui->backdrop_ready = false;
    ui->bd_w = ui->bd_h = 0;
}

static bool bd_make_target(UiContext *ui, int idx, int w, int h) {
    glGenTextures(1, &ui->bd_tex[idx]);
    glBindTexture(GL_TEXTURE_2D, ui->bd_tex[idx]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &ui->bd_fbo[idx]);
    glBindFramebuffer(GL_FRAMEBUFFER, ui->bd_fbo[idx]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ui->bd_tex[idx], 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        printf("[Backdrop] FBO %d incomplete: 0x%04X\n", idx, (unsigned)st);
        return false;
    }
    return true;
}

void ui_backdrop_init(UiContext *ui) {
#ifdef _WIN32
    if (!glGenFramebuffers || !glBlitFramebuffer) {
        printf("[Backdrop] FBO 扩展不可用，磨砂玻璃降级为半透明\n");
        return;
    }
#endif
    ui->bd_prog = qlcg_gl_compile_program(BD_VS_SRC, BD_FS_SRC);
    if (!ui->bd_prog) return;
    /* core profile 须绑 VAO 才可 draw */
    glGenVertexArrays(1, &ui->bd_vao);
}

void ui_backdrop_resize(UiContext *ui, int win_w, int win_h) {
    if (!ui->bd_prog) return;
    int w = win_w / BD_DIV, h = win_h / BD_DIV;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (w == ui->bd_w && h == ui->bd_h && ui->bd_w > 0) return;
    bd_free_targets(ui);
    if (!bd_make_target(ui, 0, w, h) || !bd_make_target(ui, 1, w, h)) {
        bd_free_targets(ui);
        return;
    }
    ui->bd_w = w;
    ui->bd_h = h;
}

/* 抓取后台缓冲降到 1/4 */
static void bd_capture(UiContext *ui) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ui->bd_fbo[0]);
    glBlitFramebuffer(0, 0, ui->pix_w, ui->pix_h, 0, 0, ui->bd_w, ui->bd_h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

/* 两趟可分离高斯，step_scale 缩放模糊半径 */
static void bd_blur_passes(UiContext *ui, float step_scale) {
    glViewport(0, 0, ui->bd_w, ui->bd_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(ui->bd_prog);
    glBindVertexArray(ui->bd_vao);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(qlcg_gl_uniform(ui->bd_prog, "uSrc"), 0);

    static const int kOrder[2][2] = { {0, 1}, {1, 0} };   /* src, dst */
    for (int pass = 0; pass < 2; pass++) {
        int src = kOrder[pass][0], dst = kOrder[pass][1];
        glBindFramebuffer(GL_FRAMEBUFFER, ui->bd_fbo[dst]);
        glBindTexture(GL_TEXTURE_2D, ui->bd_tex[src]);
        float sx = (pass == 0) ? step_scale / (float)ui->bd_w : 0.0f;
        float sy = (pass == 1) ? step_scale / (float)ui->bd_h : 0.0f;
        glUniform2f(qlcg_gl_uniform(ui->bd_prog, "uStep"), sx, sy);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ui->win_w, ui->win_h);
    glBindVertexArray(0);
}

/* 抓后台缓冲并模糊，须在场景后 UI 前 */
void ui_backdrop_capture(UiContext *ui) {
    if (!ui->bd_prog || !ui->bd_tex[0] || ui->bd_w <= 0) return;
    if (ui->reduce_motion) return;   /* 简化UI：不做磨砂，玻璃退化为半透明 */
    bd_capture(ui);
    bd_blur_passes(ui, 1.0f);
    ui->backdrop_tex   = ui->bd_tex[1];   /* 第二趟的输出 */
    ui->backdrop_ready = true;
}

/* 501：模糊整屏后盖回，UI 保持清晰 */
void ui_backdrop_blur_overlay(UiContext *ui, float radius_px) {
    if (!ui->bd_prog || !ui->bd_tex[0] || ui->bd_w <= 0) return;
    if (radius_px <= 0.01f) return;
    bd_capture(ui);
    /* 9-tap 在 1/4 尺寸下等效 σ≈6.55px */
    bd_blur_passes(ui, radius_px / 6.55f);
    ui_use_sprite_tex(ui, ui->bd_tex[1]);
    ui_draw_sprite_rect(ui, 0, 0, (float)ui->win_w, (float)ui->win_h,
                        0, 1, 1, 0, 0xFFFFFFFFu);
    ui_flush(ui);
    ui->cur_sprite_tex = 0;
}

void ui_backdrop_end_frame(UiContext *ui) {
    ui->backdrop_ready = false;
}

void ui_backdrop_release(UiContext *ui) {
    bd_free_targets(ui);
    if (ui->bd_vao)  { qlcg_gl_delete_vertex_arrays(1, &ui->bd_vao);  ui->bd_vao = 0; }
    if (ui->bd_prog) { qlcg_gl_delete_program(ui->bd_prog);          ui->bd_prog = 0; }
}
