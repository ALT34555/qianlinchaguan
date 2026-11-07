/* 按语言目录装载独立内容域 */
#include <windows.h>
#include "locale_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JsVal *load_domain(const wchar_t *directory, const wchar_t *file) {
    wchar_t path[1024];
    if (swprintf_s(path, 1024, L"%ls\\%ls", directory, file) < 0) return NULL;
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long length = ftell(fp);
    if (length < 0 || length > 1048576 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *bytes = malloc((size_t)length + 1);
    if (!bytes) { fclose(fp); return NULL; }
    bool ok = fread(bytes, 1, (size_t)length, fp) == (size_t)length;
    fclose(fp);
    bytes[length] = '\0';
    const char *text = bytes;
    if (length >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) text += 3;
    JsVal *value = ok ? qlcg_js_parse(text, NULL) : NULL;
    free(bytes);
    if (value && value->type != JS_OBJ) { qlcg_js_free(value); return NULL; }
    return value;
}

void qlcg_locale_pack_free(QlLocalePack *pack) {
    qlcg_js_free(pack->ui);
    qlcg_js_free(pack->flags);
    qlcg_js_free(pack->people);
    memset(pack, 0, sizeof(*pack));
}

bool qlcg_locale_pack_load(const wchar_t *root, const wchar_t *folder, QlLocalePack *pack) {
    wchar_t directory[1024];
    memset(pack, 0, sizeof(*pack));
    if (swprintf_s(directory, 1024, L"%ls\\%ls", root, folder) < 0) return false;
    JsVal *manifest = load_domain(directory, L"manifest.json");
    if (!manifest) return false;
    const char *code = qlcg_js_str(qlcg_js_get(manifest, "code"), "");
    const char *label = qlcg_js_str(qlcg_js_get(manifest, "label"), code);
    if (!*code || strlen(code) >= sizeof(pack->meta.code) || strspn(code,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != strlen(code)) {
        qlcg_js_free(manifest);
        return false;
    }
    snprintf(pack->meta.code, sizeof(pack->meta.code), "%s", code);
    snprintf(pack->meta.label, sizeof(pack->meta.label), "%s", label);
    snprintf(pack->name_fallback, sizeof(pack->name_fallback), "%s",
             qlcg_js_str(qlcg_js_get(manifest, "nameFallback"), "en"));
    pack->meta.is_mod = true;
    qlcg_js_free(manifest);
    pack->ui = load_domain(directory, L"ui.json");
    pack->flags = load_domain(directory, L"names\\flags.json");
    pack->people = load_domain(directory, L"names\\people.json");
    if (!pack->ui && !pack->flags) { qlcg_locale_pack_free(pack); return false; }
    return true;
}

const char *qlcg_locale_pack_text(const QlLocalePack *pack, const char *domain, const char *key) {
    const JsVal *map = strcmp(domain, "ui") == 0 ? pack->ui :
                       strcmp(domain, "flags") == 0 ? pack->flags :
                       strcmp(domain, "people") == 0 ? pack->people : NULL;
    const char *value = qlcg_js_str(qlcg_js_get(map, key), NULL);
    return value && *value ? value : NULL;
}
