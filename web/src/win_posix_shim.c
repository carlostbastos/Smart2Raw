/* Implementa, sobre POSIX, o punhado de chamadas do kernel32 que o
 * probe_main_win.c usa - para que o MESMO código do executável de Windows
 * possa ser executado e conferido aqui. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef void* HANDLE; typedef unsigned long DWORD; typedef int BOOL;
static char cmdline[4096];
int main_(void);

char*  GetCommandLineA(void){ return cmdline; }
HANDLE GetStdHandle(DWORD w){ (void)w; return (HANDLE)(size_t)1; }
BOOL   WriteFile(HANDLE h, const void *p, DWORD n, DWORD *w, void *o){
    ssize_t r = write((int)(size_t)h, p, n); (void)o;
    if(w) *w = r > 0 ? (DWORD)r : 0; return r == (ssize_t)n;
}
BOOL   ReadFile(HANDLE h, void *p, DWORD n, DWORD *g, void *o){
    ssize_t r = read((int)(size_t)h, p, n); (void)o;
    if(g) *g = r > 0 ? (DWORD)r : 0; return r >= 0;
}
HANDLE CreateFileA(const char *path, DWORD acc, DWORD sh, void *sa,
                   DWORD disp, DWORD fl, HANDLE t){
    int fd; (void)sh; (void)sa; (void)fl; (void)t;
    if(acc & 0x40000000u) fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    else                  fd = open(path, O_RDONLY);
    (void)disp;
    return fd < 0 ? (HANDLE)(size_t)-1 : (HANDLE)(size_t)fd;
}
BOOL   CloseHandle(HANDLE h){ return close((int)(size_t)h) == 0; }
BOOL   GetFileSizeEx(HANDLE h, int64_t *sz){
    struct stat st;
    if(fstat((int)(size_t)h, &st) != 0) return 0;
    *sz = (int64_t)st.st_size; return 1;
}
void   ExitProcess(unsigned c){ exit((int)c); }
BOOL   QueryPerformanceCounter(int64_t *v){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    *v = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec; return 1;
}
BOOL   QueryPerformanceFrequency(int64_t *v){ *v = 1000000000LL; return 1; }
BOOL   SetConsoleOutputCP(unsigned c){ (void)c; return 1; }

int main(int argc, char **argv){
    size_t at = 0;
    for(int i = 0; i < argc && at < sizeof cmdline - 2; i++){
        size_t l = strlen(argv[i]);
        if(at + l + 2 >= sizeof cmdline) break;
        memcpy(cmdline + at, argv[i], l); at += l;
        cmdline[at++] = ' ';
    }
    cmdline[at] = 0;
    return main_();
}
