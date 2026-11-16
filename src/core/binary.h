#ifndef QLCG_BINARY_H
#define QLCG_BINARY_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define QLCG_FILE_LIMIT (512L * 1024L * 1024L)
#define QLCG_SECTION_HEADER 24
typedef struct { FILE *file; uint8_t *buf; bool reading, ok; uint64_t bytes, limit; } QlCodec;
QlCodec qlcg_codec(FILE *file, bool reading, uint64_t limit);
QlCodec qlcg_codec_mem(void *buf, bool reading, uint64_t limit);
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
bool qlcg_section_open_ver(FILE *file, const char tag[4], long limit, uint32_t max_version, uint32_t *out_version, long *end);
bool qlcg_section_write_buf(FILE *file, const char tag[4], uint32_t version, const void *buf, size_t size);
bool qlcg_section_open_alloc(FILE *file, const char tag[4], long limit, uint32_t max_version, uint32_t *out_version, uint8_t **out_buf, size_t *out_size, long *end);
size_t qlcg_lz4_bound(size_t src_size);
size_t qlcg_lz4_compress(const void *src, size_t src_size, void *dst, size_t dst_cap);
bool qlcg_lz4_decompress(const void *src, size_t src_size, void *dst, size_t dst_size);
long qlcg_file_size(FILE *file);
#endif
