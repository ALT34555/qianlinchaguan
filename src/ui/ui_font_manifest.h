#ifndef QLCG_FONT_MANIFEST_H
#define QLCG_FONT_MANIFEST_H
#define UI_FONT_MANIFEST_CAP 64
typedef struct { char file[260],id[96],label[96]; } UiFontManifest;
int ui_font_manifest_read(const char *dir,UiFontManifest *out,int cap);
#endif
