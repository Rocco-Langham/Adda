/* The tidy view of named shape blocks - see tidy.h. Plain C99, no Adda
 * headers: both GUIs build it on its own. */
#include "tidy.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────── a growing string ── */

typedef struct { char *b; size_t len, cap; } Buf;

static void put(Buf *o, const char *s, size_t n)
{
    if (o->len + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 64;
        char *grown;
        while (o->len + n + 1 > cap) cap *= 2;
        grown = realloc(o->b, cap);
        if (!grown) return;
        o->b = grown;
        o->cap = cap;
    }
    memcpy(o->b + o->len, s, n);
    o->len += n;
    o->b[o->len] = '\0';
}

static void puts_(Buf *o, const char *s) { put(o, s, strlen(s)); }

static char *done(Buf *o)
{
    if (!o->b) {                 /* nothing was put: still a valid, empty string */
        o->b = malloc(1);
        if (o->b) o->b[0] = '\0';
    }
    return o->b;
}

/* ──────────────────────────────────────────────────── scanning ── */

static const char *skip_sp(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static const char *trim_back(const char *s, const char *e)
{
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
    return e;
}

/* the word `w` at p, in any capitals, followed by something that is not a letter */
static bool word_at(const char *p, const char *e, const char *w)
{
    size_t n = strlen(w), i;
    if ((size_t)(e - p) < n) return false;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)w[i])) return false;
    return p + n == e || !isalpha((unsigned char)p[n]);
}

static bool starts(const char *p, const char *e, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(e - p) >= n && memcmp(p, s, n) == 0;
}

/* a name: a letter or _, then letters, digits or _ */
static const char *name_end(const char *p, const char *e)
{
    if (p >= e || !(isalpha((unsigned char)*p) || *p == '_')) return p;
    while (p < e && (isalnum((unsigned char)*p) || *p == '_')) p++;
    return p;
}

/* where a distance like 15px or 2.5px ends, or p if there is none */
static const char *px_end(const char *p, const char *e)
{
    const char *q = p;
    while (q < e && (isdigit((unsigned char)*q) || *q == '.')) q++;
    if (q == p || e - q < 2 || q[0] != 'p' || q[1] != 'x') return p;
    q += 2;
    return (q == e || *q == ' ' || *q == '\t') ? q : p;
}

static bool contains(const char *s, const char *e, const char *what)
{
    size_t n = strlen(what);
    for (; s + n <= e; s++)
        if (memcmp(s, what, n) == 0) return true;
    return false;
}

/* ────────────────────────────────────────────── the first line ── */

/* insert <shape>; name = [x]  - fills in where the shape words and the name
 * are. `tidy` asks for the tidy form (name —> x) instead of the raw one. */
static bool start_line(const char *s, const char *e, const char *arrow, bool tidy,
                       const char **shape, const char **shapeEnd,
                       const char **nm, const char **nmEnd)
{
    const char *p = skip_sp(s, e), *semi;

    if (!word_at(p, e, "insert")) return false;
    p = skip_sp(p + 6, e);
    for (semi = p; semi < e && *semi != ';'; semi++) {}
    if (semi == e || trim_back(p, semi) == p) return false;
    *shape = p;
    *shapeEnd = trim_back(p, semi);

    p = skip_sp(semi + 1, e);
    if (!word_at(p, e, "name")) return false;
    p = skip_sp(p + 4, e);
    if (tidy) {
        if (!starts(p, e, arrow)) return false;
        p = skip_sp(p + strlen(arrow), e);
        *nm = p;
        *nmEnd = name_end(p, e);
        return *nmEnd > p && skip_sp(*nmEnd, e) == e;
    }
    if (p >= e || *p != '=') return false;
    p = skip_sp(p + 1, e);
    if (p >= e || *p != '[') return false;
    *nm = p + 1;
    *nmEnd = name_end(p + 1, e);
    if (*nmEnd == *nm || *nmEnd >= e || **nmEnd != ']') return false;
    return skip_sp(*nmEnd + 1, e) == e;
}

static bool end_line(const char *s, const char *e)
{
    const char *p = skip_sp(s, e);
    return word_at(p, e, "end") && skip_sp(p + 3, e) == e;
}

/* ──────────────────────────────────────── one line, either way ── */

/* A line inside a block, tidied (to_tidy) or turned back to raw. NULL when
 * it stays as it is - an end, a comment, a blank, or anything unusual. */
static char *convert(const char *s, const char *e, const char *arrow, bool to_tidy)
{
    const char *p = skip_sp(s, e), *q;
    Buf o = { NULL, 0, 0 };

    e = trim_back(s, e);
    if (p >= e || *p == '#') return NULL;
    put(&o, s, (size_t)(p - s));                         /* keep the indent */

    /* insert <shape>; name = [x]   <->   insert <shape>; name —> x */
    {
        const char *sh, *she, *nm, *nme;
        if (start_line(s, e, arrow, !to_tidy, &sh, &she, &nm, &nme)) {
            put(&o, p, 6);                               /* "insert", as typed */
            puts_(&o, " ");
            put(&o, sh, (size_t)(she - sh));
            puts_(&o, "; name ");
            if (to_tidy) { puts_(&o, arrow); puts_(&o, " "); put(&o, nm, (size_t)(nme - nm)); }
            else { puts_(&o, "= ["); put(&o, nm, (size_t)(nme - nm)); puts_(&o, "]"); }
            return done(&o);
        }
    }

    /* 15px top,right,left   <->   15px —> top, right, left */
    q = px_end(p, e);
    if (q > p) {
        const char *r = skip_sp(q, e);
        bool isTidy = starts(r, e, arrow);
        if (isTidy == to_tidy || r == e) { free(o.b); return NULL; }
        if (isTidy) r = skip_sp(r + strlen(arrow), e);
        put(&o, p, (size_t)(q - p));
        puts_(&o, " ");
        if (to_tidy) { puts_(&o, arrow); puts_(&o, " "); }
        while (r < e) {                                  /* the edges, as a, b, c */
            const char *w = r;
            while (r < e && *r != ',') r++;
            put(&o, w, (size_t)(trim_back(w, r) - w));
            if (r < e) { r = skip_sp(r + 1, e); if (r < e) puts_(&o, ", "); }
        }
        return done(&o);
    }

    /* Height = 15px   <->   Height —> 15px */
    if (word_at(p, e, "height") || word_at(p, e, "width")) {
        const char *w = p, *we = p + (word_at(p, e, "height") ? 6 : 5), *r = skip_sp(we, e);
        bool isTidy = starts(r, e, arrow);
        if (isTidy == to_tidy) { free(o.b); return NULL; }
        if (isTidy) r = skip_sp(r + strlen(arrow), e);
        else if (r < e && *r == '=') r = skip_sp(r + 1, e);
        if (r == e) { free(o.b); return NULL; }
        put(&o, w, (size_t)(we - w));
        if (to_tidy) { puts_(&o, " "); puts_(&o, arrow); puts_(&o, " "); }
        else puts_(&o, " = ");
        put(&o, r, (size_t)(e - r));
        return done(&o);
    }

    /* text [x] Hello; font impact; size15   <->   text [x] -> Hello -> font impact -> size 15 */
    if (word_at(p, e, "text")) {
        const char *t = p, *r = skip_sp(p + 4, e), *nm, *nme;
        if (r >= e || *r != '[') { free(o.b); return NULL; }
        nm = r + 1;
        nme = name_end(nm, e);
        if (nme == nm || nme >= e || *nme != ']') { free(o.b); return NULL; }
        r = skip_sp(nme + 1, e);
        put(&o, t, 4);                                   /* "text", as typed */
        puts_(&o, " [");
        put(&o, nm, (size_t)(nme - nm));
        puts_(&o, "]");

        if (to_tidy) {
            int part = 0;
            /* words that hold -> of their own would read back wrongly: leave them */
            if (r == e || contains(r, e, "->") || contains(r, e, arrow)) { free(o.b); return NULL; }
            while (r < e) {
                const char *w = r, *we;
                while (r < e && *r != ';') r++;
                we = trim_back(w, r);
                if (r < e) r = skip_sp(r + 1, e);
                if (we == w) continue;                   /* a stray ; */
                puts_(&o, " -> ");
                if (part++ && word_at(w, we, "size") && w + 4 < we &&
                    isdigit((unsigned char)w[4])) {      /* size15 -> size 15 */
                    put(&o, w, 4);
                    puts_(&o, " ");
                    put(&o, w + 4, (size_t)(we - w - 4));
                } else {
                    put(&o, w, (size_t)(we - w));
                }
            }
            return done(&o);
        } else {
            int part = 0;
            if (!starts(r, e, "->")) { free(o.b); return NULL; }
            r = skip_sp(r + 2, e);
            while (r < e) {                              /* split on " -> " */
                const char *w = r, *we;
                while (r < e && !(r[0] == ' ' && starts(r, e, " -> "))) r++;
                we = trim_back(w, r);
                if (r < e) r = skip_sp(r + 3, e);
                puts_(&o, part++ == 0 ? " " : "; ");
                put(&o, w, (size_t)(we - w));
            }
            return done(&o);
        }
    }

    free(o.b);
    return NULL;
}

/* ─────────────────────────────────────────────────────── blocks ── */

typedef struct { const char *s, *e, *next; } Line;   /* e before any \r; next past \n */

static int split_lines(const char *text, Line **out)
{
    int n = 0, cap = 64;
    const char *p = text;
    Line *ls = malloc(sizeof *ls * (size_t)cap);

    if (!ls) return 0;
    for (;;) {
        const char *s = p, *e;
        while (*p && *p != '\n') p++;
        e = p;
        if (e > s && e[-1] == '\r') e--;
        if (n == cap) {
            Line *grown = realloc(ls, sizeof *ls * (size_t)(cap *= 2));
            if (!grown) break;
            ls = grown;
        }
        ls[n].s = s;
        ls[n].e = e;
        ls[n].next = *p ? p + 1 : p;
        n++;
        if (!*p) break;
        p++;
    }
    *out = ls;
    return n;
}

/* The block starting at line i - raw or tidy as `tidy` says - ends at the
 * line returned (its end), or -1 when it is not a finished block. */
static int block_end(Line *ls, int n, int i, const char *arrow, bool tidy)
{
    const char *a, *b, *c, *d;
    int j;

    if (!start_line(ls[i].s, ls[i].e, arrow, tidy, &a, &b, &c, &d)) return -1;
    for (j = i + 1; j < n; j++) {
        const char *x, *y, *z, *w;
        if (end_line(ls[j].s, ls[j].e)) return j;
        /* another shape starting means this one was never closed */
        if (start_line(ls[j].s, ls[j].e, arrow, false, &x, &y, &z, &w) ||
            start_line(ls[j].s, ls[j].e, arrow, true, &x, &y, &z, &w))
            return -1;
    }
    return -1;
}

/* lines i .. j-1 converted, as one string - the bytes from ls[i].s to ls[j].s */
static char *convert_lines(Line *ls, int i, int j, const char *arrow, bool to_tidy)
{
    Buf o = { NULL, 0, 0 };
    int k;

    for (k = i; k < j; k++) {
        char *c = convert(ls[k].s, ls[k].e, arrow, to_tidy);
        if (c) { puts_(&o, c); free(c); }
        else put(&o, ls[k].s, (size_t)(ls[k].e - ls[k].s));
        put(&o, ls[k].e, (size_t)(ls[k].next - ls[k].e));       /* its \r\n or \n */
    }
    return done(&o);
}

int tidy_edits(const char *text, const char *arrow, long caretLine, bool on,
               TidyEdit *out, int max)
{
    Line *ls = NULL;
    int n = split_lines(text, &ls), i = 0, count = 0;

    while (i < n && count < max) {
        int j;
        bool inside;

        /* a tidy block: back to raw if the caret is in it, or tidy view is off */
        j = block_end(ls, n, i, arrow, true);
        if (j >= 0) {
            inside = caretLine >= i && caretLine < j;
            if (!on || inside) {
                out[count].start = (size_t)(ls[i].s - text);
                out[count].len = (size_t)(ls[j].s - ls[i].s);
                out[count].with = convert_lines(ls, i, j, arrow, false);
                count++;
            }
            i = j + 1;
            continue;
        }

        /* a finished raw block: tidied, unless the caret is in it */
        if (on) {
            j = block_end(ls, n, i, arrow, false);
            if (j >= 0) {
                inside = caretLine >= i && caretLine < j;
                if (!inside) {
                    char *with = convert_lines(ls, i, j, arrow, true);
                    size_t len = (size_t)(ls[j].s - ls[i].s);
                    if (with && (strlen(with) != len || memcmp(with, ls[i].s, len) != 0)) {
                        out[count].start = (size_t)(ls[i].s - text);
                        out[count].len = len;
                        out[count].with = with;
                        count++;
                    } else {
                        free(with);
                    }
                }
                i = j + 1;
                continue;
            }
        }
        i++;
    }
    free(ls);
    return count;
}

void tidy_free_edits(TidyEdit *edits, int n)
{
    int i;
    for (i = 0; i < n; i++) free(edits[i].with);
}

char *tidy_raw(const char *text, const char *arrow)
{
    TidyEdit ed[256];
    int n = tidy_edits(text, arrow, -1, false, ed, 256), i;
    Buf o = { NULL, 0, 0 };
    size_t at = 0;

    for (i = 0; i < n; i++) {
        put(&o, text + at, ed[i].start - at);
        puts_(&o, ed[i].with);
        at = ed[i].start + ed[i].len;
    }
    puts_(&o, text + at);
    tidy_free_edits(ed, n);
    return done(&o);
}
