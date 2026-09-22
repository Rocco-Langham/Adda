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

/* --check: report every syntax error without running anything, one per line
 * on stdout as `line column length message` (column 0-based, in bytes).
 * The parser stops at its first error, so the line it complained about is
 * blanked out - keeping its newline, so every other line keeps its number -
 * and the whole thing is parsed again, until it passes or stops making
 * progress. */
static void check(char *src)
{
    volatile int found = 0;
    volatile uint32_t lastLine = 0;

    adda_quiet = true;
    while (found < 20) {
        const char *start, *end, *at;
        uint32_t n;
        int col, len;

        if (setjmp(adda_error_jmp) == 0) {
            (void)parse(lex(src));
            break;                                  /* clean */
        }
        if (adda_err_line == 0 || adda_err_line == lastLine) break;
        lastLine = adda_err_line;

        for (start = src, n = 1; *start && n < adda_err_line; start++)
            if (*start == '\n') n++;
        for (end = start; *end && *end != '\n'; end++) {}
        while (end > start && end[-1] == '\r') end--;

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
        }
        if (len < 1) len = 1;                       /* e.g. something missing at the end */

        printf("%u %d %d %s\n", (unsigned)adda_err_line, col, len, adda_err_msg);
        found++;

        for (at = start; at < end; at++) *(char *)at = ' ';
    }
    adda_quiet = false;
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
