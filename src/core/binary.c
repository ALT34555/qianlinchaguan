#include "binary.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(float)==4 && sizeof(double)==8 && FLT_RADIX==2 && FLT_MANT_DIG==24 && DBL_MANT_DIG==53, "IEEE 754 required");

QlCodec qlcg_codec(FILE *file, bool reading, uint64_t limit) {
    return (QlCodec){file,NULL,reading,true,0,limit};
}
QlCodec qlcg_codec_mem(void *buf, bool reading, uint64_t limit) {
    return (QlCodec){NULL,(uint8_t *)buf,reading,true,0,limit};
}
void qlcg_io_bytes(QlCodec *io, void *data, size_t size) {
    if (!io->ok || io->bytes>io->limit || size>io->limit-io->bytes) { io->ok=false; return; }
    if (io->buf) {
        if (io->reading) memcpy(data, io->buf + io->bytes, size);
        else memcpy(io->buf + io->bytes, data, size);
    } else if (io->file && (io->reading ? fread(data,1,size,io->file) : fwrite(data,1,size,io->file))!=size) io->ok=false;
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
static const uint32_t CRC32_TABLE[256] = {
    0x00000000u,0x77073096u,0xEE0E612Cu,0x990951BAu,0x076DC419u,0x706AF48Fu,0xE963A535u,0x9E6495A3u,
    0x0EDB8832u,0x79DCB8A4u,0xE0D5E91Eu,0x97D2D988u,0x09B64C2Bu,0x7EB17CBDu,0xE7B82D07u,0x90BF1D91u,
    0x1DB71064u,0x6AB020F2u,0xF3B97148u,0x84BE41DEu,0x1ADAD47Du,0x6DDDE4EBu,0xF4D4B551u,0x83D385C7u,
    0x136C9856u,0x646BA8C0u,0xFD62F97Au,0x8A65C9ECu,0x14015C4Fu,0x63066CD9u,0xFA0F3D63u,0x8D080DF5u,
    0x3B6E20C8u,0x4C69105Eu,0xD56041E4u,0xA2677172u,0x3C03E4D1u,0x4B04D447u,0xD20D85FDu,0xA50AB56Bu,
    0x35B5A8FAu,0x42B2986Cu,0xDBBBC9D6u,0xACBCF940u,0x32D86CE3u,0x45DF5C75u,0xDCD60DCFu,0xABD13D59u,
    0x26D930ACu,0x51DE003Au,0xC8D75180u,0xBFD06116u,0x21B4F4B5u,0x56B3C423u,0xCFBA9599u,0xB8BDA50Fu,
    0x2802B89Eu,0x5F058808u,0xC60CD9B2u,0xB10BE924u,0x2F6F7C87u,0x58684C11u,0xC1611DABu,0xB6662D3Du,
    0x76DC4190u,0x01DB7106u,0x98D220BCu,0xEFD5102Au,0x71B18589u,0x06B6B51Fu,0x9FBFE4A5u,0xE8B8D433u,
    0x7807C9A2u,0x0F00F934u,0x9609A88Eu,0xE10E9818u,0x7F6A0DBBu,0x086D3D2Du,0x91646C97u,0xE6635C01u,
    0x6B6B51F4u,0x1C6C6162u,0x856530D8u,0xF262004Eu,0x6C0695EDu,0x1B01A57Bu,0x8208F4C1u,0xF50FC457u,
    0x65B0D9C6u,0x12B7E950u,0x8BBEB8EAu,0xFCB9887Cu,0x62DD1DDFu,0x15DA2D49u,0x8CD37CF3u,0xFBD44C65u,
    0x4DB26158u,0x3AB551CEu,0xA3BC0074u,0xD4BB30E2u,0x4ADFA541u,0x3DD895D7u,0xA4D1C46Du,0xD3D6F4FBu,
    0x4369E96Au,0x346ED9FCu,0xAD678846u,0xDA60B8D0u,0x44042D73u,0x33031DE5u,0xAA0A4C5Fu,0xDD0D7CC9u,
    0x5005713Cu,0x270241AAu,0xBE0B1010u,0xC90C2086u,0x5768B525u,0x206F85B3u,0xB966D409u,0xCE61E49Fu,
    0x5EDEF90Eu,0x29D9C998u,0xB0D09822u,0xC7D7A8B4u,0x59B33D17u,0x2EB40D81u,0xB7BD5C3Bu,0xC0BA6CADu,
    0xEDB88320u,0x9ABFB3B6u,0x03B6E20Cu,0x74B1D29Au,0xEAD54739u,0x9DD277AFu,0x04DB2615u,0x73DC1683u,
    0xE3630B12u,0x94643B84u,0x0D6D6A3Eu,0x7A6A5AA8u,0xE40ECF0Bu,0x9309FF9Du,0x0A00AE27u,0x7D079EB1u,
    0xF00F9344u,0x8708A3D2u,0x1E01F268u,0x6906C2FEu,0xF762575Du,0x806567CBu,0x196C3671u,0x6E6B06E7u,
    0xFED41B76u,0x89D32BE0u,0x10DA7A5Au,0x67DD4ACCu,0xF9B9DF6Fu,0x8EBEEFF9u,0x17B7BE43u,0x60B08ED5u,
    0xD6D6A3E8u,0xA1D1937Eu,0x38D8C2C4u,0x4FDFF252u,0xD1BB67F1u,0xA6BC5767u,0x3FB506DDu,0x48B2364Bu,
    0xD80D2BDAu,0xAF0A1B4Cu,0x36034AF6u,0x41047A60u,0xDF60EFC3u,0xA867DF55u,0x316E8EEFu,0x4669BE79u,
    0xCB61B38Cu,0xBC66831Au,0x256FD2A0u,0x5268E236u,0xCC0C7795u,0xBB0B4703u,0x220216B9u,0x5505262Fu,
    0xC5BA3BBEu,0xB2BD0B28u,0x2BB45A92u,0x5CB36A04u,0xC2D7FFA7u,0xB5D0CF31u,0x2CD99E8Bu,0x5BDEAE1Du,
    0x9B64C2B0u,0xEC63F226u,0x756AA39Cu,0x026D930Au,0x9C0906A9u,0xEB0E363Fu,0x72076785u,0x05005713u,
    0x95BF4A82u,0xE2B87A14u,0x7BB12BAEu,0x0CB61B38u,0x92D28E9Bu,0xE5D5BE0Du,0x7CDCEFB7u,0x0BDBDF21u,
    0x86D3D2D4u,0xF1D4E242u,0x68DDB3F8u,0x1FDA836Eu,0x81BE16CDu,0xF6B9265Bu,0x6FB077E1u,0x18B74777u,
    0x88085AE6u,0xFF0F6A70u,0x66063BCAu,0x11010B5Cu,0x8F659EFFu,0xF862AE69u,0x616BFFD3u,0x166CCF45u,
    0xA00AE278u,0xD70DD2EEu,0x4E048354u,0x3903B3C2u,0xA7672661u,0xD06016F7u,0x4969474Du,0x3E6E77DBu,
    0xAED16A4Au,0xD9D65ADCu,0x40DF0B66u,0x37D83BF0u,0xA9BCAE53u,0xDEBB9EC5u,0x47B2CF7Fu,0x30B5FFE9u,
    0xBDBDF21Cu,0xCABAC28Au,0x53B39330u,0x24B4A3A6u,0xBAD03605u,0xCDD70693u,0x54DE5729u,0x23D967BFu,
    0xB3667A2Eu,0xC4614AB8u,0x5D681B02u,0x2A6F2B94u,0xB40BBE37u,0xC30C8EA1u,0x5A05DF1Bu,0x2D02EF8Du
};
uint32_t qlcg_crc32(uint32_t crc, const void *data, size_t size) {
    const unsigned char *p=data;
    crc=~crc;
    for (size_t i=0;i<size;i++) crc=(crc>>8)^CRC32_TABLE[(crc^p[i])&0xFFu];
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
bool qlcg_section_open_ver(FILE *file, const char tag[4], long limit, uint32_t max_version, uint32_t *out_version, long *end) {
    long at=ftell(file); char got[4];
    if (at<0 || limit<at || limit>QLCG_FILE_LIMIT || limit-at<QLCG_SECTION_HEADER) return false;
    QlCodec io=qlcg_codec(file,true,QLCG_SECTION_HEADER);
    qlcg_io_bytes(&io,got,4); uint64_t version=qlcg_io_uint(&io,0,4);
    uint64_t size=qlcg_io_uint(&io,0,8); uint32_t crc=(uint32_t)qlcg_io_uint(&io,0,4);
    uint64_t reserved=qlcg_io_uint(&io,0,4);
    if (!io.ok || memcmp(got,tag,4) || version<1 || version>max_version || reserved || size>(uint64_t)(limit-at-QLCG_SECTION_HEADER)) return false;
    *end=at+QLCG_SECTION_HEADER+(long)size; uint32_t actual;
    if (out_version) *out_version=(uint32_t)version;
    return file_crc(file,at+QLCG_SECTION_HEADER,*end,&actual) && crc==actual && fseek(file,at+QLCG_SECTION_HEADER,SEEK_SET)==0;
}
bool qlcg_section_open(FILE *file, const char tag[4], long limit, long *end) {
    return qlcg_section_open_ver(file,tag,limit,1,NULL,end);
}
bool qlcg_section_write_buf(FILE *file, const char tag[4], uint32_t version, const void *buf, size_t size) {
    if (!file || !tag || version<1) return false;
    uint8_t hdr[QLCG_SECTION_HEADER]; QlCodec io=qlcg_codec_mem(hdr,false,sizeof(hdr));
    uint32_t crc=qlcg_crc32(0,buf,size);
    qlcg_io_bytes(&io,(void *)tag,4); qlcg_io_uint(&io,version,4);
    qlcg_io_uint(&io,(uint64_t)size,8); qlcg_io_uint(&io,crc,4); qlcg_io_uint(&io,0,4);
    return io.ok && fwrite(hdr,1,sizeof(hdr),file)==sizeof(hdr) && (size==0 || fwrite(buf,1,size,file)==size);
}
bool qlcg_section_open_alloc(FILE *file, const char tag[4], long limit, uint32_t max_version, uint32_t *out_version, uint8_t **out_buf, size_t *out_size, long *end) {
    long at=ftell(file); uint8_t hdr[QLCG_SECTION_HEADER]; char got[4];
    if (!out_buf || !out_size || !end || at<0 || limit<at || limit>QLCG_FILE_LIMIT || limit-at<QLCG_SECTION_HEADER) return false;
    if (fread(hdr,1,sizeof(hdr),file)!=sizeof(hdr)) return false;
    QlCodec io=qlcg_codec_mem(hdr,true,sizeof(hdr));
    qlcg_io_bytes(&io,got,4); uint64_t version=qlcg_io_uint(&io,0,4);
    uint64_t size=qlcg_io_uint(&io,0,8); uint32_t crc=(uint32_t)qlcg_io_uint(&io,0,4);
    uint64_t reserved=qlcg_io_uint(&io,0,4);
    if (!io.ok || memcmp(got,tag,4) || version<1 || version>max_version || reserved || size>(uint64_t)(limit-at-QLCG_SECTION_HEADER) || size>SIZE_MAX) return false;
    uint8_t *buf=malloc(size?(size_t)size:1);
    if (!buf) return false;
    if (size && fread(buf,1,(size_t)size,file)!=(size_t)size) { free(buf); return false; }
    if (qlcg_crc32(0,buf,(size_t)size)!=crc) { free(buf); return false; }
    if (out_version) *out_version=(uint32_t)version;
    *out_buf=buf; *out_size=(size_t)size; *end=at+QLCG_SECTION_HEADER+(long)size;
    return true;
}

static size_t lz4_write_len(uint8_t *op, uint8_t *oend, size_t len) {
    size_t n=0;
    while (len>=255) { if (op+n>=oend) return 0; op[n++]=255; len-=255; }
    if (op+n>=oend) return 0;
    op[n++]=(uint8_t)len;
    return n;
}
size_t qlcg_lz4_bound(size_t src_size) {
    return src_size+(src_size/255u)+16u;
}
size_t qlcg_lz4_compress(const void *src, size_t src_size, void *dst, size_t dst_cap) {
    if (!src_size) return 0;
    if (!src || !dst || dst_cap<qlcg_lz4_bound(src_size)) return 0;
    const uint8_t *ip=(const uint8_t *)src, *iend=ip+src_size, *anchor=ip;
    uint8_t *op=(uint8_t *)dst, *oend=op+dst_cap;
    uint32_t ht[4096]={0};
    if (src_size>=13) {
        const uint8_t *mflimit=iend-12;
        ip++;
        while (ip<=mflimit) {
            uint32_t seq; memcpy(&seq,ip,4);
            uint32_t h=(seq*2654435761u)>>20;
            uint32_t ref_pos=ht[h];
            ht[h]=(uint32_t)(ip-(const uint8_t *)src)+1u;
            const uint8_t *ref=ref_pos?((const uint8_t *)src+ref_pos-1u):NULL;
            uint32_t ref_seq=0;
            if (ref && (size_t)(ip-ref)<=65535u) memcpy(&ref_seq,ref,4);
            if (!ref || (size_t)(ip-ref)>65535u || ref_seq!=seq) { ip++; continue; }
            size_t lit=(size_t)(ip-anchor);
            uint8_t *tok=op++; if (op>oend) return 0;
            if (lit>=15) {
                *tok=0xF0u; size_t w=lz4_write_len(op,oend,lit-15u); if (!w) return 0; op+=w;
            } else *tok=(uint8_t)(lit<<4);
            if ((size_t)(oend-op)<lit+2u) return 0;
            if (lit) { memcpy(op,anchor,lit); op+=lit; }
            uint16_t off=(uint16_t)(ip-ref);
            op[0]=(uint8_t)(off&0xFFu); op[1]=(uint8_t)(off>>8); op+=2;
            const uint8_t *mlimit=iend-5;
            size_t mlen=4;
            while (ip+mlen<mlimit && ref[mlen]==ip[mlen]) mlen++;
            size_t extra=mlen-4u;
            if (extra>=15) {
                *tok|=0x0Fu; size_t w=lz4_write_len(op,oend,extra-15u); if (!w) return 0; op+=w;
            } else *tok|=(uint8_t)extra;
            ip+=mlen; anchor=ip;
        }
    }
    size_t last=(size_t)(iend-anchor);
    uint8_t *tok=op++; if (op>oend) return 0;
    if (last>=15) {
        *tok=0xF0u; size_t w=lz4_write_len(op,oend,last-15u); if (!w) return 0; op+=w;
    } else *tok=(uint8_t)(last<<4);
    if ((size_t)(oend-op)<last) return 0;
    if (last) { memcpy(op,anchor,last); op+=last; }
    return (size_t)(op-(uint8_t *)dst);
}
bool qlcg_lz4_decompress(const void *src, size_t src_size, void *dst, size_t dst_size) {
    if (!dst_size) return src_size==0;
    if (!src || !dst || !src_size) return false;
    const uint8_t *ip=(const uint8_t *)src, *iend=ip+src_size;
    uint8_t *op=(uint8_t *)dst, *oend=op+dst_size;
    while (ip<iend) {
        uint8_t tok=*ip++;
        size_t lit=tok>>4;
        if (lit==15) {
            uint8_t s;
            do { if (ip>=iend || lit>dst_size) return false; s=*ip++; lit+=s; } while (s==255);
        }
        if (lit>(size_t)(iend-ip) || lit>(size_t)(oend-op)) return false;
        if (lit) { memcpy(op,ip,lit); ip+=lit; op+=lit; }
        if (ip>=iend) break;
        if ((size_t)(iend-ip)<2u) return false;
        size_t off=(size_t)ip[0]|((size_t)ip[1]<<8); ip+=2;
        if (!off || off>(size_t)(op-(uint8_t *)dst)) return false;
        size_t mlen=4u+(tok&0x0Fu);
        if ((tok&0x0Fu)==15) {
            uint8_t s;
            do { if (ip>=iend || mlen>dst_size) return false; s=*ip++; mlen+=s; } while (s==255);
        }
        if (mlen>(size_t)(oend-op)) return false;
        const uint8_t *ref=op-off;
        while (mlen--) *op++=*ref++;
    }
    return ip==iend && op==oend;
}
