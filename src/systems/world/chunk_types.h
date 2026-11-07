#ifndef QLCG_CHUNK_TYPES_H
#define QLCG_CHUNK_TYPES_H

#include "../../core/config.h"

/* 区块类型定义（对齐 ChunkTypes */
typedef struct {
    int         id;
    const char *code;
    const char *key;
    const char *name;
    uint32_t    map_color;
    bool        is_water;
    bool        is_river;
    bool        is_waterfall;
    bool        is_wetland;
    bool        is_artificial;
    const char *desc;
} ChunkTypeDef;

/* 25 种天气配置定义（对齐 Weathe */
typedef struct {
    int         id;
    const char *code;
    const char *name;
    float       clouds;
    float       dim;
    float       wind;
    float       rain;
    float       snow;
    float       hail;
    float       dust;
    float       fog;
    uint32_t    color;
    bool        lightning;
    bool        vortex;
} WeatherProfile;

/* 区块天气采样快照 */
typedef struct {
    int                   cx, cz;
    int                   weather_id;
    float                 cloud_cover;
    float                 intensity;
    float                 temperature;
    const WeatherProfile *profile;
} ChunkWeather;

/* 天气系统运行时 */
typedef struct {
    uint32_t seed;
    int      forced_id;
    int      current_id;
    const struct MacroWorld   *world;
    const struct PrecipField  *precip;
} WeatherSystem;

/* 区块类型与天气查询接口 */
const ChunkTypeDef   *qlcg_get_chunk_type(int type_id);
const char          *qlcg_chunk_type_name(const ChunkTypeDef *def);
const char          *qlcg_chunk_type_description(const ChunkTypeDef *def);
int                   qlcg_classify_chunk(bool is_water, bool is_river, bool is_fall, bool is_wetland,
                                          bool is_saline, float elev, float temp, float aridity,
                                          float relief, float slope, float relative, int claim_type);
/* 区块类型→自然基质 1..5；无效 0 */
int                   qlcg_chunk_material(int type_id);
const char           *qlcg_climate_band_name(int band);
uint32_t              qlcg_climate_band_color(int band);
int                   qlcg_climate_band_of_temp(float temp);
uint32_t              qlcg_chunk_display_color(const ChunkTypeDef *def);

const WeatherProfile *qlcg_get_weather_profile(int weather_id);
void                  qlcg_weather_init(WeatherSystem *ws, uint32_t seed);
void                  qlcg_weather_set_seed(WeatherSystem *ws, uint32_t seed);
void                  qlcg_weather_bind_world(WeatherSystem *ws, const struct MacroWorld *w);
ChunkWeather          qlcg_sample_chunk_weather(const WeatherSystem *ws, double unix_ms,
                                                int cx, int cz, float temp, int eq_chunks);
bool                  qlcg_weather_command(WeatherSystem *ws, const char *cmd, char *reply, size_t cap);
/* 降水总量、极昼/极夜日比、风速等级 0..5 */
float                 qlcg_weather_precipitation(int weather_id);
float                 qlcg_weather_day_ratio(int weather_id, float ratio);
int                   qlcg_natural_physics_level(int weather_id);

#endif /* QLCG_CHUNK_TYPES_H */
