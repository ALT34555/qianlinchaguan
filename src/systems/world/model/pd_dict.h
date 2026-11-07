/* 稀疏参数/色板字典：键名沿用 JSON */
#ifndef QLCG_PD_DICT_H
#define QLCG_PD_DICT_H

typedef struct {
    char  *key;
    double num;
    char  *str;
    int    is_num;
} PdEntry;

typedef struct {
    PdEntry *e;
    int      count;
    int      cap;
} PdDict;

void  pd_init(PdDict *d);
void  pd_free(PdDict *d);

void  pd_set_num(PdDict *d, const char *key, double v);
void  pd_set_str(PdDict *d, const char *key, const char *v);
int   pd_has(const PdDict *d, const char *key);
/* 缺失返回 dflt；字符串型视为缺失 */
double pd_num(const PdDict *d, const char *key, double dflt);
const char *pd_str(const PdDict *d, const char *key, const char *dflt);

/* 覆盖式合并，对齐 JS 展开语义 */
void  pd_merge(PdDict *dst, const PdDict *src);
/* 数值键乘以系数，仅处理数值 */
void  pd_scale_keys(PdDict *d, const char *const *keys, int key_count, double scale);

#endif
