#include "binary.h"
#include <float.h>
#include <math.h>
#include <string.h>
_Static_assert(sizeof(float)==4 && sizeof(double)==8 && FLT_RADIX==2 && FLT_MANT_DIG==24 && DBL_MANT_DIG==53, "IEEE 754 required");

QlCodec qlcg_codec(FILE *file, bool reading, uint64_t limit) {
    return (QlCodec){file,reading,true,0,limit};
}
void qlcg_io_bytes(QlCodec *io, void *data, size_t size) {
    if (!io->ok || io->bytes>io->limit || size>io->limit-io->bytes) { io->ok=false; return; }
    if (io->file && (io->reading ? fread(data,1,size,io->file) : fwrite(data,1,size,io->file))!=size) io->ok=false;
    io->bytes+=size;
}
uint64_t qlcg_io_uint(QlCodec *io, uint64_t value, unsigned width) {
    unsigned char bytes[8]={0};
    if (width<1 || width>8) { io->ok=false; return 0; }
    for (unsigned i=0;i<width;i++) bytes[i]=(unsigned char)(value>>(8*i));
    qlcg_io_bytes(io,bytes,width);
    value=0;
    for (unsigned i=0;i<width;i++) value|=(uint64_t)bytes[i]<<(8*i);
    return value;
}
int32_t qlcg_io_i32(QlCodec *io, int32_t value) {
    uint32_t u=(uint32_t)qlcg_io_uint(io,(uint32_t)value,4);
    return u<=INT32_MAX ? (int32_t)u : -1-(int32_t)(UINT32_MAX-u);
}
float qlcg_io_f32(QlCodec *io, float value) {
    uint32_t bits; memcpy(&bits,&value,4);
    bits=(uint32_t)qlcg_io_uint(io,bits,4); memcpy(&value,&bits,4);
    if (!isfinite(value)) io->ok=false;
    return value;
}
double qlcg_io_f64(QlCodec *io, double value) {
    uint64_t bits; memcpy(&bits,&value,8);
    bits=qlcg_io_uint(io,bits,8); memcpy(&value,&bits,8);
    if (!isfinite(value)) io->ok=false;
    return value;
}
bool qlcg_io_bool(QlCodec *io, bool value) {
    uint64_t v=qlcg_io_uint(io,value?1:0,1);
    if (v>1) io->ok=false;
    return v==1;
}
bool qlcg_utf8_valid(const char *text, size_t size) {
    size_t at=0;
    while (at<size) {
        uint32_t cp=(unsigned char)text[at++], minimum=0; unsigned more=0;
        if (!cp) return false;
        if (cp>=0xC2 && cp<=0xDF) { cp&=31; more=1; minimum=0x80; }
        else if (cp>=0xE0 && cp<=0xEF) { cp&=15; more=2; minimum=0x800; }
        else if (cp>=0xF0 && cp<=0xF4) { cp&=7; more=3; minimum=0x10000; }
        else if (cp>=0x80) return false;
        if (more>size-at) return false;
        while (more--) { unsigned c=(unsigned char)text[at++]; if ((c&0xC0)!=0x80) return false; cp=(cp<<6)|(c&63); }
        if (cp<minimum || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) return false;
    }
    return true;
}
void qlcg_io_string(QlCodec *io, char *text, size_t capacity) {
    unsigned char raw[1024]={0};
    if (!capacity || capacity>sizeof(raw)) { io->ok=false; return; }
    size_t length=0;
    if (!io->reading) {
        while (length<capacity && text[length]) length++;
        if (length==capacity || !qlcg_utf8_valid(text,length)) { io->ok=false; return; }
        memcpy(raw,text,length);
    }
    length=(size_t)qlcg_io_uint(io,length,2);
    qlcg_io_bytes(io,raw,capacity);
    if (length>=capacity || !qlcg_utf8_valid((char *)raw,length)) { io->ok=false; return; }
    for (size_t i=length;i<capacity;i++) if (raw[i]) io->ok=false;
    if (io->reading && io->ok) memcpy(text,raw,capacity);
}
uint32_t qlcg_crc32(uint32_t crc, const void *data, size_t size) {
    const unsigned char *p=data;
    crc=~crc;
    for (size_t i=0;i<size;i++) {
        crc^=p[i];
        for (unsigned k=0;k<8;k++) crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
long qlcg_file_size(FILE *file) {
    long at=ftell(file);
    if (at<0 || fseek(file,0,SEEK_END)) return -1;
    long size=ftell(file);
    if (fseek(file,at,SEEK_SET) || size<0 || size>QLCG_FILE_LIMIT) return -1;
    return size;
}
static bool file_crc(FILE *file, long start, long end, uint32_t *crc) {
    unsigned char bytes[16384]; *crc=0;
    if (start<0 || end<start || end>QLCG_FILE_LIMIT || fseek(file,start,SEEK_SET)) return false;
    while (start<end) {
        size_t n=(size_t)(end-start); if (n>sizeof(bytes)) n=sizeof(bytes);
        if (fread(bytes,1,n,file)!=n) return false;
        *crc=qlcg_crc32(*crc,bytes,n); start+=(long)n;
    }
    return true;
}
long qlcg_section_begin(FILE *file, const char tag[4]) {
    long at=ftell(file); QlCodec io=qlcg_codec(file,false,QLCG_SECTION_HEADER);
    qlcg_io_bytes(&io,(void *)tag,4); qlcg_io_uint(&io,1,4);
    qlcg_io_uint(&io,0,8); qlcg_io_uint(&io,0,4); qlcg_io_uint(&io,0,4);
    return io.ok?at:-1;
}
bool qlcg_section_finish(FILE *file, long start) {
    long end=ftell(file); uint32_t crc;
    if (start<0 || end<start+QLCG_SECTION_HEADER || fflush(file) || !file_crc(file,start+QLCG_SECTION_HEADER,end,&crc) || fseek(file,start+8,SEEK_SET)) return false;
    QlCodec io=qlcg_codec(file,false,12);
    qlcg_io_uint(&io,(uint64_t)(end-start-QLCG_SECTION_HEADER),8); qlcg_io_uint(&io,crc,4);
    return io.ok && fseek(file,end,SEEK_SET)==0;
}
bool qlcg_section_open(FILE *file, const char tag[4], long limit, long *end) {
    long at=ftell(file); char got[4];
    if (at<0 || limit<at || limit>QLCG_FILE_LIMIT || limit-at<QLCG_SECTION_HEADER) return false;
    QlCodec io=qlcg_codec(file,true,QLCG_SECTION_HEADER);
    qlcg_io_bytes(&io,got,4); uint64_t version=qlcg_io_uint(&io,0,4);
    uint64_t size=qlcg_io_uint(&io,0,8); uint32_t crc=(uint32_t)qlcg_io_uint(&io,0,4);
    uint64_t reserved=qlcg_io_uint(&io,0,4);
    if (!io.ok || memcmp(got,tag,4) || version!=1 || reserved || size>(uint64_t)(limit-at-QLCG_SECTION_HEADER)) return false;
    *end=at+QLCG_SECTION_HEADER+(long)size; uint32_t actual;
    return file_crc(file,at+QLCG_SECTION_HEADER,*end,&actual) && crc==actual && fseek(file,at+QLCG_SECTION_HEADER,SEEK_SET)==0;
}
