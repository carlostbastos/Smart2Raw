/* Runs s2r_probe on every column in shapes.bin and prints the raw report, so it
 * can be diffed against the browser's. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

uint64_t *s2r_probe_input(uint32_t n);
int       s2r_probe_run(uint32_t n, int is_signed);
uint64_t *s2r_probe_report(void);
uint32_t  s2r_probe_slots(void);
uint32_t  s2r_probe_count_gt_naive(uint64_t t);
uint32_t  s2r_probe_count_gt_s2r(uint64_t t);
uint32_t  s2r_probe_file_len(void);

int main(int argc, char **argv){
    const char *path = argc > 1 ? argv[1] : "out/shapes.bin";
    FILE *f = fopen(path, "rb");
    uint32_t nshapes, s;
    if(!f){ fprintf(stderr, "cannot open %s\n", path); return 1; }
    if(fread(&nshapes, 4, 1, f) != 1) return 1;
    for(s = 0; s < nshapes; s++){
        uint32_t n, sg, i;
        uint64_t *buf;
        if(fread(&n, 4, 1, f) != 1) return 1;
        if(fread(&sg, 4, 1, f) != 1) return 1;
        buf = s2r_probe_input(n);
        if(!buf){ fprintf(stderr, "oom\n"); return 1; }
        if(fread(buf, 8, n, f) != n) return 1;
        s2r_probe_run(n, (int)sg);
        printf("shape %u", s);
        for(i = 0; i < s2r_probe_slots(); i++)
            printf(" %llu", (unsigned long long)s2r_probe_report()[i]);
        /* two live queries, so the query path is diffed too */
        printf(" q %u %u", s2r_probe_count_gt_s2r(1000), s2r_probe_count_gt_naive(1000));
        printf(" f %u\n", s2r_probe_file_len());
    }
    fclose(f);
    return 0;
}
