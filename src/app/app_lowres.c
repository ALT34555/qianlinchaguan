/* 场景分辨率与多重采样抗锯齿 */
#include "app/app_state.h"
#include "../terrain3d/gl_loader.h"

static GLuint s_fbo   = 0;
static GLuint s_tex   = 0;
static GLuint s_depth = 0;
static GLuint s_ms_fbo = 0;
static GLuint s_ms_color = 0;
static GLuint s_ms_depth = 0;
static int    s_w     = 0;
static int    s_h     = 0;
static int    s_requested_samples = 0;
static int    s_max_samples = -1;
static bool   s_scaled = false;
static bool   s_ready = false;

/* 解析 resolution，保持窗口宽高比防变形 */
void app_resolution_size(int win_w, int win_h, int *out_w, int *out_h) {
    int w = win_w, h = win_h;
    const char *res = g_app.settings.resolution;
    if (res && res[0] && strcmp(res, "auto") != 0) {
        int rw = 0, rh = 0;
        if (sscanf(res, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0) {
            float wa = (float)win_w / (float)(win_h > 0 ? win_h : 1);
            float ra = (float)rw / (float)rh;
            if (ra > wa) {
                w = rw;
                h = (int)((float)rw / wa + 0.5f);
            } else {
                h = rh;
                w = (int)((float)rh * wa + 0.5f);
            }
        }
    }
    *out_w = w;
    *out_h = h;
}

static int requested_samples(void) {
    if (!g_app.settings.antialias || !glRenderbufferStorageMultisample) return 0;
    if (s_max_samples < 0) glGetIntegerv(GL_MAX_SAMPLES, &s_max_samples);
    return s_max_samples >= 4 ? 4 : (s_max_samples >= 2 ? 2 : 0);
}

/* 场景离屏路径是否生效 */
bool app_lowres_active(int win_w, int win_h) {
    int rw, rh;
    app_resolution_size(win_w, win_h, &rw, &rh);
    return requested_samples() > 0 || (rw != win_w) || (rh != win_h);
}

static void release_multisample(void) {
    if (s_ms_color) { glDeleteRenderbuffers(1, &s_ms_color); s_ms_color = 0; }
    if (s_ms_depth) { glDeleteRenderbuffers(1, &s_ms_depth); s_ms_depth = 0; }
    if (s_ms_fbo) { glDeleteFramebuffers(1, &s_ms_fbo); s_ms_fbo = 0; }
}

void app_lowres_release(void) {
    release_multisample();
    if (s_depth) { glDeleteRenderbuffers(1, &s_depth); s_depth = 0; }
    if (s_fbo)   { glDeleteFramebuffers(1, &s_fbo);    s_fbo   = 0; }
    if (s_tex)   { glDeleteTextures(1, &s_tex);        s_tex   = 0; }
    s_w = s_h = 0;
    s_requested_samples = 0;
    s_scaled = false;
    s_ready = false;
}

static bool make_single_sample(int w, int h, bool needs_depth) {
    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (needs_depth) {
        glGenRenderbuffers(1, &s_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, s_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    }

    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tex, 0);
    if (needs_depth)
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_depth);
    bool ok = s_fbo && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    return ok;
}

static bool ensure_target(int w, int h, int samples, bool scaled) {
    if (s_ready && s_w == w && s_h == h && s_requested_samples == samples && s_scaled == scaled)
        return s_ms_fbo || s_fbo;
    if (!glGenFramebuffers || !glBindFramebuffer || !glFramebufferTexture2D ||
        !glCheckFramebufferStatus || !glGenRenderbuffers || !glBindRenderbuffer ||
        !glBlitFramebuffer || !glRenderbufferStorage || !glFramebufferRenderbuffer) {
        return false;
    }
    app_lowres_release();

    if (samples > 0) {
        glGenRenderbuffers(1, &s_ms_color);
        glBindRenderbuffer(GL_RENDERBUFFER, s_ms_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        glGenRenderbuffers(1, &s_ms_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, s_ms_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        glGenFramebuffers(1, &s_ms_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, s_ms_fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_ms_color);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_ms_depth);
        if (!s_ms_fbo || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            release_multisample();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    if (scaled && !make_single_sample(w, h, !s_ms_fbo)) {
        bool retry_single_sample = s_ms_fbo != 0;
        app_lowres_release();
        if (retry_single_sample && !make_single_sample(w, h, true)) app_lowres_release();
    }
    s_w = w;
    s_h = h;
    s_requested_samples = samples;
    s_scaled = scaled;
    s_ready = true;
    return s_ms_fbo || s_fbo;
}

/* 抗锯齿或缩放时切到场景离屏 */
bool app_lowres_begin(int win_w, int win_h) {
    int rw, rh;
    app_resolution_size(win_w, win_h, &rw, &rh);
    int samples = requested_samples();
    bool scaled = rw != win_w || rh != win_h;
    if (!scaled && samples == 0) {
        if (s_ready) app_lowres_release();
        return false;
    }
    if (!ensure_target(rw, rh, samples, scaled)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, s_ms_fbo ? s_ms_fbo : s_fbo);
    if (s_ms_fbo) glEnable(GL_MULTISAMPLE);
    glViewport(0, 0, rw, rh);
    /* 离屏颜色与深度必须自行清，外层只清默认缓冲 */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

/* 先解析多重采样，再按分辨率缩放 */
void app_lowres_end(int win_w, int win_h, int pix_w, int pix_h, bool pixelated) {
    if (!s_ms_fbo && !s_fbo) return;
    GLuint source = s_ms_fbo ? s_ms_fbo : s_fbo;
    if (s_ms_fbo && (s_w != pix_w || s_h != pix_h)) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_ms_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo);
        glBlitFramebuffer(0, 0, s_w, s_h, 0, 0, s_w, s_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        source = s_fbo;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, s_w, s_h, 0, 0, pix_w, pix_h,
                      GL_COLOR_BUFFER_BIT,
                      (pixelated || source == s_ms_fbo) ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
}

/* 320x240 档位用最近邻，对齐 base */
bool app_lowres_pixelated(void) {
    return strcmp(g_app.settings.resolution, "320x240") == 0;
}

/* 当前离屏缓冲是否带 MSAA */
bool app_lowres_msaa(void) {
    return s_ms_fbo != 0;
}
