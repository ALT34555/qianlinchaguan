#ifndef QLCG_PATHS_H
#define QLCG_PATHS_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
bool qlcg_paths_configure(const char *resources, const char *data, const char *cache);
bool qlcg_path_resolve(const char *logical, bool writing, char *out, size_t capacity);
bool qlcg_path_wide(const wchar_t *logical, bool writing, wchar_t *out, size_t capacity);
#endif
