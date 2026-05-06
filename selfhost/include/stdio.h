#ifndef TINYC_SELFHOST_STDIO_H
#define TINYC_SELFHOST_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __tinyc_FILE FILE;

#ifdef __APPLE__
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin __stdinp
#define stdout __stdoutp
#define stderr __stderrp
#else
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#endif

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int snprintf(char *s, size_t n, const char *fmt, ...);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);

#endif
