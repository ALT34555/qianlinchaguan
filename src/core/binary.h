#ifndef QLCG_BINARY_H
#define QLCG_BINARY_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define QLCG_FILE_LIMIT (512L * 1024L * 1024L)
#define QLCG_SECTION_HEADER 24
typedef struct { FILE *file; bool reading, ok; uint64_t bytes, limit; } QlCodec;
QlCodec qlcg_codec(FILE *file, bool reading, uint64_t limit);
uint64_t qlcg_io_uint(QlCodec *io, uint64_t value, unsigned width);
int32_t qlcg_io_i32(QlCodec *io, int32_t value);
float qlcg_io_f32(QlCodec *io, float value);
double qlcg_io_f64(QlCodec *io, double value);
bool qlcg_io_bool(QlCodec *io, bool value);
void qlcg_io_bytes(QlCodec *io, void *data, size_t size);
void qlcg_io_string(QlCodec *io, char *text, size_t capacity);
bool qlcg_utf8_valid(const char *text, size_t size);
uint32_t qlcg_crc32(uint32_t crc, const void *data, size_t size);
long qlcg_section_begin(FILE *file, const char tag[4]);
bool qlcg_section_finish(FILE *file, long start);
bool qlcg_section_open(FILE *file, const char tag[4], long limit, long *end);
long qlcg_file_size(FILE *file);
#endif
