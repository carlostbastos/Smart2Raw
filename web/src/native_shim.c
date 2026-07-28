/* Native counterpart of the memory-FILE, so the SAME s2r_probe.c can be built
 * against the real libc and its report compared, field by field, against the
 * report the wasm build produces. A demo that cannot be diffed against a native
 * run is a demo, not evidence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp; static size_t slurp_n;

unsigned char *s2r_memfile_ptr(const char *path){
    FILE *f = fopen(path, "rb");
    long n;
    if(!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    free(slurp);
    slurp = (unsigned char*)malloc(n > 0 ? (size_t)n : 1u);
    slurp_n = 0;
    if(slurp && n > 0) slurp_n = fread(slurp, 1, (size_t)n, f);
    fclose(f);
    return slurp;
}
size_t s2r_memfile_len(const char *path){
    FILE *f = fopen(path, "rb");
    long n;
    if(!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f);
    fclose(f);
    return n > 0 ? (size_t)n : 0u;
}
