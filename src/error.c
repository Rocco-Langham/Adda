/* Error reporting. Every message is `file:line: what went wrong`, followed by
 * the offending source line and a caret, because a language aimed at beginners
 * lives or dies on its diagnostics. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "adda.h"

jmp_buf adda_error_jmp;

static const char *src_name = "<input>";
static const char *src_text = "";

#define MAX_HINTS 32
static const char *hints_shown[MAX_HINTS];
static int         hints_count;

void adda_source(const char *filename, const char *text)
{
    src_name = filename;
    src_text = text;
}

/* Start of the 1-based line `line` within the source, or NULL. */
static const char *line_start(uint32_t line)
{
    const char *p = src_text;
    uint32_t n = 1;
    if (line == 0) return NULL;
    while (*p && n < line) {
        if (*p == '\n') n++;
        p++;
    }
    return (n == line) ? p : NULL;
}

static void show_line(uint32_t line, const char *at)
{
    const char *start = line_start(line);
    const char *p;
    size_t len;

    if (!start) return;
    p = start;
    while (*p && *p != '\n') p++;
    len = (size_t)(p - start);
    while (len > 0 && start[len - 1] == '\r') len--;

    fprintf(stderr, "  %*u | %.*s\n", 4, line, (int)len, start);

    /* Caret: under `at` when we know it, otherwise under the first
     * non-blank character of the line. */
    {
        size_t col = 0;
        size_t i;
        if (at && at >= start && at <= start + len) {
            col = (size_t)(at - start);
        } else {
            while (col < len && (start[col] == ' ' || start[col] == '\t')) col++;
        }
        fprintf(stderr, "  %*s | ", 4, "");
        for (i = 0; i < col; i++) fputc(start[i] == '\t' ? '\t' : ' ', stderr);
        fputs("^\n", stderr);
    }
}

bool        adda_quiet;
uint32_t    adda_err_line;
const char *adda_err_at;
char        adda_err_msg[256];

static void report(const char *at, uint32_t line, const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    vsnprintf(adda_err_msg, sizeof adda_err_msg, fmt, copy);
    va_end(copy);
    adda_err_line = line;
    adda_err_at = at;
    if (adda_quiet) return;

    fprintf(stderr, "%s:%u: ", src_name, line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    show_line(line, at);
}

void adda_error(uint32_t line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(NULL, line, fmt, ap);
    va_end(ap);
    longjmp(adda_error_jmp, 1);
}

void adda_error_at(const char *at, uint32_t line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(at, line, fmt, ap);
    va_end(ap);
    longjmp(adda_error_jmp, 1);
}

/* A nudge, not a failure: shown once per `key` so a loop cannot spam it. */
void adda_hint(const char *key, const char *fmt, ...)
{
    va_list ap;
    int i;
    for (i = 0; i < hints_count; i++)
        if (strcmp(hints_shown[i], key) == 0) return;
    if (hints_count < MAX_HINTS) hints_shown[hints_count++] = key;

    fputs("adda: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
