/* Colouring the code - see colour.h. Plain C99, no Adda headers: both GUIs
 * build it on its own. */
#include "colour.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

typedef struct { ColourSpan *out; int n, max; } Spans;

static void add(Spans *s, const char *base, const char *p, size_t len, ColourKind k)
{
    if (s->n >= s->max || len == 0) return;
    s->out[s->n].start = (size_t)(p - base);
    s->out[s->n].len = len;
    s->out[s->n].kind = k;
    s->n++;
}

static bool is_word(const char *p, size_t n, const char *w)
{
    size_t i;
    if (strlen(w) != n) return false;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)w[i])) return false;
    return true;
}

static bool one_of(const char *p, size_t n, const char *const *words)
{
    for (; *words; words++)
        if (is_word(p, n, *words)) return true;
    return false;
}

/* a statement's first word */
static const char *const STATEMENTS[] = {
    "print", "if", "else", "while", "for", "define", "return", "call", "add",
    "remove", "end", "delay", "openApplication", "insert", "text", "height",
    "width", "function", "colour", "color", "title", NULL
};

/* words that start a value, straight after = or a { or a statement */
static const char *const VALUE_STARTS[] = {
    "ask", "list", "map", "item", "length", "has", "call", NULL
};

static const char *const CONSTANTS[] = { "true", "false", "nothing", NULL };

/* the words a test is built from, in an if or a while */
static const char *const TEST_WORDS[] = { "is", "not", "and", "or", NULL };

static bool is_number(const char *p, size_t n)
{
    size_t i = 0, digits = 0;
    if (n >= 3 && p[n - 2] == 'p' && p[n - 1] == 'x') n -= 2;      /* 15px */
    if (i < n && p[i] == '-') i++;
    for (; i < n; i++) {
        if (isdigit((unsigned char)p[i])) digits++;
        else if (p[i] != '.') return false;
    }
    return digits > 0;
}

/* An arrow like the tidy view's: any run of dashes - plain ones, = signs, or
 * the long dash (in UTF-8, or the single byte Windows uses) - then a >. The
 * branch before a line in a delay starts with a corner (or a | on Windows)
 * and uses the box-drawing dash. Returns how many bytes it takes, or 0. */
static size_t arrow_at(const char *p, const char *e)
{
    const char *q = p;
    if (e - q >= 3 && (unsigned char)q[0] == 0xE2 && (unsigned char)q[1] == 0x94 &&
        (unsigned char)q[2] == 0x94) q += 3;                           /* └ */
    else if (q < e && *q == '|') q++;
    for (;;) {
        if (q < e && (*q == '-' || *q == '=')) q++;
        else if (e - q >= 3 && (unsigned char)q[0] == 0xE2 &&
                 (((unsigned char)q[1] == 0x80 && (unsigned char)q[2] == 0x94) ||    /* — */
                  ((unsigned char)q[1] == 0x94 && (unsigned char)q[2] == 0x80))) q += 3;   /* ─ */
        else if (q < e && (unsigned char)*q == 0x97) q++;
        else break;
    }
    return (q > p && q < e && *q == '>' && !(*p == '|' && q == p + 1))
           ? (size_t)(q + 1 - p) : 0;
}

static const char *skip_blanks(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static void colour_line(Spans *s, const char *base, const char *p, const char *e)
{
    const char *q = p;
    const char *first = NULL;         /* the statement's first word */
    size_t firstLen = 0;
    int words = 0;
    bool test = false;                /* an if, else if or while: is/and/or count */
    bool valueNext = false;           /* the next word may start a value */
    bool afterSemi = false;           /* a shape's ; setting word comes next */
    bool inValue = false;             /* past an item/length/has/call: of, with count */
    bool printed = false;             /* a print line: its words are text unless alone */
    int braces = 0;

    while (q < e) {
        const char *w;
        size_t n;

        if (*q == ' ' || *q == '\t') { q++; continue; }

        /* a comment: # where a word could start, to the end of the line */
        if (*q == '#') { add(s, base, q, (size_t)(e - q), COL_COMMENT); return; }

        /* "quoted text", to the closing quote (\" does not close it) */
        if (*q == '"') {
            const char *r = q + 1;
            while (r < e && *r != '"') r += (*r == '\\' && r + 1 < e) ? 2 : 1;
            if (r < e) r++;
            add(s, base, q, (size_t)(r - q), COL_STRING);
            q = r;
            words++;
            valueNext = false;
            continue;
        }

        /* [name] */
        if (*q == '[') {
            const char *r = q + 1;
            if (r < e && (isalpha((unsigned char)*r) || *r == '_')) {
                while (r < e && (isalnum((unsigned char)*r) || *r == '_')) r++;
                if (r < e && *r == ']') {
                    add(s, base, q, (size_t)(r + 1 - q), COL_VARIABLE);
                    q = r + 1;
                    words++;
                    valueNext = false;
                    continue;
                }
            }
        }

        {
            size_t a = arrow_at(q, e);
            /* in the tidy view an arrow stands where a ; or an = did */
            if (a) {
                add(s, base, q, a, COL_ARROW);
                q += a;
                afterSemi = true;
                valueNext = true;
                continue;
            }
        }

        if (*q == '{' || *q == '}' || *q == '(' || *q == ')' || *q == ',' || *q == ';' || *q == '=') {
            if (*q == '{') braces++;
            if (*q == '}' && braces) braces--;
            valueNext = (*q == '=' || *q == '{' || *q == '(');
            afterSemi = (*q == ';');
            add(s, base, q, 1, COL_PUNCT);
            q++;
            continue;
        }

        /* a word: up to a space or one of the marks above */
        w = q;
        while (q < e && *q != ' ' && *q != '\t' && *q != '{' && *q != '}' &&
               *q != '(' && *q != ')' && *q != ',' && *q != ';' && *q != '=' &&
               *q != '"' && !(*q == '[' && q > w) && !arrow_at(q, e))
            q++;
        n = (size_t)(q - w);
        if (!n) { q++; continue; }

        if (words == 0) {
            first = w;
            firstLen = n;
            if (!one_of(w, n, STATEMENTS) && !is_number(w, n) &&
                (isalpha((unsigned char)*w) || *w == '_')) {   /* age ——>: the tidy [age] */
                const char *r = skip_blanks(q, e);
                if (arrow_at(r, e)) {
                    add(s, base, w, n, COL_VARIABLE);
                    words++;
                    continue;
                }
            }
            if (one_of(w, n, STATEMENTS)) {
                add(s, base, w, n, COL_KEYWORD);
                test = is_word(w, n, "if") || is_word(w, n, "while");
                printed = is_word(w, n, "print");
                valueNext = is_word(w, n, "return") || is_word(w, n, "print") ||
                            is_word(w, n, "if") || is_word(w, n, "while");
                words++;
                continue;
            }
        } else if (words == 1 && first && is_word(first, firstLen, "else") && is_word(w, n, "if")) {
            add(s, base, w, n, COL_KEYWORD);
            test = true;
            valueNext = true;
            words++;
            continue;
        }
        words++;

        /* after print, a word is a value only when it is all there is */
        if (printed && !braces && valueNext) {
            const char *r = q;
            while (r < e && (*r == ' ' || *r == '\t')) r++;
            if (r < e) valueNext = false;
        }

        if (is_number(w, n)) {
            add(s, base, w, n, COL_NUMBER);
        } else if (one_of(w, n, CONSTANTS) && (valueNext || test || braces)) {
            add(s, base, w, n, COL_CONSTANT);
        } else if (afterSemi && (is_word(w, n, "font") || is_word(w, n, "size") ||
                                 is_word(w, n, "location") || is_word(w, n, "name"))) {
            add(s, base, w, n, COL_KEYWORD);                /* a shape's settings */
        } else if (valueNext && one_of(w, n, VALUE_STARTS)) {
            add(s, base, w, n, COL_KEYWORD);
        } else if ((test || braces) && one_of(w, n, TEST_WORDS)) {
            add(s, base, w, n, COL_KEYWORD);
        } else if (first && is_word(first, firstLen, "for") &&
                   (is_word(w, n, "each") || is_word(w, n, "in"))) {
            add(s, base, w, n, COL_KEYWORD);
        } else if (first && (is_word(first, firstLen, "define") || is_word(first, firstLen, "call")) &&
                   is_word(w, n, "with")) {
            add(s, base, w, n, COL_KEYWORD);
        } else if (first && is_word(first, firstLen, "add") && is_word(w, n, "to")) {
            add(s, base, w, n, COL_KEYWORD);
        } else if (inValue && (is_word(w, n, "of") || is_word(w, n, "with"))) {
            add(s, base, w, n, COL_KEYWORD);                /* item 1 of [x], call f with 2 */
        }
        /* a value-starting word lets the next word start one too (item length of ...) */
        if (valueNext && one_of(w, n, VALUE_STARTS) && !is_word(w, n, "ask"))
            inValue = true;           /* not ask: its question is plain words */
        valueNext = (valueNext && one_of(w, n, VALUE_STARTS)) ||
                    (inValue && (is_word(w, n, "of") || is_word(w, n, "with")));
        afterSemi = false;
    }
}

int colour_spans(const char *text, ColourSpan *out, int max)
{
    Spans s;
    const char *p = text;

    s.out = out;
    s.n = 0;
    s.max = max;
    while (*p && s.n < max) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        colour_line(&s, text, p, (e > p && e[-1] == '\r') ? e - 1 : e);
        p = *e ? e + 1 : e;
    }
    return s.n;
}
