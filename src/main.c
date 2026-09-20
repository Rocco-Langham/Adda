/* adda - run an Adda program.
 *
 *   adda program.adda        run it
 *   adda --tokens file       show how the lexer split the source
 *   adda --stats file        run it, then report arena bytes used
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adda.h"

#ifdef _WIN32
#include <windows.h>
#endif

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
    fputs("usage: adda [--tokens|--ast|--stats] program.adda\n", stderr);
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
    if (!path) usage();

    arena_init();

    if (setjmp(adda_error_jmp) != 0) {
        arena_free_all();
        return 65;                 /* a reported Adda error */
    }

    src = read_file(path);
    adda_source(path, src);

    if (mode && strcmp(mode, "--tokens") == 0) {
        dump_tokens(src);
    } else if (mode && strcmp(mode, "--ast") == 0) {
        fputs("adda: --ast is not available yet\n", stderr);
    } else {
        Node *program = parse(lex(src));
        interpret(program);
        if (mode && strcmp(mode, "--stats") == 0)
            fprintf(stderr, "arena: %lu bytes\n", (unsigned long)arena_bytes_used());
    }

    fflush(stdout);
    arena_free_all();
    return 0;
}
