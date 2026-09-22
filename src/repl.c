/* The interactive prompt.
 *
 * Three things make this more than a read-eval-print loop:
 *
 *   - Everything typed is kept in one growing buffer, so line numbers keep
 *     counting up and an error inside a function defined ten entries ago can
 *     still show the line it came from.
 *   - A block keeps reading until its `end` arrives, so you can type an `if`
 *     over several lines.
 *   - An error reports itself and the loop carries on, rather than ending the
 *     session the way a script would.
 */
#include <stdio.h>
#include <string.h>
#include "adda.h"

#ifdef _WIN32
#include <io.h>
#define STDIN_IS_TTY() (_isatty(_fileno(stdin)) != 0)
#else
#include <unistd.h>
#define STDIN_IS_TTY() (isatty(0) != 0)
#endif

/* ------------------------------------------------- everything typed so far */

static char    *src_bytes;
static size_t   src_len, src_cap;
static uint32_t src_lines;          /* newlines seen, so the next line is +1 */

static void session_append(const char *text, size_t n)
{
    size_t i;

    if (src_len + n + 1 > src_cap) {
        size_t cap = src_cap < 4096 ? 4096 : src_cap;
        char *bytes;

        while (cap < src_len + n + 1) cap *= 2;
        bytes = adda_alloc(cap);
        if (src_len) memcpy(bytes, src_bytes, src_len);
        src_bytes = bytes;
        src_cap = cap;
    }

    memcpy(src_bytes + src_len, text, n);
    src_len += n;
    src_bytes[src_len] = 0;

    for (i = 0; i < n; i++)
        if (text[i] == '\n') src_lines++;

    adda_source("typed", src_bytes);
}

/* Drop the entry just read - used for :commands, which are not Adda code. */
static void session_rewind(size_t to_len, uint32_t to_lines)
{
    src_len = to_len;
    src_lines = to_lines;
    if (src_bytes) src_bytes[src_len] = 0;
    adda_source("typed", src_bytes);
}

/* Reads one whole line, however long, into the session. False at end of input. */
static bool read_line(void)
{
    char buf[1024];
    bool got = false;

    for (;;) {
        if (!fgets(buf, sizeof buf, stdin)) break;
        got = true;
        session_append(buf, strlen(buf));
        if (strchr(buf, '\n')) return true;
    }
    if (got) session_append("\n", 1);     /* last line had no newline */
    return got;
}

/* ------------------------------------------------------------------- input */

/* True while a block is still open, so the prompt should keep reading. An
 * opener only counts as the first word of a line, which is what keeps
 * `else if` from opening a second block. */
static bool block_still_open(TokenList tl)
{
    int depth = 0;
    bool line_start = true;
    uint32_t i;

    for (i = 0; i < tl.count; i++) {
        Token *t = &tl.tokens[i];

        if (t->kind == TK_NEWLINE) { line_start = true; continue; }
        if (!line_start) continue;
        line_start = false;

        if (token_is_word(t, "if")  || token_is_word(t, "while") ||
            token_is_word(t, "for") || token_is_word(t, "define")) depth++;
        else if (token_is_word(t, "delay") &&           /* not delay end */
                 !(i + 1 < tl.count && token_is_word(&tl.tokens[i + 1], "end"))) depth++;
        else if (token_is_word(t, "openApplication") && i + 2 < tl.count &&
                 token_is_word(&tl.tokens[i + 1], "details") &&
                 tl.tokens[i + 2].kind == TK_NEWLINE) depth++;
        else if (token_is_word(t, "end")) depth--;
    }
    return depth > 0;
}

static void show_help(void)
{
    puts("Adda");
    puts("");
    puts("  Type any Adda code. A line on its own shows you its value:");
    puts("");
    puts("      adda> 2 + 2");
    puts("      4");
    puts("");
    puts("  Blocks keep reading until you type end:");
    puts("");
    puts("      adda> if 2 > 1");
    puts("        ...     print yes");
    puts("        ... end");
    puts("      yes");
    puts("");
    puts("  :vars   show everything you have defined");
    puts("  :help   this message");
    puts("  :quit   leave (Ctrl-D does the same)");
}

/* Returns true if the line was a :command and has been dealt with. */
static bool meta_command(const char *line, bool *quit)
{
    const char *p = line;
    size_t len;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return false;

    len = strlen(p);
    while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' ||
                       p[len - 1] == ' '  || p[len - 1] == '\t')) len--;

    if (len == 5 && memcmp(p, ":quit", 5) == 0) { *quit = true; return true; }
    if (len == 5 && memcmp(p, ":exit", 5) == 0) { *quit = true; return true; }
    if (len == 5 && memcmp(p, ":help", 5) == 0) { show_help(); return true; }
    if (len == 5 && memcmp(p, ":vars", 5) == 0) { interp_show_variables(); return true; }

    printf("I do not know the command %.*s - try :help\n", (int)len, p);
    return true;
}

/* --------------------------------------------------------------------- loop */

/* A bare expression at the end of an entry becomes a print, so typing `count`
 * shows you the value. */
static void echo_last_value(Node *program)
{
    Node *last;

    if (program->nkids == 0) return;
    last = program->kids[program->nkids - 1];
    if (last->kind == N_EXPRSTMT) {
        last->kind = N_PRINT;
        last->flag = true;          /* stay quiet when the value is nothing */
    }
}

void repl(void)
{
    bool tty = STDIN_IS_TTY();

    interp_init();

    if (tty) {
        puts("Adda - type :help for help, :quit to leave.");
    }

    for (;;) {
        size_t   entry_start = src_len;
        uint32_t entry_line  = src_lines + 1;
        uint32_t entry_mark  = src_lines;
        TokenList tokens;
        Node *program;
        bool quit = false;

        /* Errors land here: the message has already been printed, so just go
         * round again with the next entry. */
        if (setjmp(adda_error_jmp) != 0) {
            fflush(stdout);
            continue;
        }

        if (tty) { fputs("adda> ", stdout); fflush(stdout); }
        if (!read_line()) break;

        if (meta_command(src_bytes + entry_start, &quit)) {
            session_rewind(entry_start, entry_mark);
            if (quit) break;
            continue;
        }

        for (;;) {
            tokens = lex_range(src_bytes, src_bytes + entry_start,
                               src_bytes + src_len, entry_line);
            if (!block_still_open(tokens)) break;

            if (tty) { fputs("  ... ", stdout); fflush(stdout); }
            if (!read_line()) {                          /* input ran out */
                fflush(stdout);
                fputs("adda: the input ended with a block still open\n", stderr);
                quit = true;
                break;
            }
        }
        if (quit) break;

        program = parse_mode(tokens, true);
        echo_last_value(program);
        interp_run(program);
        fflush(stdout);
    }

    if (tty) puts("");
}
