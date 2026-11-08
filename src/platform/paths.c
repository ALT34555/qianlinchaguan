#include "paths.h"
#include "platform.h"
#include "core/binary.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static char resource_root[2048]=".",data_root[2048]=".",cache_root[2048]="cache";
static bool copy_root(char *out, const char *root) {
    if(!root || !*root || strlen(root)>=2048 || !qlcg_utf8_valid(root,strlen(root))) return false;
    strcpy(out,root);
    for(char *p=out;*p;p++) if(*p=='\\') *p='/';
    return true;
}
bool qlcg_paths_configure(const char *resources, const char *data, const char *cache) {
    char r[2048],d[2048],c[2048];
    if(!copy_root(r,resources) || !copy_root(d,data) || !copy_root(c,cache)) return false;
    strcpy(resource_root,r); strcpy(data_root,d); strcpy(cache_root,c); return true;
}
bool qlcg_path_resolve(const char *logical, bool writing, char *out, size_t capacity) {
    char path[2048];
    if(!logical || !*logical || strlen(logical)>=sizeof(path) || !out || !capacity || !qlcg_utf8_valid(logical,strlen(logical))) return false;
    strcpy(path,logical);
    for(char *p=path;*p;p++) if(*p=='\\') *p='/';
    bool absolute=path[0]=='/' || (isalpha((unsigned char)path[0]) && path[1]==':');
    if(absolute) { int n=snprintf(out,capacity,"%s",path); return n>=0 && (size_t)n<capacity; }
    const char *part=path;
    while(*part) {
        const char *slash=strchr(part,'/'); size_t n=slash?(size_t)(slash-part):strlen(part);
        if(!n || (n==1 && part[0]=='.') || (n==2 && part[0]=='.' && part[1]=='.') || memchr(part,':',n)) return false;
        part=slash?slash+1:part+n;
    }
    bool resource=!strncmp(path,"content/",8) || !strncmp(path,"src/systems/world/model/",24);
    if(resource && writing) return false;
    const char *base=resource?resource_root:data_root,*tail=path;
    if(!strcmp(path,"cache") || !strncmp(path,"cache/",6)) { base=cache_root; tail=path+5; if(*tail=='/') tail++; }
    int n=snprintf(out,capacity,"%s%s%s",base,*tail?"/":"",tail);
    return n>=0 && (size_t)n<capacity;
}
bool qlcg_path_wide(const wchar_t *logical, bool writing, wchar_t *out, size_t capacity) {
    char path[4096],resolved[4096];
    return qlcg_wide_to_utf8(logical,path,sizeof(path)) && qlcg_path_resolve(path,writing,resolved,sizeof(resolved)) &&
        qlcg_utf8_to_wide(resolved,out,capacity)>0;
}
