/* 精简 JSON DOM：供 content 数据装载使用 */
#ifndef QLCG_JS_H
#define QLCG_JS_H

typedef enum { JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ } JsType;

typedef struct JsVal {
    JsType type;
    double num;
    int    bval;
    char  *str;
    struct JsVal **items;  /* 数组元素 / 对象取值 */
    char **keys;           /* 对象键，数组为 NULL */
    int    count;
    int    cap;
} JsVal;

/* 解析失败返回 NULL，err 可空 */
JsVal *qlcg_js_parse(const char *text, const char **err);
JsVal *qlcg_js_load(const char *path, const char **err);
void   qlcg_js_free(JsVal *v);

const JsVal *qlcg_js_get(const JsVal *obj, const char *key);
const JsVal *qlcg_js_at(const JsVal *v, int i);
const char  *qlcg_js_key(const JsVal *v, int i);
int          qlcg_js_count(const JsVal *v);

double      qlcg_js_num(const JsVal *v, double dflt);
const char *qlcg_js_str(const JsVal *v, const char *dflt);
int         qlcg_js_bool(const JsVal *v, int dflt);
int         qlcg_js_has(const JsVal *obj, const char *key);

#endif
