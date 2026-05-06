#ifndef TINYC_SELFHOST_CTYPE_H
#define TINYC_SELFHOST_CTYPE_H

#define isalpha(c) ((((c) >= 'a') && ((c) <= 'z')) || (((c) >= 'A') && ((c) <= 'Z')))
#define isdigit(c) (((c) >= '0') && ((c) <= '9'))
#define isalnum(c) (isalpha(c) || isdigit(c))

#endif
