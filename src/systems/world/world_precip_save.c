/* RAIN1：量化降水尾段 */
#include "world_precip_save.h"
#include "precip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint32_t fnv32(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint16_t quant(float v) {
    float q = v * PRECIP_SAVE_SCALE;
    if (q < 0.0f) q = 0.0f;
    if (q > 65535.0f) q = 65535.0f;
    return (uint16_t)lrintf(q);
}

/* 有效文件尾：limit>0 取槽尾，否则文件末 */
static long precip_limit_end(FILE *file, long limit) {
    if (limit > 0) return limit;
    if (fseek(file, 0, SEEK_END) != 0) return -1;
    return ftell(file);
}

/* 0 旧档 / 1 有效 / -1 损坏 */
static int read_header(FILE *file, int *day, int *cols, int *rows,
                       uint32_t *seed, uint32_t *payload, uint32_t *checksum, long limit) {
    long start = ftell(file);
    long end = precip_limit_end(file, limit);
    if (start < 0 || end < 0 || fseek(file, start, SEEK_SET) != 0) return -1;
    if (end == start) return 0;
    if (end - start < (long)PRECIP_SAVE_HEADER_BYTES) return -1;
    uint8_t raw[PRECIP_SAVE_HEADER_BYTES];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) return -1;
    if (raw[0] != 'R' || raw[1] != 'A' || raw[2] != 'I' || raw[3] != 'N') return -1;
    uint32_t version = get_u32(raw + 4);
    *payload = get_u32(raw + 8);
    *day = (int)get_u32(raw + 12);
    *cols = (int)get_u32(raw + 16);
    *rows = (int)get_u32(raw + 20);
    *seed = get_u32(raw + 24);
    uint32_t scale = get_u32(raw + 28);
    *checksum = get_u32(raw + 32);
    uint32_t reserved = get_u32(raw + 36);
    if (version != PRECIP_SAVE_VERSION || reserved != 0u) return -1;
    if (scale != (uint32_t)PRECIP_SAVE_SCALE) return -1;
    if (*cols <= 0 || *rows <= 0 || *cols > 4096 || *rows > 4096) return -1;
    uint64_t cells = (uint64_t)(uint32_t)*cols * (uint64_t)(uint32_t)*rows;
    if (cells == 0 || cells > 4u * 1024u * 1024u) return -1;
    if (cells * 5u != (uint64_t)*payload) return -1;
    if ((uint64_t)(end - start) != (uint64_t)PRECIP_SAVE_HEADER_BYTES + *payload) return -1;
    return 1;
}

bool qlcg_precip_write(FILE *file, const MacroWorld *world) {
    if (!file || !world || !world->precip || !world->precip->ready) return true;
    qlcg_precip_flush_partial((MacroWorld *)world);
    const PrecipField *f = world->precip;
    uint64_t cells = (uint64_t)f->cell_count;
    uint32_t payload = (uint32_t)(cells * 5u);
    uint8_t *buf = (uint8_t *)malloc(payload ? payload : 1);
    if (!buf) return false;
    uint8_t *pw = buf;
    uint8_t *pr = buf + cells * 2u;
    uint8_t *pp = buf + cells * 4u;
    for (int i = 0; i < f->cell_count; i++) {
        put_u16(pw + (size_t)i * 2u, quant(f->water[i]));
        put_u16(pr + (size_t)i * 2u, quant(f->rain[i]));
        pp[i] = f->phase[i] ? 1u : 0u;
    }
    uint32_t checksum = fnv32(buf, payload);
    uint8_t raw[PRECIP_SAVE_HEADER_BYTES];
    memset(raw, 0, sizeof(raw));
    raw[0] = 'R'; raw[1] = 'A'; raw[2] = 'I'; raw[3] = 'N';
    put_u32(raw + 4, PRECIP_SAVE_VERSION);
    put_u32(raw + 8, payload);
    put_u32(raw + 12, (uint32_t)f->day);
    put_u32(raw + 16, (uint32_t)f->columns);
    put_u32(raw + 20, (uint32_t)f->rows);
    put_u32(raw + 24, f->seed);
    put_u32(raw + 28, (uint32_t)PRECIP_SAVE_SCALE);
    put_u32(raw + 32, checksum);
    bool ok = fwrite(raw, 1, sizeof(raw), file) == sizeof(raw) &&
              fwrite(buf, 1, payload, file) == payload;
    free(buf);
    return ok;
}

static bool read_precip(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit, bool strict) {
    if (!file || !opt) return false;
    int day = 0, cols = 0, rows = 0;
    uint32_t seed = 0, payload = 0, checksum = 0;
    int r = read_header(file, &day, &cols, &rows, &seed, &payload, &checksum, limit);
    if (r <= 0) return r == 0 || !strict;
    uint8_t *buf = (uint8_t *)malloc(payload);
    if (!buf) return false;
    if (fread(buf, 1, payload, file) != payload || fnv32(buf, payload) != checksum) {
        free(buf);
        return !strict;
    }
    int expected_cols=world?world->columns:opt->columns;
    int expected_rows=world?world->rows:opt->columns/(opt->map_mode==WORLD_MAP_INFINITE?1:2);
    if (cols != expected_cols || rows != expected_rows || seed != opt->seed) {
        fprintf(stderr, "[precip] RAIN1 与当前世界不匹配，忽略\n");
        free(buf);
        return !strict;
    }
    size_t cell_total = (size_t)cols * rows;
    if (strict) for (size_t i = 0; i < cell_total; i++) {
        if (buf[cell_total * 4 + i] > 1) { free(buf); return false; }
    }
    if (!world) { free(buf); return true; }
    if (!world->precip) world->precip = qlcg_precip_create(world);
    if (!world->precip) { free(buf); return false; }
    size_t cells = (size_t)cols * (size_t)rows;
    float *water = (float *)malloc(cells * sizeof(float));
    float *rain = (float *)malloc(cells * sizeof(float));
    uint8_t *phase = (uint8_t *)malloc(cells);
    if (!water || !rain || !phase) {
        free(water); free(rain); free(phase); free(buf);
        return false;
    }
    const uint8_t *pw = buf;
    const uint8_t *pr = buf + cells * 2u;
    const uint8_t *pp = buf + cells * 4u;
    for (size_t i = 0; i < cells; i++) {
        water[i] = (float)get_u16(pw + i * 2u) / PRECIP_SAVE_SCALE;
        rain[i] = (float)get_u16(pr + i * 2u) / PRECIP_SAVE_SCALE;
        phase[i] = pp[i] ? 1u : 0u;
    }
    bool ok = qlcg_precip_adopt(world->precip, day, water, rain, phase);
    free(water); free(rain); free(phase); free(buf);
    return ok;
}

bool qlcg_precip_read_optional_lim(FILE *file, MacroWorld *world, long limit) {
    return world && read_precip(file, world, &world->opt, limit, false);
}

bool qlcg_precip_read_strict(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit) {
    return read_precip(file, world, opt, limit, true);
}

bool qlcg_precip_tail_valid_lim(FILE *file, long limit) {
    int day = 0, cols = 0, rows = 0;
    uint32_t seed = 0, payload = 0, checksum = 0;
    int r = read_header(file, &day, &cols, &rows, &seed, &payload, &checksum, limit);
    if (r <= 0) return true;   /* 天气尾段可选，损坏不拖垮存档 */
    uint8_t *buf = (uint8_t *)malloc(payload);
    if (!buf) return true;
    bool ok = fread(buf, 1, payload, file) == payload && fnv32(buf, payload) == checksum;
    free(buf);
    (void)ok;
    return true;
}
