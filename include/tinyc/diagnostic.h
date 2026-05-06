/*
 * diagnostic.h — Error/warning reporting with source locations
 *
 * Outputs GCC-style diagnostics:
 *   file.c:42:10: error: unterminated string literal
 */
#ifndef TINYC_DIAGNOSTIC_H
#define TINYC_DIAGNOSTIC_H

#include <stdarg.h>

/* C99 doesn't have _Noreturn; use compiler extension if available */
#if defined(__GNUC__) || defined(__clang__)
#define TINYC_NORETURN __attribute__((noreturn))
#else
#define TINYC_NORETURN
#endif

typedef enum {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_FATAL,
} DiagLevel;

/* Initialize the diagnostic system for a file.
 * `filename` and `source` must remain valid for the lifetime of diagnostics. */
void diag_init(const char *filename, const char *source);

/* Emit a diagnostic at the given source location. */
void diag_emit(DiagLevel level, int line, int col, const char *fmt, ...);

/* Emit a diagnostic (va_list version). */
void diag_emitv(DiagLevel level, int line, int col, const char *fmt, va_list ap);

/* Return the number of errors emitted so far. */
int diag_error_count(void);

/* Return the number of warnings emitted so far. */
int diag_warning_count(void);

/* Emit a fatal diagnostic and abort. */
TINYC_NORETURN void diag_fatal(int line, int col, const char *fmt, ...);

#endif /* TINYC_DIAGNOSTIC_H */
