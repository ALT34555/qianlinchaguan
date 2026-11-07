/* 阵营命名策略与显示回退 */
#include "faction_naming.h"
#include "../../core/math_util.h"
#include "../../core/i18n.h"
#include <stdio.h>
#include <string.h>

void qlcg_faction_init_identity(TerritoryFaction *faction, uint32_t seed,
                                const char *salt, const char *name) {
    char base[96], keyed[112];
    snprintf(base, sizeof(base), "%u:%d,%d:%s", seed,
             faction->cx, faction->cz, salt);
    snprintf(keyed, sizeof(keyed), "%s:faction", base);
    snprintf(faction->uid, sizeof(faction->uid), "%08x%08x",
             qlcg_hash_str(base), qlcg_hash_str(keyed));
    faction->origin_type_id = 0;
    faction->culture[0] = '\0';
    faction->auto_name[0] = '\0';
    qlcg_faction_set_name(faction, name);
}

static void copy_name(char *out, size_t cap, const char *name) {
    if (!name) name = "";
    size_t length = strlen(name);
    if (length >= cap) {
        length = cap - 1;
        while (length && ((unsigned char)name[length] & 0xC0) == 0x80) --length;
    }
    memmove(out, name, length);
    out[length] = '\0';
}

void qlcg_faction_set_name(TerritoryFaction *faction, const char *name) {
    copy_name(faction->name, sizeof(faction->name), name);
}

static bool format_name(char *out, size_t cap, const char *pattern,
                        const char *climate, const char *terrain) {
    if (!pattern || !climate || !terrain) return false;
    size_t used = 0;
    bool has_climate = false, has_terrain = false;
    while (*pattern) {
        const char *part = pattern;
        size_t count = 1;
        if (strncmp(pattern, "{climate}", 9) == 0) {
            part = climate; count = strlen(part); pattern += 9; has_climate = true;
        } else if (strncmp(pattern, "{terrain}", 9) == 0) {
            part = terrain; count = strlen(part); pattern += 9; has_terrain = true;
        } else {
            if (*pattern == '{' || *pattern == '}') return false;
            ++pattern;
        }
        if (used + count >= cap) return false;
        memcpy(out + used, part, count);
        used += count;
    }
    out[used] = '\0';
    return has_climate && has_terrain && used > 0;
}

bool qlcg_culture_checked(const WorldCreatorOptions *opt, const char *code) {
    if (opt->culture[0])
        return strcmp(opt->culture, "mixed") == 0 || strcmp(opt->culture, code) == 0;
    for (int i = 0; i < QLCG_MAX_LOCALES; ++i) {
        if (strncmp(opt->culture_choices[i], code, sizeof(opt->culture_choices[i])) == 0)
            return true;
    }
    return false;
}

static const char *culture_for_name(const WorldCreatorOptions *opt, uint32_t random) {
    if (opt->culture[0] && strcmp(opt->culture, "mixed") != 0) return opt->culture;

    const char *choices[QLCG_MAX_LOCALES];
    int count = 0;
    int available = qlcg_i18n_culture_count();
    for (int i = 0; i < available; ++i) {
        const char *code = qlcg_i18n_culture_meta(i)->code;
        if (qlcg_culture_checked(opt, code)) choices[count++] = code;
    }
    if (count == 0) return qlcg_i18n_get_locale();
    return choices[random % (uint32_t)count];
}

void qlcg_faction_generate_name(TerritoryFaction *faction, int natural_type, const WorldCreatorOptions *opt) {
    uint32_t random = qlcg_hash_str(faction->uid);
    const char *culture = culture_for_name(opt, random);
    copy_name(faction->culture, sizeof(faction->culture), culture);
    faction->origin_type_id = natural_type;
    int climate = natural_type / 100, material = natural_type / 10 % 10, terrain = natural_type % 10;
    char climate_key[24], terrain_key[24];
    snprintf(climate_key, sizeof(climate_key), "climate.%d", climate);
    snprintf(terrain_key, sizeof(terrain_key), "terrain.%d.%d", material, terrain);
    bool valid = climate >= 1 && climate <= 5 && material >= 0 && material < 6 && terrain >= 1 && terrain <= 5;
    if (valid && format_name(faction->auto_name, sizeof(faction->auto_name),
            qlcg_i18n_name_text(culture, "flags", "format"),
            qlcg_i18n_name_text(culture, "flags", climate_key),
            qlcg_i18n_name_text(culture, "flags", terrain_key))) return;
    char fallback_key[24];
    snprintf(fallback_key, sizeof(fallback_key), "fallback.%u", (random ^ 0x9E3779B9u) % 4);
    copy_name(faction->auto_name, sizeof(faction->auto_name),
              qlcg_i18n_name_text(culture, "flags", fallback_key));
}

void qlcg_world_set_culture(MacroWorld *world, const WorldCreatorOptions *opt) {
    copy_name(world->opt.culture, sizeof(world->opt.culture), opt->culture);
    memmove(world->opt.culture_choices, opt->culture_choices, sizeof(opt->culture_choices));
    for (int i = 0; i < world->faction_count; ++i) {
        TerritoryFaction *faction = &world->factions[i];
        qlcg_faction_generate_name(faction, faction->origin_type_id, &world->opt);
    }
    ++world->territory_revision;
}

const char *qlcg_faction_display_name(const TerritoryFaction *faction) {
    if (faction->name[0]) return faction->name;
    if (faction->auto_name[0]) return faction->auto_name;
    return qlcg_i18n_name_text(faction->culture[0] ? faction->culture : "zh-CN", "flags", "fallback.0");
}
