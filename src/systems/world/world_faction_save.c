/* 冻结阵营名称、文化与领地 */
#include "world_faction_save.h"
#include <string.h>
#include <math.h>
#include <limits.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t cell_count;
    uint32_t faction_count;
    uint32_t faction_size;
    int32_t last_civ_day;
} FactionSaveHeader;

/* 有效文件尾：limit>0 取槽尾，否则文件末 */
static long tail_limit(FILE *file, long limit) {
    if (limit > 0) return limit;
    if (fseek(file, 0, SEEK_END) != 0) return -1;
    return ftell(file);
}

/* 校验头部并求负载字节数；位置停在文件末尾 */
static bool faction_tail_extent(FILE *file, long start, long limit,
                                unsigned long long *out_len) {
    if (start < 0) return false;
    FactionSaveHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1) return false;
    if (header.magic != 0x46414354u || header.version != 1u ||
        header.faction_count > MAX_FACTIONS || header.faction_size != sizeof(TerritoryFaction) ||
        header.cell_count == 0 || header.cell_count > 1024u * 512u) return false;
    uint64_t required = sizeof(header) + (uint64_t)header.faction_count * header.faction_size +
                        (uint64_t)header.cell_count * (sizeof(int16_t) + sizeof(uint8_t));
    long end = tail_limit(file, limit);
    if (end < start || (uint64_t)(end - start) < required) return false;
    if (out_len) *out_len = required;
    return true;
}

bool qlcg_factions_tail_valid_lim(FILE *file, long limit) {
    long start = ftell(file);
    if (start < 0) return false;
    unsigned long long len = 0;
    bool ok = faction_tail_extent(file, start, limit, &len);
    return fseek(file, start, SEEK_SET) == 0 && ok;
}

/* 结束偏移供后续可选尾段定位；返回后位置复位 */
long qlcg_factions_tail_end_lim(FILE *file, long limit) {
    long start = ftell(file);
    if (start < 0) return -1;
    unsigned long long len = 0;
    bool ok = faction_tail_extent(file, start, limit, &len);
    bool back = fseek(file, start, SEEK_SET) == 0;
    if (!ok || !back) return -1;
    if (len > (unsigned long long)(LONG_MAX - start)) return -1;
    return start + (long)len;
}

bool qlcg_factions_write(FILE *file, const MacroWorld *world) {
    FactionSaveHeader header = {0x46414354u, 1u, (uint32_t)world->cell_count,
        (uint32_t)world->faction_count, sizeof(TerritoryFaction), world->last_civ_day};
    size_t cells = header.cell_count, factions = header.faction_count;
    return fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(world->factions, sizeof(TerritoryFaction), factions, file) == factions &&
        fwrite(world->claim_faction, sizeof(int16_t), cells, file) == cells &&
        fwrite(world->claim_type, sizeof(uint8_t), cells, file) == cells;
}

bool qlcg_factions_read_lim(FILE *file, MacroWorld *world, long limit) {
    long start = ftell(file);
    if (start < 0) return false;
    FactionSaveHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        header.magic != 0x46414354u || header.version != 1u ||
        header.cell_count != (uint32_t)world->cell_count ||
        header.faction_count > MAX_FACTIONS || header.faction_size != sizeof(TerritoryFaction)) return false;
    size_t factions = header.faction_count, cells = header.cell_count;
    uint64_t need = sizeof(header) + (uint64_t)factions * sizeof(TerritoryFaction) +
                    (uint64_t)cells * (sizeof(int16_t) + sizeof(uint8_t));
    long end = tail_limit(file, limit);
    if (end < start || (uint64_t)(end - start) < need) return false;
    if (fseek(file, start + (long)sizeof(header), SEEK_SET) != 0) return false;
    if (fread(world->factions, sizeof(TerritoryFaction), factions, file) != factions ||
        fread(world->claim_faction, sizeof(int16_t), cells, file) != cells ||
        fread(world->claim_type, sizeof(uint8_t), cells, file) != cells) return false;
    for (size_t i = 0; i < factions; ++i) {
        const TerritoryFaction *f = &world->factions[i];
        if (f->id != (int)i || f->flag_cell < 0 || f->flag_cell >= world->cell_count ||
            !isfinite(f->wx) || !isfinite(f->wz) ||
            !memchr(f->uid, 0, sizeof(f->uid)) || !memchr(f->name, 0, sizeof(f->name)) ||
            !memchr(f->auto_name, 0, sizeof(f->auto_name)) || !memchr(f->culture, 0, sizeof(f->culture))) return false;
    }
    world->faction_count = (int)factions;
    world->last_civ_day = header.last_civ_day;
    ++world->territory_revision;
    return true;
}
