#ifndef QLCG_LOCALE_PACK_H
#define QLCG_LOCALE_PACK_H

#include "i18n.h"
#include "../systems/world/model/model_json.h"

typedef struct {
    QlLocaleMeta meta;
    char name_fallback[32];
    JsVal *ui;
    JsVal *flags;
    JsVal *people;
} QlLocalePack;

bool qlcg_locale_pack_load(const wchar_t *root, const wchar_t *folder, QlLocalePack *pack);
void qlcg_locale_pack_free(QlLocalePack *pack);
const char *qlcg_locale_pack_text(const QlLocalePack *pack, const char *domain, const char *key);

#endif
