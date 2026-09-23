/* adda - run an Adda program.
 *
 * Usage:
 *   adda                     start an interactive session
 *   adda program.adda        run it
 *   adda --tokens file       show how the lexer split the source
 *   adda --stats file        run it, then report arena bytes used
 *   adda --check file        list syntax errors without running (for the GUI)
 *   adda --warnings file     list what may go wrong, without running (for the GUI)
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fileno, under -std=c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "adda.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define at_terminal(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define at_terminal(f) isatty(fileno(f))
#endif

/* Warnings are asked about before running, when someone is at the terminal
 * to answer. Run from the GUI, stdin is a pipe: the GUI has asked already. */
static bool go_ahead(Node *program)
{
    char answer[16];

    if (!at_terminal(stdin) || !at_terminal(stderr)) return true;
    if (!adda_warnings(program, NULL)) return true;
    fputs("Warning:\n", stderr);
    (void)adda_warnings(program, stderr);
    fputs("\nRun it anyway? (y/n) ", stderr);
    if (!fgets(answer, sizeof answer, stdin)) return false;
    return answer[0] == 'y' || answer[0] == 'Y';
}

/* --check: report every mistake that can be found without running, one per
 * line on stdout as `line column length message` (column 0-based, in bytes),
 * in line order.
 *
 * The parser stops at its first error, so the line it complained about is
 * dealt with and the whole thing parsed again, until it passes or stops making
 * progress. A misspelt word is corrected; a line that opens a block is left
 * opening one (`if true`), so its `end` is not blamed as well; anything else
 * is blanked, keeping its newline so every other line keeps its number. Once
 * it parses, adda_lint looks for names that are used but never made. */

#define MAX_FOUND 60

typedef struct { uint32_t line; int col, len; char msg[256]; } Found;

typedef struct {
    Found       f[MAX_FOUND];
    int         n;
    const char *orig;            /* the source as written, for finding marks */
} Findings;

static const char *nth_line(const char *src, uint32_t line, const char **end)
{
    const char *start = src, *e;
    uint32_t n;
    for (n = 1; *start && n < line; start++)
        if (*start == '\n') n++;
    for (e = start; *e && *e != '\n'; e++) {}
    while (e > start && e[-1] == '\r') e--;
    *end = e;
    return start;
}

static void found(Findings *fs, uint32_t line, int col, int len, const char *msg)
{
    int i;
    if (fs->n == MAX_FOUND) return;
    for (i = 0; i < fs->n; i++)             /* the same thing twice on a line */
        if (fs->f[i].line == line && fs->f[i].col == col && strcmp(fs->f[i].msg, msg) == 0)
            return;
    fs->f[fs->n].line = line;
    fs->f[fs->n].col = col;
    fs->f[fs->n].len = len < 1 ? 1 : len;
    snprintf(fs->f[fs->n].msg, sizeof fs->f[0].msg, "%s", msg);
    fs->n++;
}

/* adda_lint's report: mark the text it names, where it is on the line */
static void lint_found(uint32_t line, const char *mark, const char *msg, void *ctx)
{
    Findings *fs = ctx;
    const char *end, *start = nth_line(fs->orig, line, &end), *at = NULL, *p;
    size_t n = strlen(mark);

    for (p = start; p + 5 + n <= end && !at; p++)        /* a function: after call */
        if (memcmp(p, "call ", 5) == 0 && memcmp(p + 5, mark, n) == 0) at = p + 5;
    for (p = start; p + n <= end && !at; p++)
        if (memcmp(p, mark, n) == 0) at = p;
    if (at) {
        found(fs, line, (int)(at - start), (int)n, msg);
    } else {
        const char *b = start;
        while (b < end && (*b == ' ' || *b == '\t')) b++;
        found(fs, line, (int)(b - start), (int)(end - b), msg);
    }
}

static int by_place(const void *x, const void *y)
{
    const Found *a = x, *b = y;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    return (a->col > b->col) - (a->col < b->col);
}

/* What a line that opens a block becomes when it cannot be read: the same
 * kind of block, so its end still has something to close. NULL: blank it. */
static const char *stand_in(const char *start, const char *end)
{
    static const char *const kinds[][2] = {
        { "else if", "else if true" }, { "if", "if true" }, { "while", "while false" },
        { "for", "for each [_] in list" }, { "define", "define _" }, { "delay", "delay 0" },
    };
    const char *p = start;
    size_t i;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        size_t n = strlen(kinds[i][0]);
        if ((size_t)(end - p) >= n && memcmp(p, kinds[i][0], n) == 0 &&
            (p + n == end || p[n] == ' ' || p[n] == '\t')) {
            if (strcmp(kinds[i][0], "delay") == 0) {          /* not delay end */
                const char *q = p + n;
                while (q < end && (*q == ' ' || *q == '\t')) q++;
                if ((size_t)(end - q) >= 3 && memcmp(q, "end", 3) == 0) return NULL;
            }
            return kinds[i][1];
        }
    }
    return NULL;
}

/* src with the bytes [from, to) replaced by `with` (a new buffer) */
static char *splice(const char *src, const char *from, const char *to, const char *with)
{
    size_t a = (size_t)(from - src), w = strlen(with), rest = strlen(to);
    char *out = adda_alloc(a + w + rest + 1);
    memcpy(out, src, a);
    memcpy(out + a, with, w);
    memcpy(out + a + w, to, rest + 1);
    return out;
}

static void check(char *src)
{
    static Findings fs;
    char *extra = adda_alloc(strlen(src) * 2 + 2);  /* the lines given up on */
    size_t extraLen = 0;
    Node *volatile program = NULL;
    volatile uint32_t lastLine = 0;
    volatile int errors = 0, fixedLast = 0;
    int i;

    fs.n = 0;
    fs.orig = src;
    extra[0] = '\0';
    adda_quiet = true;
    while (errors < 25) {
        const char *start, *end, *at, *standIn;
        int col, len;

        adda_err_fix = NULL;
        if (setjmp(adda_error_jmp) == 0) {
            program = parse(lex(src));
            break;                                  /* it reads */
        }
        if (adda_err_line == 0) break;
        start = nth_line(src, adda_err_line, &end);

        if (adda_err_line == lastLine) {
            /* a line that went wrong again after a fix: give up on it */
            if (!fixedLast) break;
            fixedLast = 0;
            memcpy(extra + extraLen, start, (size_t)(end - start));
            extraLen += (size_t)(end - start);
            extra[extraLen++] = '\n';
            extra[extraLen] = '\0';
            src = splice(src, start, end, "");
            continue;
        }
        lastLine = adda_err_line;

        at = adda_err_at;
        if (at && at >= start && at <= end) {
            /* the word or number the error is about, else just that character */
            const char *e;
            if (at == end && at > start) at--;      /* past the end: mark the last character */
            e = at;
            while (e < end && (isalnum((unsigned char)*e) || *e == '_')) e++;
            if (e == at && at < end) e = at + 1;
            col = (int)(at - start);
            len = (int)(e - at);
        } else {                                    /* the whole line, less indent */
            const char *b = start;
            while (b < end && (*b == ' ' || *b == '\t')) b++;
            col = (int)(b - start);
            len = (int)(end - b);
            at = NULL;
        }
        found(&fs, adda_err_line, col, len, adda_err_msg);
        errors++;

        if (adda_err_fix && at) {                   /* prnt -> print, and read on */
            src = splice(src, at, at + adda_err_fix_len, adda_err_fix);
            fixedLast = 1;
            continue;
        }
        /* the rest of the line is lost to the parser: keep its names */
        memcpy(extra + extraLen, start, (size_t)(end - start));
        extraLen += (size_t)(end - start);
        extra[extraLen++] = '\n';
        extra[extraLen] = '\0';
        if (!strstr(adda_err_msg, "no open block") && !strstr(adda_err_msg, "never closed") &&
                   (standIn = stand_in(start, end)) != NULL) {
            src = splice(src, start, end, standIn);
            fixedLast = 1;                          /* if the stand-in fails too, blank it */
        } else {
            src = splice(src, start, end, "");
            fixedLast = 0;
        }
    }
    if (program && setjmp(adda_error_jmp) == 0)
        adda_lint(program, extra, lint_found, &fs);
    adda_quiet = false;

    qsort(fs.f, (size_t)fs.n, sizeof fs.f[0], by_place);
    for (i = 0; i < fs.n; i++)
        printf("%u %d %d %s\n", (unsigned)fs.f[i].line, fs.f[i].col, fs.f[i].len, fs.f[i].msg);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;

    if (!f) {
        fprintf(stderr, "adda: cannot open '%s'\n", path);
        exit(66);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = adda_alloc((size_t)size + 1);
    got = fread(buf, 1, (size_t)size, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

static void dump_tokens(const char *src)
{
    TokenList tl = lex(src);
    uint32_t i;

    for (i = 0; i < tl.count; i++) {
        Token *t = &tl.tokens[i];
        if (t->kind == TK_NEWLINE) { printf("%4u  newline\n", t->line); continue; }
        if (t->kind == TK_EOF)     { printf("%4u  end\n", t->line); continue; }
        printf("%4u  %-8s %.*s\n", t->line, token_kind_name(t->kind),
               (int)t->len, t->start);
    }
}

static void usage(void)
{
    fputs("usage: adda [--tokens|--ast|--stats|--check|--warnings] program.adda\n"
          "       adda                      start an interactive session\n", stderr);
    exit(64);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *mode = NULL;
    char *src;
    int i;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   /* so text is not mangled in the terminal */
#endif

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) == 0) mode = argv[i];
        else if (!path) path = argv[i];
        else usage();
    }
    if (!path && mode) usage();

    arena_init();

    /* No file to run means an interactive session. The REPL handles its own
     * errors, so it must not share main's one-shot handler. */
    if (!path) {
        repl();
        fflush(stdout);
        if (adda_window_is_open()) adda_window_run();
        arena_free_all();
        return 0;
    }

    if (setjmp(adda_error_jmp) != 0) {
        arena_free_all();
        return 65;                 /* a reported Adda error */
    }

    src = read_file(path);
    adda_source(path, src);

    if (mode && strcmp(mode, "--check") == 0) {
        check(src);
    } else if (mode && strcmp(mode, "--warnings") == 0) {
        (void)adda_warnings(parse(lex(src)), stdout);
    } else if (mode && strcmp(mode, "--tokens") == 0) {
        dump_tokens(src);
    } else if (mode && strcmp(mode, "--ast") == 0) {
        fputs("adda: --ast is not available yet\n", stderr);
    } else {
        Node *program = parse(lex(src));
        if (!go_ahead(program)) {
            arena_free_all();
            return 0;
        }
        interpret(program);
        fflush(stdout);
        if (adda_window_is_open()) adda_window_run();   /* stays up until closed */
        if (mode && strcmp(mode, "--stats") == 0)
            fprintf(stderr, "arena: %lu bytes\n", (unsigned long)arena_bytes_used());
    }

    fflush(stdout);
    arena_free_all();
    return 0;
}
