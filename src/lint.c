/* Mistakes that can be seen without running the program - see adda_lint in
 * adda.h. Everything here is about names: what the program sets or defines
 * anywhere, against what it uses. Order is not considered, because a
 * function can be defined below its call and return (N) can jump about. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "adda.h"

#define MAX_NAMES 512

typedef struct { const char *s; uint32_t len; int arity; } Name;   /* arity -1: not known */
typedef struct { Name v[MAX_NAMES]; int n; } Names;

typedef struct {
    Names      vars, funcs, shapes;
    uint32_t   firstApp;       /* the first openApplication outside a function; 0 none */
    int        inShape;        /* in a named shape's block: its insert has said it all */
    LintReport report;
    void      *ctx;
} Lint;

static bool same(const char *a, const char *b, uint32_t len, bool anyCase)
{
    uint32_t i;
    if (!anyCase) return memcmp(a, b, len) == 0;
    for (i = 0; i < len; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static Name *find(Names *ns, const char *s, uint32_t len, bool anyCase)
{
    int i;
    for (i = 0; i < ns->n; i++)
        if (ns->v[i].len == len && same(ns->v[i].s, s, len, anyCase)) return &ns->v[i];
    return NULL;
}

static void add(Names *ns, const char *s, uint32_t len, int arity)
{
    Name *had = find(ns, s, len, false);
    if (had) {
        if (had->arity != arity) had->arity = -1;     /* defined twice, differently */
        return;
    }
    if (ns->n == MAX_NAMES) return;
    ns->v[ns->n].s = s;
    ns->v[ns->n].len = len;
    ns->v[ns->n].arity = arity;
    ns->n++;
}

/* the closest name to s, if it is a near miss - never for a name of one or
 * two letters, which is a near miss of every other short name */
static const Name *nearest(const Names *ns, const char *s, uint32_t len)
{
    const Name *best = NULL;
    uint32_t bestD = len <= 2 ? 1 : len <= 4 ? 2 : 3;
    int i;
    for (i = 0; i < ns->n; i++) {
        uint32_t d = adda_edit_distance(s, len, ns->v[i].s, ns->v[i].len);
        if (d < bestD) { bestD = d; best = &ns->v[i]; }
    }
    return best;
}

/* ─────────────────────────────────────── what the program defines ── */

static void collect(Lint *L, Node *n, bool inDefine)
{
    uint32_t i;

    if (!n || n->kind == N_GOTO) return;       /* a jump's `a` is a note, not code */
    switch (n->kind) {
    case N_ASSIGN:
        if (n->a && n->a->kind == N_VAR) add(&L->vars, n->a->name->bytes, n->a->name->len, 0);
        break;
    case N_FOREACH:
        if (n->name) add(&L->vars, n->name->bytes, n->name->len, 0);
        break;
    case N_DEFINE:
        add(&L->funcs, n->name->bytes, n->name->len, n->nparams);
        for (i = 0; i < n->nparams; i++)
            add(&L->vars, n->params[i]->bytes, n->params[i]->len, 0);
        collect(L, n->b, true);
        return;
    case N_SHAPE:
        if (n->name) add(&L->shapes, n->name->bytes, n->name->len, 0);
        break;
    case N_OPENAPP:
        if (!inDefine && (!L->firstApp || n->line < L->firstApp)) L->firstApp = n->line;
        break;
    default:
        break;
    }
    collect(L, n->a, inDefine);
    collect(L, n->b, inDefine);
    collect(L, n->c, inDefine);
    for (i = 0; i < n->nkids; i++) collect(L, n->kids[i], inDefine);
}

/* Lines the parser could not read still say what they meant to define: any
 * [name] in them is known (a shape's too, on a line with insert), and so is a
 * word after define. */
static void collect_text(Lint *L, const char *p)
{
    while (p && *p) {
        const char *e = p, *q;
        bool shape;
        while (*e && *e != '\n') e++;
        shape = false;
        for (q = p; q + 6 <= e; q++)
            if (memcmp(q, "insert", 6) == 0) shape = true;

        for (q = p; q < e; q++) {
            if (*q == '[') {
                const char *s = q + 1, *t = s;
                while (t < e && (isalnum((unsigned char)*t) || *t == '_')) t++;
                if (t < e && *t == ']' && t > s) {
                    add(&L->vars, s, (uint32_t)(t - s), 0);
                    if (shape) add(&L->shapes, s, (uint32_t)(t - s), 0);
                }
                q = t;
            } else if (e - q > 6 && memcmp(q, "define", 6) == 0 && (q[6] == ' ' || q[6] == '\t')) {
                const char *s = q + 6, *t;
                while (s < e && (*s == ' ' || *s == '\t')) s++;
                for (t = s; t < e && (isalnum((unsigned char)*t) || *t == '_'); t++) {}
                if (t > s) add(&L->funcs, s, (uint32_t)(t - s), -1);
                q = t - 1;
            } else if (e - q >= 15 && memcmp(q, "openApplication", 15) == 0 && !L->firstApp) {
                L->firstApp = 1;
            }
        }
        p = *e ? e + 1 : e;
    }
}

/* ────────────────────────────────────────────── what it uses ── */

static void say(Lint *L, uint32_t line, const char *mark, const char *fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    L->report(line, mark, msg, L->ctx);
}

static void check(Lint *L, Node *n, bool inDefine)
{
    char mark[80];
    uint32_t i;

    if (!n || n->kind == N_GOTO) return;
    switch (n->kind) {
    case N_VAR:
        if (!n->flag && !find(&L->vars, n->name->bytes, n->name->len, false) &&
            !find(&L->funcs, n->name->bytes, n->name->len, false)) {
            const Name *near = nearest(&L->vars, n->name->bytes, n->name->len);
            snprintf(mark, sizeof mark, "[%s]", n->name->bytes);
            if (near)
                say(L, n->line, mark, "nothing ever sets [%s] - did you mean [%.*s]?",
                    n->name->bytes, (int)near->len, near->s);
            else
                say(L, n->line, mark, "nothing ever sets [%s] - give it a value first, "
                    "like: [%s] = 0", n->name->bytes, n->name->bytes);
        }
        return;

    case N_ASSIGN:
        if (n->a && n->a->kind != N_VAR) check(L, n->a, inDefine);   /* item 1 of [x] = ... */
        check(L, n->b, inDefine);
        return;

    case N_CALL: {
        const Name *f = find(&L->funcs, n->name->bytes, n->name->len, false);
        if (!f) {
            const Name *near = nearest(&L->funcs, n->name->bytes, n->name->len);
            if (near)
                say(L, n->line, n->name->bytes, "there is no function called '%s' - did you "
                    "mean '%.*s'?", n->name->bytes, (int)near->len, near->s);
            else
                say(L, n->line, n->name->bytes, "there is no function called '%s' - make one "
                    "with: define %s", n->name->bytes, n->name->bytes);
        } else if (f->arity >= 0 && (uint32_t)f->arity != n->nkids) {
            say(L, n->line, n->name->bytes, "'%s' needs %d input%s, but got %u",
                n->name->bytes, f->arity, f->arity == 1 ? "" : "s", n->nkids);
        }
        break;
    }

    case N_SHAPE:
        if (!inDefine && (!L->firstApp || L->firstApp > n->line))
            say(L, n->line, "insert", "insert draws in the app window - put openApplication "
                "on a line before it");
        break;

    case N_BLOCK:
        /* a named shape is a block: the shape, then its text lines */
        if (n->nkids && n->kids[0]->kind == N_SHAPE && n->kids[0]->name &&
            n->kids[0]->line == n->line) {
            check(L, n->kids[0], inDefine);
            L->inShape++;
            for (i = 1; i < n->nkids; i++) check(L, n->kids[i], inDefine);
            L->inShape--;
            return;
        }
        break;

    case N_SHAPETEXT:
        snprintf(mark, sizeof mark, "[%s]", n->name->bytes);
        if (!find(&L->shapes, n->name->bytes, n->name->len, true)) {
            const Name *near = nearest(&L->shapes, n->name->bytes, n->name->len);
            if (near)
                say(L, n->line, mark, "there is no shape called [%s] - did you mean [%.*s]?",
                    n->name->bytes, (int)near->len, near->s);
            else
                say(L, n->line, mark, "there is no shape called [%s] - name one with: "
                    "insert box; name = [%s]", n->name->bytes, n->name->bytes);
        } else if (!inDefine && !L->inShape && (!L->firstApp || L->firstApp > n->line)) {
            say(L, n->line, "text", "text draws in the app window - put openApplication "
                "on a line before it");
        }
        break;

    case N_DEFINE:
        check(L, n->b, true);
        return;

    default:
        break;
    }
    check(L, n->a, inDefine);
    check(L, n->b, inDefine);
    check(L, n->c, inDefine);
    for (i = 0; i < n->nkids; i++) check(L, n->kids[i], inDefine);
}

void adda_lint(Node *program, const char *extra, LintReport report, void *ctx)
{
    static Lint L;               /* large: the name tables */

    memset(&L, 0, sizeof L);
    L.report = report;
    L.ctx = ctx;
    collect(&L, program, false);
    collect_text(&L, extra);
    check(&L, program, false);
}
