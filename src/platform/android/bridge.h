#ifndef QLCG_ANDROID_BRIDGE_H
#define QLCG_ANDROID_BRIDGE_H
#ifdef __ANDROID__
#include <stdbool.h>
#include <stddef.h>
bool qlcg_android_import_request(void);
bool qlcg_android_export_request(const char *path);
bool qlcg_android_document_poll(char *kind, size_t kind_cap, char *path, size_t path_cap, char *error, size_t error_cap);
#endif
#endif
