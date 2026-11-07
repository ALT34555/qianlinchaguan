/* ICE1 尾段：显式小端序列化 */
#include "world_ice_save.h"
#include "ice_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 空位/墓碑与 ice_runtime.c 同步 */
#define EDIT_KEY_EMPTY INT64_MIN
#define EDIT_KEY_TOMB  INT64_MAX
#define EDIT_FLAGS_ALL (ICE_EDIT_HOLE | ICE_EDIT_DAMAGE)
#define TRANS_FLAG_USED 1u

_Static_assert(ICE_SAVE_EDIT_LIMIT == ICE_EDIT_CAP_MAX / 2u, "limit is half cap");
_Static_assert(ICE_TRANS_CAP <= 64, "transition slice fits");

/* ---------- 小端读写 ---------- */

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

static void put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static void put_i64(uint8_t *p, int64_t v) { put_u64(p, (uint64_t)v); }
static int64_t get_i64(const uint8_t *p) { return (int64_t)get_u64(p); }
static void put_i32(uint8_t *p, int32_t v) { put_u32(p, (uint32_t)v); }
static int32_t get_i32(const uint8_t *p) { return (int32_t)get_u32(p); }

static void put_f32(uint8_t *p, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    put_u32(p, u);
}

static float get_f32(const uint8_t *p) {
    uint32_t u = get_u32(p);
    float v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

static void put_f64(uint8_t *p, double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    put_u64(p, u);
}

static double get_f64(const uint8_t *p) {
    uint64_t u = get_u64(p);
    double v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* ---------- 尾段头 ---------- */

typedef struct {
    uint32_t version;
    uint32_t payload_bytes;
    uint32_t edit_count;
    uint32_t transition_count;
    uint32_t model_version;
    uint32_t epoch;
    uint32_t reserved;
    double   saved_climate_day;
} IceSaveHeader;

/* 有效文件尾：limit>0 取槽尾，否则文件末 */
static long ice_limit_end(FILE *file, long limit) {
    if (limit > 0) return limit;
    if (fseek(file, 0, SEEK_END) != 0) return -1;
    return ftell(file);
}

/* 返回 1 有 ICE1，0 无（到界或紧接 RAIN1），-1 损坏 */
static int read_header_lim(FILE *file, IceSaveHeader *h, long limit) {
    long start = ftell(file);
    long end = ice_limit_end(file, limit);
    memset(h, 0, sizeof(*h));
    if (start < 0 || end < 0 || fseek(file, start, SEEK_SET) != 0) return -1;
    if (end == start) return 0;
    if (end - start < (long)ICE_SAVE_HEADER_BYTES) return -1;
    uint8_t raw[ICE_SAVE_HEADER_BYTES];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) return -1;
    if (raw[0] != 'I' || raw[1] != 'C' || raw[2] != 'E' || raw[3] != '1') {
        bool rain = raw[0] == 'R' && raw[1] == 'A' && raw[2] == 'I' && raw[3] == 'N';
        return fseek(file, start, SEEK_SET) == 0 && rain ? 0 : -1;
    }
    h->version          = get_u32(raw + 4);
    h->payload_bytes    = get_u32(raw + 8);
    h->edit_count       = get_u32(raw + 12);
    h->transition_count = get_u32(raw + 16);
    h->model_version    = get_u32(raw + 20);
    h->epoch            = get_u32(raw + 24);
    h->reserved         = get_u32(raw + 28);
    h->saved_climate_day = get_f64(raw + 32);
    if (h->version != ICE_SAVE_VERSION || h->model_version != ICE_SAVE_MODEL_VERSION) return -1;
    if (h->reserved != 0u || !isfinite(h->saved_climate_day)) return -1;
    if (h->edit_count > ICE_SAVE_EDIT_LIMIT || h->transition_count > (uint32_t)ICE_TRANS_CAP) return -1;
    /* 64 位乘法后再比，避免 32 位回绕 */
    uint64_t need = (uint64_t)h->edit_count * ICE_SAVE_EDIT_BYTES +
                    (uint64_t)h->transition_count * ICE_SAVE_TRANS_BYTES;
    if (need != (uint64_t)h->payload_bytes) return -1;
    if ((uint64_t)(end - start) < (uint64_t)ICE_SAVE_HEADER_BYTES + need) return -1;
    return 1;
}

static bool write_header(FILE *file, uint32_t payload, uint32_t edits, uint32_t trans,
                         uint32_t epoch, double day) {
    uint8_t raw[ICE_SAVE_HEADER_BYTES];
    memset(raw, 0, sizeof(raw));
    raw[0] = 'I'; raw[1] = 'C'; raw[2] = 'E'; raw[3] = '1';
    put_u32(raw + 4, ICE_SAVE_VERSION);
    put_u32(raw + 8, payload);
    put_u32(raw + 12, edits);
    put_u32(raw + 16, trans);
    put_u32(raw + 20, ICE_SAVE_MODEL_VERSION);
    put_u32(raw + 24, epoch);
    put_u32(raw + 28, 0u);
    put_f64(raw + 32, day);
    return fwrite(raw, 1, sizeof(raw), file) == sizeof(raw);
}

/* 空闲表定位：与 ice_runtime.c 同式 */

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint32_t key_hash(int64_t gx, int64_t gz) {
    return (uint32_t)mix64((uint64_t)gx * 73856093ULL ^ (uint64_t)gz * 19349663ULL);
}

static IceEdit *slot_of(IceEdit *tab, uint32_t cap, int64_t gx, int64_t gz, bool for_insert) {
    uint32_t mask = cap - 1u;
    uint32_t i = key_hash(gx, gz) & mask;
    IceEdit *tomb = NULL;
    for (uint32_t probe = 0; probe <= mask; probe++) {
        IceEdit *e = &tab[i];
        if (e->gx == EDIT_KEY_EMPTY) return for_insert ? (tomb ? tomb : e) : NULL;
        if (e->gx == EDIT_KEY_TOMB) { if (!tomb) tomb = e; }
        else if (e->gx == gx && e->gz == gz) return e;
        i = (i + 1u) & mask;
    }
    return for_insert ? tomb : NULL;
}

static void table_clear(IceEdit *tab, uint32_t cap) {
    for (uint32_t i = 0; i < cap; i++) {
        tab[i].gx = EDIT_KEY_EMPTY;
        tab[i].gz = 0;
        tab[i].broken_at = 0.0;
        tab[i].damage = 0.0f;
        tab[i].flags = 0u;
        tab[i].stamp = 0u;
    }
}

/* 装载率 ≤ 1/2；超上限返回 0 */
static uint32_t table_cap_for(uint32_t count) {
    uint32_t cap = 64u;
    while (cap < count * 2u && cap < ICE_EDIT_CAP_MAX) cap <<= 1;
    if (cap < count * 2u || cap > ICE_EDIT_CAP_MAX) return 0u;
    return cap;
}

/* ---------- 写 ---------- */

bool qlcg_ice_write(FILE *file, const MacroWorld *world) {
    if (!file) return false;
    const IceWorldState *st = world ? world->ice_state : NULL;
    uint32_t edits = 0, trans = 0;
    if (st && st->edits) {
        for (uint32_t i = 0; i < st->cap; i++) {
            int64_t gx = st->edits[i].gx;
            if (gx == EDIT_KEY_EMPTY || gx == EDIT_KEY_TOMB) continue;
            edits++;
        }
    }
    if (st) for (int i = 0; i < ICE_TRANS_CAP; i++) if (st->trans[i].used) trans++;
    /* 超出可读上限时明确失败，不静默截断 */
    if (edits > ICE_SAVE_EDIT_LIMIT) return false;
    uint32_t payload = edits * ICE_SAVE_EDIT_BYTES + trans * ICE_SAVE_TRANS_BYTES;
    uint32_t epoch = st ? st->epoch : 0u;
    double day = (world && world->ice_time_valid) ? world->ice_time.climate_day
               : (st ? st->last_climate_day : 0.0);
    if (!isfinite(day)) day = 0.0;
    long start = ftell(file);
    if (start < 0) return false;
    if (!write_header(file, payload, edits, trans, epoch, day)) return false;
    if (st && st->edits) {
        for (uint32_t i = 0; i < st->cap; i++) {
            const IceEdit *e = &st->edits[i];
            if (e->gx == EDIT_KEY_EMPTY || e->gx == EDIT_KEY_TOMB) continue;
            uint8_t raw[ICE_SAVE_EDIT_BYTES];
            put_i64(raw + 0, e->gx);
            put_i64(raw + 8, e->gz);
            put_f64(raw + 16, e->broken_at);
            put_f32(raw + 24, e->damage);
            put_u32(raw + 28, e->flags);
            put_u32(raw + 32, e->stamp);
            put_u32(raw + 36, 0u);
            if (fwrite(raw, 1, sizeof(raw), file) != sizeof(raw)) return false;
        }
    }
    for (int i = 0; st && i < ICE_TRANS_CAP; i++) {
        const IceThermalTransition *t = &st->trans[i];
        if (!t->used) continue;
        uint8_t raw[ICE_SAVE_TRANS_BYTES];
        put_i32(raw + 0, t->cx);
        put_i32(raw + 4, t->cz);
        put_f32(raw + 8, t->wx);
        put_f32(raw + 12, t->wz);
        put_f64(raw + 16, t->start_day);
        put_f32(raw + 24, t->delta_h);
        put_f32(raw + 28, t->delta_q);
        put_f32(raw + 32, t->tau);
        put_u32(raw + 36, TRANS_FLAG_USED);
        if (fwrite(raw, 1, sizeof(raw), file) != sizeof(raw)) return false;
    }
    long end = ftell(file);
    return end >= start && (uint64_t)(end - start) == (uint64_t)ICE_SAVE_HEADER_BYTES + payload;
}

/* ---------- 读 ---------- */

/* 读负载并装入 st；失败时 st 保持原样 */
static bool read_body(FILE *file, const IceSaveHeader *h, IceWorldState *st) {
    uint32_t count = h->edit_count;
    IceEdit *tmp = NULL;
    bool ok = true;
    if (count) {
        tmp = (IceEdit *)calloc(count, sizeof(IceEdit));
        if (!tmp) return false;
    }
    for (uint32_t i = 0; i < count && ok; i++) {
        uint8_t raw[ICE_SAVE_EDIT_BYTES];
        if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) { ok = false; break; }
        int64_t gx = get_i64(raw + 0), gz = get_i64(raw + 8);
        double broken = get_f64(raw + 16);
        float damage = get_f32(raw + 24);
        uint32_t flags = get_u32(raw + 28);
        uint32_t stamp = get_u32(raw + 32);
        uint32_t reserved = get_u32(raw + 36);
        if (gx == EDIT_KEY_EMPTY || gx == EDIT_KEY_TOMB) ok = false;
        else if (gz == EDIT_KEY_EMPTY || gz == EDIT_KEY_TOMB) ok = false;
        else if (!isfinite(broken) || !isfinite(damage)) ok = false;
        else if (!(damage >= 0.0f && damage <= 1.0f)) ok = false;
        else if ((flags & ~(uint32_t)EDIT_FLAGS_ALL) != 0u) ok = false;
        else if (reserved != 0u) ok = false;
        else {
            tmp[i].gx = gx;
            tmp[i].gz = gz;
            tmp[i].broken_at = broken;
            tmp[i].damage = damage;
            tmp[i].flags = flags;
            tmp[i].stamp = stamp;
        }
    }

    IceThermalTransition tr[ICE_TRANS_CAP];
    memset(tr, 0, sizeof(tr));
    for (uint32_t i = 0; i < h->transition_count && ok; i++) {
        uint8_t raw[ICE_SAVE_TRANS_BYTES];
        if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) { ok = false; break; }
        float wx = get_f32(raw + 8), wz = get_f32(raw + 12);
        double start = get_f64(raw + 16);
        float dh = get_f32(raw + 24), dq = get_f32(raw + 28), tau = get_f32(raw + 32);
        uint32_t flags = get_u32(raw + 36);
        if ((flags & ~TRANS_FLAG_USED) != 0u) { ok = false; break; }
        if (!(flags & TRANS_FLAG_USED)) continue;   /* 未用占位：跳过 */
        if (!isfinite(wx) || !isfinite(wz) || !isfinite(start)) { ok = false; break; }
        if (!isfinite(dh) || fabsf(dh) > 64.0f) { ok = false; break; }
        if (!isfinite(dq) || fabsf(dq) > 8.0f) { ok = false; break; }
        if (!isfinite(tau) || tau <= 0.0f || tau > 1024.0f) { ok = false; break; }
        tr[i].used = true;
        tr[i].cx = get_i32(raw + 0);
        tr[i].cz = get_i32(raw + 4);
        tr[i].wx = wx;
        tr[i].wz = wz;
        tr[i].start_day = start;
        tr[i].delta_h = dh;
        tr[i].delta_q = dq;
        tr[i].tau = tau;
    }

    if (ok && count == 0) {
        /* 档内声明空表：清掉旧表，避免残留 */
        free(st->edits);
        st->edits = NULL;
        st->cap = 0;
        st->count = 0;
        st->edit_revision++;
    } else if (ok && count) {
        uint32_t cap = table_cap_for(count);
        if (!cap) ok = false;
        else {
            IceEdit *tab = (IceEdit *)malloc((size_t)cap * sizeof(IceEdit));
            if (!tab) ok = false;
            else {
                table_clear(tab, cap);
                for (uint32_t i = 0; i < count && ok; i++) {
                    if (slot_of(tab, cap, tmp[i].gx, tmp[i].gz, false)) { ok = false; break; }
                    IceEdit *dst = slot_of(tab, cap, tmp[i].gx, tmp[i].gz, true);
                    if (!dst) { ok = false; break; }
                    *dst = tmp[i];
                }
                if (ok) {
                    free(st->edits);
                    st->edits = tab;
                    st->cap = cap;
                    st->count = count;
                    st->edit_revision++;
                } else {
                    free(tab);
                }
            }
        }
    }
    free(tmp);
    if (!ok) return false;
    memcpy(st->trans, tr, sizeof(tr));
    st->epoch = h->epoch;
    st->last_climate_day = h->saved_climate_day;
    return true;
}

bool qlcg_ice_read_optional_lim(FILE *file, MacroWorld *world, long limit) {
    if (!file || !world) return false;
    if (!world->ice_state) {
        world->ice_state = qlcg_ice_world_create();
        if (!world->ice_state) return false;
    }
    IceSaveHeader h;
    int r = read_header_lim(file, &h, limit);
    if (r == 0) return true;    /* 旧档：保留空编辑，按自然基线 */
    if (r < 0) return false;
    return read_body(file, &h, world->ice_state);
}

bool qlcg_ice_tail_valid_lim(FILE *file, long limit) {
    if (!file) return false;
    IceSaveHeader h;
    int r = read_header_lim(file, &h, limit);
    if (r == 0) return true;
    if (r < 0) return false;
    IceWorldState *probe = qlcg_ice_world_create();
    if (!probe) return false;
    bool ok = read_body(file, &h, probe);
    qlcg_ice_world_free(probe);
    return ok;
}

long qlcg_ice_tail_end_lim(FILE *file, long limit) {
    if (!file) return -1;
    long start = ftell(file);
    if (start < 0) return -1;
    IceSaveHeader h;
    int r = read_header_lim(file, &h, limit);
    if (r <= 0) return -1;
    return start + (long)ICE_SAVE_HEADER_BYTES + (long)h.payload_bytes;
}
