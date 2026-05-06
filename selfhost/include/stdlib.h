#ifndef TINYC_SELFHOST_STDLIB_H
#define TINYC_SELFHOST_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void exit(int status);
double strtod(const char *nptr, char **endptr);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

#endif
