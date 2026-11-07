/* 内置文化的置旗地命名表 */
#include "name_catalog.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *pattern;
    const char *climates[5];
    const char *terrain[6][5];
    const char *fallback[4];
} FlagNameTable;

static const FlagNameTable ZH = {
    "{climate}{terrain}之地",
    {"冰封", "寒冷", "温和", "温暖", "炎热"},
    {
        {"基原", "基洼", "基丘", "基崖", "基壑"},
        {"石原", "石洼", "石丘", "石崖", "石壑"},
        {"土原", "土洼", "土丘", "土崖", "土壑"},
        {"盐原", "盐洼", "盐丘", "盐崖", "盐壑"},
        {"平沙", "沙洼", "沙丘", "沙崖", "沙壑"},
        {"砾原", "砾洼", "砾丘", "砾崖", "砾壑"}
    },
    {"远野之地", "初望之地", "新垣之地", "未名之地"}
};

static const FlagNameTable EN = {
    "{climate} {terrain}",
    {"Frozen", "Cold", "Temperate", "Subtropical", "Tropical"},
    {
        {"Substrate Plains", "Substrate Hollow", "Substrate Heights", "Substrate Cliffs", "Substrate Ravines"},
        {"Rocky Plains", "Rock Hollow", "Rocky Heights", "Rock Cliffs", "Rock Ravines"},
        {"Earthen Plains", "Earthen Hollow", "Earthen Heights", "Earthen Cliffs", "Earthen Ravines"},
        {"Salt Plains", "Salt Hollow", "Salt Hills", "Salt Cliffs", "Salt Ravines"},
        {"Sandy Plains", "Sand Hollow", "Sand Hills", "Sand Cliffs", "Sand Ravines"},
        {"Gravel Plains", "Gravel Hollow", "Gravel Hills", "Gravel Cliffs", "Gravel Ravines"}
    },
    {"Farfield", "Firstwatch", "Newhaven", "Uncharted Lands"}
};

const char *qlcg_builtin_flag_text(const char *culture, const char *key) {
    const FlagNameTable *table = strcmp(culture, "zh-CN") == 0 ? &ZH : &EN;
    int a, b;
    char extra;
    if (strcmp(key, "format") == 0) return table->pattern;
    if (sscanf(key, "climate.%d%c", &a, &extra) == 1 && a >= 1 && a <= 5)
        return table->climates[a - 1];
    if (sscanf(key, "terrain.%d.%d%c", &a, &b, &extra) == 2 && a >= 0 && a < 6 && b >= 1 && b <= 5)
        return table->terrain[a][b - 1];
    if (sscanf(key, "fallback.%d%c", &a, &extra) == 1 && a >= 0 && a < 4)
        return table->fallback[a];
    return NULL;
}
