/*
 * diagnostic.c — Error/warning reporting implementation
 */
#include "tinyc/diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_filename = "<unknown>";
static const char *g_source = NULL;
static int g_errors = 0;
static int g_warnings = 0;

void diag_init(const char *filename, const char *source) {
    g_filename = filename ? filename : "<unknown>";
    g_source = source;
    g_errors = 0;
    g_warnings = 0;
}

static const char *level_str(DiagLevel level) {
    switch (level) {
    case DIAG_NOTE:    return "note";
    case DIAG_WARNING: return "warning";
    case DIAG_ERROR:   return "error";
    case DIAG_FATAL:   return "fatal error";
    }
    return "unknown";
}

static const char *level_color(DiagLevel level) {
    switch (level) {
    case DIAG_NOTE:    return "\033[1;36m"; /* bold cyan */
    case DIAG_WARNING: return "\033[1;35m"; /* bold magenta */
    case DIAG_ERROR:   return "\033[1;31m"; /* bold red */
    case DIAG_FATAL:   return "\033[1;31m"; /* bold red */
    }
    return "\033[0m";
}

/* Find the start of line `lineno` (1-indexed) in the source buffer.
 * Returns NULL if not found or if source is NULL. */
static const char *find_line_start(int lineno) {
    if (!g_source || lineno < 1) {
        return NULL;
    }
    const char *p = g_source;
    int cur = 1;
    while (cur < lineno && *p) {
        if (*p == '\n') {
            cur++;
        }
        p++;
    }
    return (cur == lineno) ? p : NULL;
}

void diag_emitv(DiagLevel level, int line, int col, const char *fmt, va_list ap) {
#ifdef TINYC_SELFHOST
    printf("%s:%d:%d: %s: %s\n", g_filename, line, col, level_str(level), fmt);
    if (level == DIAG_ERROR || level == DIAG_FATAL) {
        g_errors++;
    } else if (level == DIAG_WARNING) {
        g_warnings++;
    }
    return;
#endif
    /* Location */
    fprintf(stderr, "\033[1m%s:%d:%d: ", g_filename, line, col);
    /* Level */
    fprintf(stderr, "%s%s: \033[0m", level_color(level), level_str(level));
    /* Message */
    fprintf(stderr, "\033[1m");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\033[0m\n");

    /* Show the source line if available */
    const char *line_start = find_line_start(line);
    if (line_start) {
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') {
            line_end++;
        }
        int line_len = (int)(line_end - line_start);
        fprintf(stderr, " %.*s\n", line_len, line_start);
        /* Caret under the column */
        if (col > 0 && col <= line_len) {
            fprintf(stderr, " ");
            for (int i = 1; i < col; i++) {
                fputc(line_start[i - 1] == '\t' ? '\t' : ' ', stderr);
            }
            fprintf(stderr, "\033[1;32m^\033[0m\n");
        }
    }

    if (level == DIAG_ERROR || level == DIAG_FATAL) {
        g_errors++;
    } else if (level == DIAG_WARNING) {
        g_warnings++;
    }
}

void diag_emit(DiagLevel level, int line, int col, const char *fmt, ...) {
#ifdef TINYC_SELFHOST
    diag_emitv(level, line, col, fmt, (va_list)0);
#else
    va_list ap;
    va_start(ap, fmt);
    diag_emitv(level, line, col, fmt, ap);
    va_end(ap);
#endif
}

int diag_error_count(void) {
    return g_errors;
}

int diag_warning_count(void) {
    return g_warnings;
}

TINYC_NORETURN void diag_fatal(int line, int col, const char *fmt, ...) {
#ifdef TINYC_SELFHOST
    diag_emitv(DIAG_FATAL, line, col, fmt, (va_list)0);
#else
    va_list ap;
    va_start(ap, fmt);
    diag_emitv(DIAG_FATAL, line, col, fmt, ap);
    va_end(ap);
#endif
    exit(1);
}
