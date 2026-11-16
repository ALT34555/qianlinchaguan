/* RAIN1/RAIN2：量化降水尾段 */
#include "world_precip_save.h"
#include "precip.h"
#include "core/binary.h"
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

static bool precip_dims_valid(int cols, int rows, uint64_t *out_cells) {
    if (cols <= 0 || rows <= 0 || cols > 4096 || rows > 4096) return false;
    uint64_t cells = (uint64_t)(uint32_t)cols * (uint64_t)(uint32_t)rows;
    if (cells == 0 || cells > 4u * 1024u * 1024u) return false;
    *out_cells = cells;
    return true;
}

/* 0 旧档 / 1 有效 / -1 损坏 */
static int parse_header(const uint8_t raw[PRECIP_SAVE_HEADER_BYTES], size_t total,
                        uint32_t *ver, int *day, int *cols, int *rows,
                        uint32_t *seed, uint32_t *payload, uint32_t *checksum, uint32_t *raw_size) {
    if (raw[0] != 'R' || raw[1] != 'A' || raw[2] != 'I' || raw[3] != 'N') return -1;
    *ver = get_u32(raw + 4);
    *payload = get_u32(raw + 8);
    *day = (int)get_u32(raw + 12);
    *cols = (int)get_u32(raw + 16);
    *rows = (int)get_u32(raw + 20);
    *seed = get_u32(raw + 24);
    uint32_t scale = get_u32(raw + 28);
    *checksum = get_u32(raw + 32);
    *raw_size = get_u32(raw + 36);
    if (scale != (uint32_t)PRECIP_SAVE_SCALE) return -1;
    uint64_t cells = 0;
    if (!precip_dims_valid(*cols, *rows, &cells)) return -1;
    if (*ver == PRECIP_SAVE_VERSION) {
        if (*raw_size != 0u || cells * 5u != (uint64_t)*payload) return -1;
    } else if (*ver == PRECIP_SAVE_VERSION2) {
        uint64_t expected_raw = cells * 4u + (cells + 7u) / 8u;
        if ((uint64_t)*raw_size != expected_raw || *payload == 0u) return -1;
    } else {
        return -1;
    }
    if ((uint64_t)total != (uint64_t)PRECIP_SAVE_HEADER_BYTES + *payload) return -1;
    return 1;
}

static int read_header(FILE *file, uint32_t *ver, int *day, int *cols, int *rows,
                       uint32_t *seed, uint32_t *payload, uint32_t *checksum, uint32_t *raw_size, long limit) {
    long start = ftell(file);
    long end = precip_limit_end(file, limit);
    if (start < 0 || end < 0 || fseek(file, start, SEEK_SET) != 0) return -1;
    if (end == start) return 0;
    if (end - start < (long)PRECIP_SAVE_HEADER_BYTES) return -1;
    uint8_t raw[PRECIP_SAVE_HEADER_BYTES];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) return -1;
    return parse_header(raw, (size_t)(end - start), ver, day, cols, rows, seed, payload, checksum, raw_size);
}

static void fill_header(uint8_t raw[PRECIP_SAVE_HEADER_BYTES], uint32_t version, uint32_t payload,
                        const PrecipField *f, uint32_t checksum, uint32_t extra) {
    memset(raw, 0, PRECIP_SAVE_HEADER_BYTES);
    raw[0] = 'R'; raw[1] = 'A'; raw[2] = 'I'; raw[3] = 'N';
    put_u32(raw + 4, version);
    put_u32(raw + 8, payload);
    put_u32(raw + 12, (uint32_t)f->day);
    put_u32(raw + 16, (uint32_t)f->columns);
    put_u32(raw + 20, (uint32_t)f->rows);
    put_u32(raw + 24, f->seed);
    put_u32(raw + 28, (uint32_t)PRECIP_SAVE_SCALE);
    put_u32(raw + 32, checksum);
    put_u32(raw + 36, extra);
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
    fill_header(raw, PRECIP_SAVE_VERSION, payload, f, checksum, 0u);
    bool ok = fwrite(raw, 1, sizeof(raw), file) == sizeof(raw) &&
              fwrite(buf, 1, payload, file) == payload;
    free(buf);
    return ok;
}

bool qlcg_precip_encode_compact(const MacroWorld *world, uint8_t **out_buf, size_t *out_size) {
    if (!out_buf || !out_size) return false;
    *out_buf = NULL; *out_size = 0;
    if (!world || !world->precip || !world->precip->ready) return true;
    qlcg_precip_flush_partial((MacroWorld *)world);
    const PrecipField *f = world->precip;
    size_t cells = (size_t)f->cell_count;
    size_t packed_bytes = cells * 4u + (cells + 7u) / 8u;
    uint8_t *packed = (uint8_t *)calloc(packed_bytes ? packed_bytes : 1u, 1u);
    if (!packed) return false;
    uint8_t *w_lo = packed, *w_hi = packed + cells;
    uint8_t *r_lo = packed + cells * 2u, *r_hi = packed + cells * 3u;
    uint8_t *p_bits = packed + cells * 4u;
    uint16_t prev_w = 0, prev_r = 0;
    for (size_t i = 0; i < cells; i++) {
        uint16_t cw = quant(f->water[i]), cr = quant(f->rain[i]);
        uint16_t dw = (uint16_t)(cw - prev_w), dr = (uint16_t)(cr - prev_r);
        prev_w = cw; prev_r = cr;
        w_lo[i] = (uint8_t)(dw & 0xFFu); w_hi[i] = (uint8_t)(dw >> 8);
        r_lo[i] = (uint8_t)(dr & 0xFFu); r_hi[i] = (uint8_t)(dr >> 8);
        if (f->phase[i]) p_bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
    }
    uint32_t checksum = qlcg_crc32(0, packed, packed_bytes);
    size_t max_comp = qlcg_lz4_bound(packed_bytes);
    uint8_t *out = (uint8_t *)malloc(PRECIP_SAVE_HEADER_BYTES + max_comp);
    if (!out) { free(packed); return false; }
    size_t comp_size = qlcg_lz4_compress(packed, packed_bytes, out + PRECIP_SAVE_HEADER_BYTES, max_comp);
    free(packed);
    if (!comp_size || comp_size > UINT32_MAX) { free(out); return false; }
    fill_header(out, PRECIP_SAVE_VERSION2, (uint32_t)comp_size, f, checksum, (uint32_t)packed_bytes);
    *out_buf = out; *out_size = PRECIP_SAVE_HEADER_BYTES + comp_size;
    return true;
}

static bool adopt_precip_data(uint32_t ver, int day, int cols, int rows, uint32_t seed,
                              const uint8_t *payload, size_t payload_bytes, uint32_t checksum,
                              uint32_t raw_size, MacroWorld *world, const WorldCreatorOptions *opt, bool strict) {
    uint8_t *unpacked = NULL;
    const uint8_t *src = payload;
    if (ver == PRECIP_SAVE_VERSION) {
        if (fnv32(payload, payload_bytes) != checksum) return !strict;
    } else {
        unpacked = (uint8_t *)malloc(raw_size ? raw_size : 1u);
        if (!unpacked) return false;
        if (!qlcg_lz4_decompress(payload, payload_bytes, unpacked, raw_size) ||
            qlcg_crc32(0, unpacked, raw_size) != checksum) {
            free(unpacked);
            return !strict;
        }
        src = unpacked;
    }
    int expected_cols = world ? world->columns : opt->columns;
    int expected_rows = world ? world->rows : opt->columns / (opt->map_mode == WORLD_MAP_INFINITE ? 1 : 2);
    if (cols != expected_cols || rows != expected_rows || seed != opt->seed) {
        fprintf(stderr, "[precip] RAIN 与当前世界不匹配，忽略\n");
        free(unpacked);
        return !strict;
    }
    size_t cells = (size_t)cols * (size_t)rows;
    if (ver == PRECIP_SAVE_VERSION && strict) {
        for (size_t i = 0; i < cells; i++) if (src[cells * 4u + i] > 1) { free(unpacked); return false; }
    }
    if (!world) { free(unpacked); return true; }
    if (!world->precip) world->precip = qlcg_precip_create(world);
    if (!world->precip) { free(unpacked); return false; }
    float *water = (float *)malloc(cells * sizeof(float));
    float *rain = (float *)malloc(cells * sizeof(float));
    uint8_t *phase = (uint8_t *)malloc(cells);
    if (!water || !rain || !phase) {
        free(water); free(rain); free(phase); free(unpacked);
        return false;
    }
    if (ver == PRECIP_SAVE_VERSION) {
        const uint8_t *pw = src, *pr = src + cells * 2u, *pp = src + cells * 4u;
        for (size_t i = 0; i < cells; i++) {
            water[i] = (float)get_u16(pw + i * 2u) / PRECIP_SAVE_SCALE;
            rain[i] = (float)get_u16(pr + i * 2u) / PRECIP_SAVE_SCALE;
            phase[i] = pp[i] ? 1u : 0u;
        }
    } else {
        const uint8_t *w_lo = src, *w_hi = src + cells;
        const uint8_t *r_lo = src + cells * 2u, *r_hi = src + cells * 3u;
        const uint8_t *p_bits = src + cells * 4u;
        uint16_t prev_w = 0, prev_r = 0;
        for (size_t i = 0; i < cells; i++) {
            prev_w = (uint16_t)(prev_w + ((uint16_t)w_lo[i] | ((uint16_t)w_hi[i] << 8)));
            prev_r = (uint16_t)(prev_r + ((uint16_t)r_lo[i] | ((uint16_t)r_hi[i] << 8)));
            water[i] = (float)prev_w / PRECIP_SAVE_SCALE;
            rain[i] = (float)prev_r / PRECIP_SAVE_SCALE;
            phase[i] = (p_bits[i >> 3] >> (i & 7u)) & 1u;
        }
    }
    bool ok = qlcg_precip_adopt(world->precip, day, water, rain, phase);
    free(water); free(rain); free(phase); free(unpacked);
    return ok;
}

static bool read_precip(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit, bool strict) {
    if (!file || !opt) return false;
    uint32_t ver = 0, seed = 0, payload = 0, checksum = 0, raw_size = 0;
    int day = 0, cols = 0, rows = 0;
    int r = read_header(file, &ver, &day, &cols, &rows, &seed, &payload, &checksum, &raw_size, limit);
    if (r <= 0) return r == 0 || !strict;
    uint8_t *buf = (uint8_t *)malloc(payload ? payload : 1u);
    if (!buf) return false;
    bool got = fread(buf, 1, payload, file) == payload;
    bool ok = got ? adopt_precip_data(ver, day, cols, rows, seed, buf, payload, checksum, raw_size, world, opt, strict) : !strict;
    free(buf);
    return ok;
}

bool qlcg_precip_read_strict_mem(const uint8_t *data, size_t size, MacroWorld *world, const WorldCreatorOptions *opt) {
    if (!opt) return false;
    if (size == 0) return true;
    if (!data || size < PRECIP_SAVE_HEADER_BYTES) return false;
    uint32_t ver = 0, seed = 0, payload = 0, checksum = 0, raw_size = 0;
    int day = 0, cols = 0, rows = 0;
    if (parse_header(data, size, &ver, &day, &cols, &rows, &seed, &payload, &checksum, &raw_size) != 1) return false;
    return adopt_precip_data(ver, day, cols, rows, seed, data + PRECIP_SAVE_HEADER_BYTES, payload, checksum, raw_size, world, opt, true);
}

bool qlcg_precip_read_optional_lim(FILE *file, MacroWorld *world, long limit) {
    return world && read_precip(file, world, &world->opt, limit, false);
}

bool qlcg_precip_read_strict(FILE *file, MacroWorld *world, const WorldCreatorOptions *opt, long limit) {
    return read_precip(file, world, opt, limit, true);
}

bool qlcg_precip_tail_valid_lim(FILE *file, long limit) {
    uint32_t ver = 0, seed = 0, payload = 0, checksum = 0, raw_size = 0;
    int day = 0, cols = 0, rows = 0;
    int r = read_header(file, &ver, &day, &cols, &rows, &seed, &payload, &checksum, &raw_size, limit);
    if (r == 0) return true;   /* 旧档无天气尾段 */
    if (r < 0) return true;    /* 旧档尾段损坏不拖垮存档 */
    uint8_t *buf = (uint8_t *)malloc(payload ? payload : 1u);
    if (!buf) return true;
    bool ok = fread(buf, 1, payload, file) == payload && fnv32(buf, payload) == checksum;
    free(buf);
    return ok;
}
