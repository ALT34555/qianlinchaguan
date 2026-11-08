#ifndef QLCG_I18N_H
#define QLCG_I18N_H

#include "config.h"

#define QLCG_MAX_LOCALES   16
#define QLCG_L10N_DIR      L"userdata/mods/l10n"

typedef struct {
    char code[32];
    char label[64];
    bool is_mod;
    char modified_date[11]; /* 外置语言目录的本地日期 */
} QlLocaleMeta;

/* 初始化 zh-CN/en 并扫描外置语言包 */
void        qlcg_i18n_init(const wchar_t *mod_dir);
/* 重新扫描 userdata/mods/l10n 目录 */
int         qlcg_i18n_rescan(const wchar_t *mod_dir);
/* 获取已注册语言数量与元数据 */
int         qlcg_i18n_count(void);
const QlLocaleMeta *qlcg_i18n_meta(int idx);
/* 切换当前语言（支持前缀匹配，未命中回退 zh-CN） */
void        qlcg_i18n_set_locale(const char *code);
const char *qlcg_i18n_get_locale(void);
int         qlcg_i18n_get_index(void);
/* 按键取当前语言文案（缺键自动回退 zh-CN） */
const char *qlcg_t(const char *key);
const char *qlcg_i18n_name_text(const char *culture, const char *domain, const char *key);
int         qlcg_i18n_culture_count(void);
const QlLocaleMeta *qlcg_i18n_culture_meta(int idx);

static inline const char *tr(const char *key) {
    return qlcg_t(key);
}

#endif /* QLCG_I18N_H */
