/* The tidy view must never lose your code.
 *
 * It is only a way of SHOWING the code, so raw -> tidy -> raw has to give back
 * exactly what was typed. This went wrong once: a variable called [name] tidied
 * to `name ——> Rocco` and would not turn back, because `name ——>` is also how a
 * shape's name setting is shown, so the line was silently rewritten. [delay]
 * and [print] were worse - they came back as a delay block and a print.
 *
 * Build:  gcc -std=c99 -I src -o adda-tidytest tests/tidy_roundtrip.c src/tidy.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tidy.h"

/* the Windows code page 1252 arrow, the one most people will be using */
static const char *ARROW  = "\x97\x97>";
static const char *BRANCH = "|\x97>";

static int failures;

/* `text` with every edit applied - last to first, as tidy.h asks */
static char *apply_edits(const char *text, long caretLine, bool on)
{
    TidyEdit ed[256];
    int n = tidy_edits(text, ARROW, BRANCH, caretLine, on, ed, 256), k;
    size_t len = strlen(text);
    char *out = malloc(len * 4 + 4096);

    if (!out) { fputs("out of memory\n", stderr); exit(2); }
    memcpy(out, text, len + 1);

    for (k = n - 1; k >= 0; k--) {
        size_t with = strlen(ed[k].with), cur = strlen(out);
        memmove(out + ed[k].start + with, out + ed[k].start + ed[k].len,
                cur - ed[k].start - ed[k].len + 1);
        memcpy(out + ed[k].start, ed[k].with, with);
    }
    tidy_free_edits(ed, n);
    return out;
}

static void show(const char *label, const char *s)
{
    printf("    %-6s |", label);
    for (; *s; s++) {
        if (*s == '\n')                   printf("\\n");
        else if (*s == '\r')              printf("\\r");
        else if ((unsigned char)*s == 0x97) printf("[em]");
        else                              putchar(*s);
    }
    printf("|\n");
}

static void roundtrip(const char *name, const char *raw)
{
    char *tidy = apply_edits(raw, -1, true);
    char *back = tidy_raw(tidy, ARROW, BRANCH);

    if (strcmp(raw, back) != 0) {
        failures++;
        printf("FAIL %s\n", name);
        show("raw", raw);
        show("tidy", tidy);
        show("back", back);
    }
    free(tidy);
    free(back);
}

/* Turning the tidy view off has to give the code back whatever wrote the
 * arrows - this build, the other GUI, or a different code page. If it does
 * not, the line stays tidied and is saved and run exactly as shown, which is
 * how `print ——> hello` ended up being printed with its arrow. */
static void untidies(const char *name, const char *tidied, const char *want)
{
    char *back = tidy_raw(tidied, ARROW, BRANCH);

    if (strcmp(back, want) != 0) {
        failures++;
        printf("FAIL %s\n", name);
        show("tidy", tidied);
        show("back", back);
        show("want", want);
    }
    free(back);
}

int main(void)
{
    /* everyday lines */
    roundtrip("print",         "print hello\n");
    roundtrip("assign text",   "[greeting] = Rocco\n");
    roundtrip("assign maths",  "[next] = [age] + 1\n");
    roundtrip("assign ask",    "[age] = ask How old are you?\n");
    roundtrip("comment",       "# just a note\nprint hi\n");
    roundtrip("blank lines",   "print a\n\nprint b\n");
    roundtrip("no final nl",   "print hello");
    roundtrip("empty",         "");
    roundtrip("crlf",          "print hello\r\ndelay - 500\r\n    print x\r\nend\r\n");

    /* names that also mean something to the tidy view */
    roundtrip("var name",      "[name] = Rocco\n");
    roundtrip("var title",     "[title] = Hi\n");
    roundtrip("var width",     "[width] = 5\n");
    roundtrip("var height",    "[height] = 5\n");
    roundtrip("var text",      "[text] = hi\n");
    roundtrip("var colour",    "[colour] = red\n");
    roundtrip("var color",     "[color] = red\n");
    roundtrip("var function",  "[function] = x\n");
    roundtrip("var insert",    "[insert] = x\n");
    roundtrip("var delay",     "[delay] = 5\n");
    roundtrip("var print",     "[print] = x\n");
    roundtrip("var openApp",   "[openApplication] = x\n");

    /* blocks */
    roundtrip("delay",         "delay - 500\n    print hello\nend\n");
    roundtrip("delay nested",  "delay - 500\n    delay - 100\n        print a\n    end\nend\n");
    roundtrip("if",            "if [age] > 18\n    print Adult\nend\n");
    roundtrip("while",         "while [c] < 5\n    print [c]\nend\n");
    roundtrip("define",        "define greet with [who]\n    print Hi [who]\nend\n");
    roundtrip("app",           "openApplication My Game\nprint Hello\n");

    /* text that looks like the tidy view's own marks */
    roundtrip("ascii arrow",   "print a ==> b\n");
    roundtrip("em arrow",      "print a \x97\x97> b\n");

    /* arrows this build would not itself have written */
    untidies("cp1252 arrow", "print \x97\x97> hello\n", "print hello\n");
    untidies("utf-8 arrow",  "print \xe2\x80\x94\xe2\x80\x94> hello\n", "print hello\n");
    untidies("ascii arrow",  "print ==> hello\n", "print hello\n");
    untidies("utf-8 assign", "greeting \xe2\x80\x94\xe2\x80\x94> Rocco\n",
                             "[greeting] = Rocco\n");
    untidies("utf-8 branch", "delay - 500\n|\xe2\x80\x94>print hi\nend\n",
                             "delay - 500\n    print hi\nend\n");

    if (failures) {
        printf("\ntidy round-trip: %d failed\n", failures);
        return 1;
    }
    printf("tidy round-trip: all clean\n");
    return 0;
}
