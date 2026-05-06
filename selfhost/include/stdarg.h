#ifndef TINYC_SELFHOST_STDARG_H
#define TINYC_SELFHOST_STDARG_H

typedef void *va_list;
#define va_start(ap, last) ((void)0)
#define va_end(ap) ((void)0)
#define va_arg(ap, type) (*(type *)0)

#endif
