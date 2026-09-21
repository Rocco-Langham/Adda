/* The parser.
 *
 * Adda is line-oriented, so almost everything here works on a *run*: a range
 * of tokens bounded by the end of a line, a comma, a brace, or a keyword.
 * Before a run is parsed it is scanned once to decide what it even is:
 *
 *     VALUE  - it contains an operator, starts with a leading keyword, is a
 *              lone number, or is a whole {...} group.  Bare words in a VALUE
 *              run are variables.
 *     TEXT   - anything else.  The run is taken from the raw source, so
 *              spacing survives, and {...} holes are interpolated.
 *
 * That single decision is what lets `name = Rocco` and `next = age + 1` live
 * in the same language without quotes.
 */
#include <stdio.h>
#include <string.h>
#include "adda.h"

typedef struct {
    Token    *t;
    uint32_t  n;
    uint32_t  i;             /* statement cursor */
    int       in_condition;  /* bare words resolve softly inside a condition */
    int       depth;         /* block nesting; functions must be at depth 0 */
    int       interactive;   /* REPL: a bare expression is a statement */
} P;

/* A cursor over one token range, used while parsing an expression. */
typedef struct { P *p; uint32_t i, end; } E;

#define STOP_END  1
#define STOP_DELAY 4          /* `delay end` closes a delay block too */
#define STOP_ELSE 2

static Node *parse_run(P *p, uint32_t from, uint32_t to);
static Node *expr(E *e);
static Node *primary(E *e);
static Node *statement(P *p);
static Node *parse_block(P *p, int stops, const char *opener, uint32_t opener_line);

/* ------------------------------------------------------------------- nodes */

static Node *node(NodeKind kind, uint32_t line)
{
    Node *n = adda_alloc(sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

static void add_kid(Node *n, Node *kid)
{
    uint32_t cap = 4;

    if (n->nkids == 0) {
        n->kids = adda_alloc(sizeof(Node *) * cap);
    } else if (n->nkids >= 4 && (n->nkids & (n->nkids - 1)) == 0) {
        Node **grown = adda_alloc(sizeof(Node *) * n->nkids * 2);
        memcpy(grown, n->kids, sizeof(Node *) * n->nkids);
        n->kids = grown;
    }
    n->kids[n->nkids++] = kid;
}

/* ----------------------------------------------------------------- scanning */

static Token *at(P *p, uint32_t i) { return &p->t[i < p->n ? i : p->n - 1]; }
static bool   word_at(P *p, uint32_t i, const char *w) { return token_is_word(at(p, i), w); }

static bool is_op_kind(TokenKind k) { return k >= TK_PLUS && k <= TK_NE; }

/* A string token's span covers its contents, not its quotes, so a raw source
 * slice has to step back over the opener and past the closer. */
static const char *token_start(Token *t)
{
    return t->start - (t->kind == TK_STRING ? 1 : 0);
}

static const char *token_end(Token *t)
{
    return t->start + t->len + (t->kind == TK_STRING ? 1 : 0);
}

static uint32_t line_end(P *p, uint32_t from)
{
    uint32_t i = from;
    while (i < p->n && p->t[i].kind != TK_NEWLINE && p->t[i].kind != TK_EOF) i++;
    return i;
}

/* Index of the closer matching the opener at `open`, or -1. */
static int match_close(P *p, uint32_t open, uint32_t to)
{
    TokenKind opener = p->t[open].kind;
    TokenKind closer = (opener == TK_LPAREN) ? TK_RPAREN : TK_RBRACE;
    int depth = 0;
    uint32_t i;

    for (i = open; i < to; i++) {
        if (p->t[i].kind == opener) depth++;
        else if (p->t[i].kind == closer && --depth == 0) return (int)i;
    }
    return -1;
}

/* Search at bracket depth 0 - the primitive the whole parser is built on. */
static int find_top(P *p, uint32_t from, uint32_t to, TokenKind kind, const char *word)
{
    int depth = 0;
    uint32_t i;

    for (i = from; i < to; i++) {
        TokenKind k = p->t[i].kind;
        if (k == TK_LPAREN || k == TK_LBRACE) depth++;
        else if (k == TK_RPAREN || k == TK_RBRACE) depth--;
        else if (depth == 0) {
            if (word ? token_is_word(&p->t[i], word) : (k == kind)) return (int)i;
        }
    }
    return -1;
}

static int find_last_top_word(P *p, uint32_t from, uint32_t to, const char *word)
{
    int depth = 0, found = -1;
    uint32_t i;

    for (i = from; i < to; i++) {
        TokenKind k = p->t[i].kind;
        if (k == TK_LPAREN || k == TK_LBRACE) depth++;
        else if (k == TK_RPAREN || k == TK_RBRACE) depth--;
        else if (depth == 0 && token_is_word(&p->t[i], word)) found = (int)i;
    }
    return found;
}

/* ---------------------------------------------------------------- mode scan */

static bool leading_keyword(P *p, uint32_t i)
{
    return word_at(p, i, "list")  || word_at(p, i, "map")  ||
           word_at(p, i, "call")  || word_at(p, i, "item") ||
           word_at(p, i, "length") || word_at(p, i, "has") ||
           word_at(p, i, "ask");
}

/* The decision described at the top of this file. */
static bool run_is_value(P *p, uint32_t from, uint32_t to)
{
    uint32_t i;

    if (to <= from) return false;

    /* A leading keyword decides on its own, so a bare `map` or `list` is an
     * empty collection rather than the word. */
    if (leading_keyword(p, from)) return true;

    if (to - from == 1) {
        Token *t = &p->t[from];
        if (t->kind == TK_NUMBER) return true;
        return token_is_word(t, "true") || token_is_word(t, "false") ||
               token_is_word(t, "nothing");
    }

    if (p->t[from].kind == TK_LBRACE && match_close(p, from, to) == (int)to - 1)
        return true;

    /* A fully bracketed run is whatever its contents are, so `(list 2, 3)` is a
     * list while `note = (optional)` stays as the words you typed. */
    if (p->t[from].kind == TK_LPAREN && match_close(p, from, to) == (int)to - 1)
        return run_is_value(p, from + 1, to - 1);

    /* Only an operator outside every bracket decides the mode. One inside a
     * {...} hole belongs to that hole, so `print Next year {age + 1}` stays
     * text; `total = (a + b) * 2` is still maths because of the '*', and a run
     * wrapped entirely in brackets was already handled just above. */
    {
        int depth = 0;

        for (i = from; i < to; i++) {
            TokenKind k = p->t[i].kind;
            if (k == TK_LPAREN || k == TK_LBRACE) depth++;
            else if (k == TK_RPAREN || k == TK_RBRACE) depth--;
            else if (depth == 0 && is_op_kind(k)) return true;
        }
    }

    return false;
}

/* ---------------------------------------------------------------- templates */

typedef struct { char *bytes; uint32_t len, cap; } SB;

static void sb_put(SB *s, char c)
{
    if (s->len + 1 > s->cap) {
        uint32_t cap = s->cap < 32 ? 32 : s->cap * 2;
        char *bytes = adda_alloc(cap);
        if (s->len) memcpy(bytes, s->bytes, s->len);
        s->bytes = bytes;
        s->cap = cap;
    }
    s->bytes[s->len++] = c;
}

static Node *text_node(const char *bytes, uint32_t len, uint32_t line)
{
    Node *n = node(N_TEXT, line);
    n->name = text_new(bytes, len);
    return n;
}

/* {name} on its own was how a variable used to be written. A bare word is
 * text now, so rather than quietly print the word, say what to write. */
static void old_style_hole(const Token *t)
{
    if (t->kind != TK_WORD || t->bracketed) return;
    if (token_is_word(t, "true") || token_is_word(t, "false") ||
        token_is_word(t, "nothing")) return;
    adda_error_at(t->start, t->line,
                  "to use a variable, write [%.*s] - braces are for sums, as in {[%.*s] + 1}",
                  (int)t->len, t->start, (int)t->len, t->start);
}

/* Build a text template from raw source, so spacing is preserved exactly and
 * {...} holes keep their real line numbers for error messages. */
static Node *build_template(P *p, const char *s, const char *e, bool quoted,
                            uint32_t line)
{
    Node *tpl = node(N_TEMPLATE, line);
    SB sb;
    const char *q = s;
    bool in_quotes = false;      /* a "..." section inside an otherwise bare run */

    sb.bytes = NULL; sb.len = sb.cap = 0;

    while (q < e) {
        if (q[0] == '{' && q + 1 < e && q[1] == '{') { sb_put(&sb, '{'); q += 2; continue; }
        if (q[0] == '}' && q + 1 < e && q[1] == '}') { sb_put(&sb, '}'); q += 2; continue; }

        /* \" is how you get a literal quote mark into bare text */
        if (q[0] == '\\' && q + 1 < e && q[1] == '"') { sb_put(&sb, '"'); q += 2; continue; }

        /* Quotes inside a bare run just mark a literal section - they let you
         * write a keyword as text - so they are not printed themselves. */
        if (!quoted && q[0] == '"') { in_quotes = !in_quotes; q++; continue; }

        if ((quoted || in_quotes) && q[0] == '\\' && q + 1 < e) {
            char c = q[1];
            sb_put(&sb, c == 'n' ? '\n' : c == 't' ? '\t' : c);
            q += 2;
            continue;
        }

        if (q[0] == '{' || adda_bracket_name(q)) {
            const char *close = q + 1;
            int depth = 1;

            if (q[0] == '[') {             /* [name]: the name is the whole hole */
                close = q + adda_bracket_name(q) - 1;
                depth = 0;
            }
            while (close < e && depth > 0) {
                if (*close == '{') depth++;
                else if (*close == '}') depth--;
                if (depth == 0) break;
                close++;
            }
            if (depth != 0)
                adda_error_at(q, line, "this {...} is missing its closing brace");

            if (sb.len) { add_kid(tpl, text_node(sb.bytes, sb.len, line)); sb.len = 0; }

            if (q[0] == '[') {                  /* [name]: straight to the variable */
                Node *v = node(N_VAR, line);
                v->name = intern(q + 1, (uint32_t)(close - q - 1));
                add_kid(tpl, v);
                q = close + 1;
                continue;
            }

            {
                TokenList sub = lex_range(s, q + 1, close, line);
                P inner;
                E cur;
                Node *hole;

                inner.t = sub.tokens;
                inner.n = sub.count;
                inner.i = 0;
                inner.in_condition = p->in_condition;
                inner.depth = p->depth;

                cur.p = &inner;
                cur.i = 0;
                cur.end = sub.count > 0 ? sub.count - 1 : 0;   /* drop the EOF */
                if (cur.end == 0)
                    adda_error_at(q, line, "this {} is empty - put a value inside it");

                if (cur.end == 1 && q[0] == '{') old_style_hole(&inner.t[0]);
                hole = expr(&cur);
                if (cur.i < cur.end)
                    adda_error_at(inner.t[cur.i].start, inner.t[cur.i].line,
                                  "I did not expect '%.*s' here",
                                  (int)inner.t[cur.i].len, inner.t[cur.i].start);
                add_kid(tpl, hole);
            }
            q = close + 1;
            continue;
        }

        sb_put(&sb, *q++);
    }

    if (sb.len || tpl->nkids == 0)
        add_kid(tpl, text_node(sb.bytes, sb.len, line));

    if (tpl->nkids == 1 && tpl->kids[0]->kind == N_TEXT) return tpl->kids[0];
    return tpl;
}

static Node *parse_text_run(P *p, uint32_t from, uint32_t to)
{
    Token *first = &p->t[from];
    Token *last  = &p->t[to - 1];

    if (to - from == 1 && first->kind == TK_STRING)
        return build_template(p, first->start, first->start + first->len, true,
                              first->line);

    return build_template(p, token_start(first), token_end(last), false, first->line);
}

/* -------------------------------------------------------------- expressions */

static bool at_kind(E *e, TokenKind k) { return e->i < e->end && e->p->t[e->i].kind == k; }
static bool at_word(E *e, const char *w) { return e->i < e->end && token_is_word(&e->p->t[e->i], w); }

static uint32_t here_line(E *e)
{
    return e->p->t[e->i < e->p->n ? e->i : e->p->n - 1].line;
}

static void expect_word(E *e, const char *w, const char *message)
{
    if (!at_word(e, w)) {
        Token *t = &e->p->t[e->i < e->p->n ? e->i : e->p->n - 1];
        adda_error_at(t->start, t->line, "%s", message);
    }
    e->i++;
}

static Node *binary(int op, Node *a, Node *b, uint32_t line)
{
    Node *n = node(N_BINARY, line);
    n->op = op;
    n->a = a;
    n->b = b;
    return n;
}

/* Split a range on top-level commas; each piece is its own run, so
 * `list Rocco, Mary` is two names while `list 1, 2` is two numbers. */
static void parse_elements(P *p, Node *into, uint32_t from, uint32_t to)
{
    while (from < to) {
        int comma = find_top(p, from, to, TK_COMMA, NULL);
        uint32_t stop = comma < 0 ? to : (uint32_t)comma;
        if (stop > from) add_kid(into, parse_run(p, from, stop));
        from = stop + 1;
    }
}

static Node *primary(E *e)
{
    P *p = e->p;
    Token *t;
    uint32_t line;

    if (e->i >= e->end) {
        Token *last = &p->t[e->i < p->n ? e->i : p->n - 1];
        adda_error_at(last->start, last->line, "something is missing at the end of this line");
    }

    t = &p->t[e->i];
    line = t->line;

    if (t->kind == TK_NUMBER) {
        Node *n = node(N_NUMBER, line);
        n->number = t->number;
        e->i++;
        return n;
    }

    if (t->kind == TK_STRING) {
        e->i++;
        return build_template(p, t->start, t->start + t->len, true, line);
    }

    if (t->kind == TK_LPAREN || t->kind == TK_LBRACE) {
        int close = match_close(p, e->i, e->end);
        E sub;
        Node *inner;

        if (close < 0)
            adda_error_at(t->start, line, "this %s is missing its closing %s",
                          t->kind == TK_LPAREN ? "(" : "{",
                          t->kind == TK_LPAREN ? ")" : "}");
        sub.p = p;
        sub.i = e->i + 1;
        sub.end = (uint32_t)close;
        if (sub.i >= sub.end)
            adda_error_at(t->start, line, "there is nothing inside these brackets");
        if (t->start[0] == '{' && sub.end == sub.i + 1)
            old_style_hole(&p->t[sub.i]);
        inner = expr(&sub);
        e->i = (uint32_t)close + 1;
        return inner;
    }

    if (t->kind == TK_MINUS) {                   /* spaced unary minus */
        Node *n = node(N_UNARY, line);
        e->i++;
        n->op = TK_MINUS;
        n->a = primary(e);
        return n;
    }

    if (t->kind != TK_WORD)
        adda_error_at(t->start, line, "I did not expect '%.*s' here",
                      (int)t->len, t->start);

    if (token_is_word(t, "true"))    { e->i++; { Node *n = node(N_BOOL, line); n->flag = true;  return n; } }
    if (token_is_word(t, "false"))   { e->i++; { Node *n = node(N_BOOL, line); n->flag = false; return n; } }
    if (token_is_word(t, "nothing")) { e->i++; return node(N_NOTHING, line); }

    if (token_is_word(t, "map")) { e->i++; return node(N_MAP, line); }

    if (token_is_word(t, "list")) {
        Node *n = node(N_LIST, line);
        e->i++;
        parse_elements(p, n, e->i, e->end);
        e->i = e->end;
        return n;
    }

    if (token_is_word(t, "length")) {
        Node *n = node(N_LENGTH, line);
        e->i++;
        expect_word(e, "of", "'length' is a special word in Adda: write 'length of <something>'. To use it as plain text, put it in quotes.");
        n->a = primary(e);
        return n;
    }

    /* `ask <prompt>` reads a line from whoever is running the program. The
     * prompt is an ordinary run, so `ask How old are you?` needs no quotes and
     * `ask Hello [name], how old are you?` fills in the value. */
    if (token_is_word(t, "ask")) {
        Node *n = node(N_ASK, line);
        e->i++;
        if (e->i < e->end) n->a = parse_run(p, e->i, e->end);
        e->i = e->end;
        return n;
    }

    /* `has <key> of <map>` - the same bracket-pair shape as `item ... of`. */
    if (token_is_word(t, "has")) {
        Node *n = node(N_HAS, line);
        int of;
        E key;

        e->i++;
        of = find_top(p, e->i, e->end, TK_EOF, "of");
        if (of < 0)
            adda_error_at(t->start, line,
                          "'has' is a special word in Adda: write 'has <key> of <map>'. "
                          "To use it as plain text, put it in quotes.");
        key.p = p; key.i = e->i; key.end = (uint32_t)of;
        if (key.i >= key.end)
            adda_error_at(t->start, line, "has needs a key, as in: has name of person");
        n->a = expr(&key);
        e->i = (uint32_t)of + 1;
        n->b = primary(e);
        return n;
    }

    /* `item <index> of <container>` is a bracket pair, not an infix operator:
     * that is what makes both `item i + 1 of nums` and `item 1 of nums + 1`
     * parse the way they read. */
    if (token_is_word(t, "item")) {
        Node *n = node(N_INDEX, line);
        int of;
        E idx;

        e->i++;
        of = find_top(p, e->i, e->end, TK_EOF, "of");
        if (of < 0)
            adda_error_at(t->start, line, "'item' is a special word in Adda: write 'item 1 of nums'. To use it as plain text, put it in quotes.");
        idx.p = p; idx.i = e->i; idx.end = (uint32_t)of;
        if (idx.i >= idx.end)
            adda_error_at(t->start, line, "item needs a position, as in: item 1 of nums");
        n->a = expr(&idx);
        e->i = (uint32_t)of + 1;
        n->b = primary(e);
        return n;
    }

    if (token_is_word(t, "call")) {
        Node *n = node(N_CALL, line);
        e->i++;
        if (e->i >= e->end || p->t[e->i].kind != TK_WORD)
            adda_error_at(t->start, line, "'call' is a special word in Adda: write 'call <name> with <values>'. To use it as plain text, put it in quotes.");
        n->name = p->t[e->i].text;
        e->i++;
        if (at_word(e, "with")) {
            e->i++;
            parse_elements(p, n, e->i, e->end);
            e->i = e->end;
        }
        return n;
    }

    /* a plain word: either `key of container`, a variable written [name], or
     * just the word itself - a bare word is always text */
    e->i++;
    if (!t->bracketed && at_word(e, "of")) {
        Node *n = node(N_FIELD, line);
        n->name = t->text;
        e->i++;
        n->a = primary(e);                 /* right-associative on purpose */
        return n;
    }
    if (!t->bracketed) return text_node(t->start, t->len, line);
    {
        Node *n = node(N_VAR, line);
        n->name = t->text;
        return n;
    }
}

static Node *unary(E *e)
{
    if (at_kind(e, TK_MINUS)) {
        Node *n = node(N_UNARY, here_line(e));
        e->i++;
        n->op = TK_MINUS;
        n->a = unary(e);
        return n;
    }
    return primary(e);
}

static Node *factor(E *e)
{
    Node *left = unary(e);
    while (at_kind(e, TK_STAR) || at_kind(e, TK_SLASH) || at_kind(e, TK_PERCENT)) {
        int op = e->p->t[e->i].kind;
        uint32_t line = here_line(e);
        e->i++;
        left = binary(op, left, unary(e), line);
    }
    return left;
}

static Node *term(E *e)
{
    Node *left = factor(e);
    while (at_kind(e, TK_PLUS) || at_kind(e, TK_MINUS)) {
        int op = e->p->t[e->i].kind;
        uint32_t line = here_line(e);
        e->i++;
        left = binary(op, left, factor(e), line);
    }
    return left;
}

/* Comparisons chain: `1 < x < 10` means what it looks like. */
static Node *comparison(E *e)
{
    Node *left = term(e);
    Node *chain = NULL;

    while (at_kind(e, TK_LT) || at_kind(e, TK_GT) ||
           at_kind(e, TK_LE) || at_kind(e, TK_GE)) {
        int op = e->p->t[e->i].kind;
        uint32_t line = here_line(e);
        Node *right, *cmp;

        e->i++;
        right = term(e);
        cmp = binary(op, left, right, line);
        if (chain) {
            Node *both = node(N_AND, line);
            both->a = chain;
            both->b = cmp;
            chain = both;
        } else {
            chain = cmp;
        }
        left = right;
    }
    return chain ? chain : left;
}

static Node *equality(E *e)
{
    Node *left = comparison(e);

    if (at_word(e, "is")) {
        uint32_t line = here_line(e);
        int op = TK_ASSIGN;                 /* '=' doubles as "is equal to" */
        e->i++;
        if (at_word(e, "not")) { e->i++; op = TK_NE; }
        return binary(op, left, comparison(e), line);
    }
    if (at_kind(e, TK_ASSIGN) || at_kind(e, TK_NE)) {
        int op = e->p->t[e->i].kind;
        uint32_t line = here_line(e);
        e->i++;
        return binary(op, left, comparison(e), line);
    }
    return left;
}

static Node *not_expr(E *e)
{
    if (at_word(e, "not")) {
        Node *n = node(N_UNARY, here_line(e));
        e->i++;
        n->op = TK_WORD;                    /* logical not */
        n->a = not_expr(e);
        return n;
    }
    return equality(e);
}

static Node *and_expr(E *e)
{
    Node *left = not_expr(e);
    while (at_word(e, "and")) {
        Node *n = node(N_AND, here_line(e));
        e->i++;
        n->a = left;
        n->b = not_expr(e);
        left = n;
    }
    return left;
}

static Node *expr(E *e)
{
    Node *left = and_expr(e);
    while (at_word(e, "or")) {
        Node *n = node(N_OR, here_line(e));
        e->i++;
        n->a = left;
        n->b = and_expr(e);
        left = n;
    }
    return left;
}

/* --------------------------------------------------------------- runs */

static Node *parse_value(P *p, uint32_t from, uint32_t to)
{
    E e;
    Node *n;

    e.p = p; e.i = from; e.end = to;
    n = expr(&e);
    if (e.i < e.end)
        adda_error_at(p->t[e.i].start, p->t[e.i].line,
                      "I did not expect '%.*s' here",
                      (int)p->t[e.i].len, p->t[e.i].start);
    return n;
}

static Node *parse_run(P *p, uint32_t from, uint32_t to)
{
    if (to <= from) return text_node("", 0, at(p, from)->line);
    if (run_is_value(p, from, to)) return parse_value(p, from, to);

    return parse_text_run(p, from, to);
}

static Node *parse_condition(P *p, uint32_t from, uint32_t to, const char *what)
{
    Node *n;

    if (to <= from)
        adda_error_at(at(p, from)->start, at(p, from)->line,
                      "%s needs something to test, as in: %s score > 10", what, what);
    p->in_condition++;
    n = parse_value(p, from, to);
    p->in_condition--;
    return n;
}

/* -------------------------------------------------------------- statements */

static void end_line(P *p, uint32_t e)
{
    p->i = e;
    if (p->i < p->n && p->t[p->i].kind == TK_NEWLINE) p->i++;
}

static void expect_end(P *p, const char *opener, uint32_t opener_line)
{
    if (!word_at(p, p->i, "end"))
        adda_error(opener_line,
                   "this '%s' is never closed - add 'end' after its last line",
                   opener);
    end_line(p, p->i + 1);
}

static Node *parse_if(P *p)
{
    uint32_t s = p->i, e = line_end(p, s);
    uint32_t line = p->t[s].line;
    Node *n = node(N_IF, line);

    n->a = parse_condition(p, s + 1, e, "if");
    end_line(p, e);

    p->depth++;
    n->b = parse_block(p, STOP_END | STOP_ELSE, "if", line);

    if (word_at(p, p->i, "else")) {
        uint32_t else_line = at(p, p->i)->line;
        p->i++;
        if (word_at(p, p->i, "if")) {        /* chained: the nested if eats 'end' */
            n->c = parse_if(p);
            p->depth--;
            return n;
        }
        end_line(p, line_end(p, p->i));
        n->c = parse_block(p, STOP_END, "else", else_line);
    }
    p->depth--;
    expect_end(p, "if", line);
    return n;
}

/*   delay - 1500        wait 1500 milliseconds (1000 is a second),
 *   print hello         then run everything down to
 *   delay end           here - a plain `end` closes it too
 * The dash is optional: `delay 1500` and `delay - [wait]` work as well. */
static Node *parse_delay(P *p)
{
    uint32_t s = p->i, e = line_end(p, s);
    uint32_t line = p->t[s].line;
    uint32_t from = s + 1;
    Node *n = node(N_DELAY, line);

    if (from < e && p->t[from].kind == TK_MINUS) from++;
    if (from >= e)
        adda_error_at(p->t[s].start, line,
                      "delay needs a time in milliseconds, as in: delay - 1500");
    n->a = parse_value(p, from, e);
    end_line(p, e);
    p->depth++;
    n->b = parse_block(p, STOP_END | STOP_DELAY, "delay", line);
    p->depth--;
    if (word_at(p, p->i, "delay")) end_line(p, p->i + 2);    /* delay end */
    else expect_end(p, "delay", line);
    return n;
}

static Node *parse_while(P *p)
{
    uint32_t s = p->i, e = line_end(p, s);
    uint32_t line = p->t[s].line;
    Node *n = node(N_WHILE, line);

    n->a = parse_condition(p, s + 1, e, "while");
    end_line(p, e);
    p->depth++;
    n->b = parse_block(p, STOP_END, "while", line);
    p->depth--;
    expect_end(p, "while", line);
    return n;
}

/* A place that wants a bare name also takes it as [name]: steps past the
 * brackets so t[*i] is the word. */
static void skip_bracket(P *p, uint32_t *i, uint32_t e)
{
    if (*i + 2 < e && p->t[*i].kind == TK_LBRACE && p->t[*i].start[0] == '[' &&
        p->t[*i + 1].kind == TK_WORD && p->t[*i + 2].kind == TK_RBRACE)
        (*i)++;
}

/* ...and past the closing one, once the name has been read */
static void skip_close(P *p, uint32_t *i, uint32_t e)
{
    if (*i < e && p->t[*i].kind == TK_RBRACE && p->t[*i].start[0] == ']') (*i)++;
}

static Node *parse_foreach(P *p)
{
    uint32_t s = p->i, e = line_end(p, s);
    uint32_t line = p->t[s].line;
    Node *n = node(N_FOREACH, line);
    uint32_t i = s + 1;
    int in;

    if (!word_at(p, i, "each"))
        adda_error_at(p->t[s].start, line, "write it as: for each <name> in <list>");
    i++;
    skip_bracket(p, &i, e);
    if (i >= e || p->t[i].kind != TK_WORD)
        adda_error_at(p->t[s].start, line, "for each needs a name, as in: for each [n] in [nums]");
    if (!p->t[i].bracketed)
        adda_error_at(p->t[i].start, line, "put the name in square brackets: for each [%.*s] in ...",
                      (int)p->t[i].len, p->t[i].start);
    n->name = p->t[i].text;
    i++;
    skip_close(p, &i, e);
    in = find_top(p, i, e, TK_EOF, "in");
    if (in < 0)
        adda_error_at(p->t[s].start, line, "write it as: for each %s in <list>", n->name->bytes);
    if ((uint32_t)in + 1 >= e)
        adda_error_at(p->t[s].start, line, "for each needs something to go through");
    /* Always a value: the thing after `in` names a list or a map, it is never
     * loose text. */
    n->a = parse_value(p, (uint32_t)in + 1, e);
    end_line(p, e);
    p->depth++;
    n->b = parse_block(p, STOP_END, "for each", line);
    p->depth--;
    expect_end(p, "for each", line);
    return n;
}

static Node *parse_define(P *p)
{
    uint32_t s = p->i, e = line_end(p, s);
    uint32_t line = p->t[s].line;
    Node *n = node(N_DEFINE, line);
    uint32_t i = s + 1;

    if (p->depth != 0)
        adda_error_at(p->t[s].start, line,
                      "functions have to be defined on their own, not inside another block");
    if (i >= e || p->t[i].kind != TK_WORD)
        adda_error_at(p->t[s].start, line, "write it as: define <name> with <inputs>");
    n->name = p->t[i].text;
    i++;

    if (i < e) {
        if (!word_at(p, i, "with"))
            adda_error_at(p->t[i].start, p->t[i].line,
                          "write it as: define %s with <inputs>", n->name->bytes);
        i++;
        n->params = adda_alloc(sizeof(Text *) * 16);
        while (i < e) {
            skip_bracket(p, &i, e);
            if (p->t[i].kind != TK_WORD)
                adda_error_at(p->t[i].start, p->t[i].line, "this is not a name I can use");
            if (!p->t[i].bracketed)
                adda_error_at(p->t[i].start, p->t[i].line,
                              "put each input's name in square brackets: [%.*s]",
                              (int)p->t[i].len, p->t[i].start);
            if (n->nparams == 16)
                adda_error_at(p->t[i].start, p->t[i].line, "that is too many inputs for one function");
            n->params[n->nparams++] = p->t[i].text;
            i++;
            skip_close(p, &i, e);
            if (i < e && p->t[i].kind == TK_COMMA) i++;
        }
    }

    end_line(p, e);
    n->b = parse_block(p, STOP_END, "define", line);
    expect_end(p, "define", line);
    return n;
}

static Node *parse_add(P *p, uint32_t s, uint32_t e)
{
    uint32_t line = p->t[s].line;
    Node *n = node(N_ADD, line);
    int to = find_last_top_word(p, s + 1, e, "to");   /* last, so `add go to jail to places` works */

    if (to < 0)
        adda_error_at(p->t[s].start, line, "write it as: add <value> to <list>");
    n->a = parse_run(p, s + 1, (uint32_t)to);
    n->b = parse_value(p, (uint32_t)to + 1, e);
    end_line(p, e);
    return n;
}

static Node *parse_remove(P *p, uint32_t s, uint32_t e)
{
    uint32_t line = p->t[s].line;
    Node *n = node(N_REMOVE, line);

    if (s + 1 >= e)
        adda_error_at(p->t[s].start, line, "write it as: remove item 1 of <list>");
    n->a = parse_value(p, s + 1, e);
    if (n->a->kind != N_INDEX && n->a->kind != N_FIELD)
        adda_error_at(p->t[s].start, line,
                      "remove needs an item or a key, as in: remove item 1 of nums");
    end_line(p, e);
    return n;
}

static Node *parse_assign(P *p, uint32_t s, uint32_t e, uint32_t eq)
{
    uint32_t line = p->t[s].line;
    Node *n = node(N_ASSIGN, line);

    if (eq == s)
        adda_error_at(p->t[s].start, line, "there is nothing to the left of the '='");

    if (eq == s + 1 && p->t[s].kind == TK_WORD && !p->t[s].bracketed)
        adda_error_at(p->t[s].start, line,
                      "to set a variable, put its name in square brackets: [%.*s] = ...",
                      (int)p->t[s].len, p->t[s].start);
    n->a = parse_value(p, s, eq);
    if (n->a->kind != N_VAR && n->a->kind != N_FIELD && n->a->kind != N_INDEX)
        adda_error_at(p->t[s].start, line, "I cannot put a value into that");

    n->b = parse_run(p, eq + 1, e);
    end_line(p, e);
    return n;
}

static Node *statement(P *p)
{
    uint32_t s = p->i;
    uint32_t e = line_end(p, s);
    uint32_t line = at(p, s)->line;
    int eq;

    if (word_at(p, s, "end") || word_at(p, s, "else"))
        adda_error_at(p->t[s].start, line, "there is no open block for this '%.*s'",
                      (int)p->t[s].len, p->t[s].start);

    if (word_at(p, s, "print")) {
        Node *n = node(N_PRINT, line);
        n->a = parse_run(p, s + 1, e);
        end_line(p, e);
        return n;
    }
    if (word_at(p, s, "if"))     return parse_if(p);
    if (word_at(p, s, "while"))  return parse_while(p);
    if (word_at(p, s, "delay") && !word_at(p, s + 1, "end")) return parse_delay(p);

    /* openApplication [title]: a blank window; the program waits for it to close */
    if (word_at(p, s, "openApplication")) {
        Node *n = node(N_OPENAPP, line);
        if (e > s + 1) n->a = parse_run(p, s + 1, e);
        end_line(p, e);
        return n;
    }
    if (word_at(p, s, "delay"))
        adda_error_at(p->t[s].start, line, "there is no open delay for this 'delay end'");
    if (word_at(p, s, "for"))    return parse_foreach(p);
    if (word_at(p, s, "define")) return parse_define(p);
    if (word_at(p, s, "add"))    return parse_add(p, s, e);
    if (word_at(p, s, "remove")) return parse_remove(p, s, e);

    if (word_at(p, s, "return")) {
        Node *n = node(N_RETURN, line);
        if (e > s + 1) n->a = parse_run(p, s + 1, e);
        end_line(p, e);
        return n;
    }

    eq = find_top(p, s, e, TK_ASSIGN, NULL);
    if (eq >= 0) return parse_assign(p, s, e, (uint32_t)eq);

    /* No '=' and no keyword. A call is fine; anything else is almost certainly
     * a forgotten `print`. */
    if (word_at(p, s, "call")) {
        Node *n = node(N_EXPRSTMT, line);
        n->a = parse_value(p, s, e);
        end_line(p, e);
        return n;
    }

    /* In the REPL, a line on its own is something you want to look at. */
    if (p->interactive) {
        Node *n = node(N_EXPRSTMT, line);
        n->a = parse_run(p, s, e);
        end_line(p, e);
        return n;
    }

    adda_error_at(p->t[s].start, line,
                  "I do not know what to do with this line - did you mean 'print %.*s'?",
                  (int)(token_end(&p->t[e - 1]) - p->t[s].start), p->t[s].start);
    return NULL;
}

static Node *parse_block(P *p, int stops, const char *opener, uint32_t opener_line)
{
    Node *blk = node(N_BLOCK, at(p, p->i)->line);

    for (;;) {
        while (p->i < p->n && p->t[p->i].kind == TK_NEWLINE) p->i++;

        if (p->i >= p->n || p->t[p->i].kind == TK_EOF) {
            if (stops)
                adda_error(opener_line,
                           "this '%s' is never closed - add 'end' after its last line",
                           opener);
            return blk;
        }
        if ((stops & STOP_END)  && word_at(p, p->i, "end"))  return blk;
        if ((stops & STOP_DELAY) && word_at(p, p->i, "delay") &&
            word_at(p, p->i + 1, "end")) return blk;
        if ((stops & STOP_ELSE) && word_at(p, p->i, "else")) return blk;

        add_kid(blk, statement(p));
    }
}

Node *parse_mode(TokenList tokens, bool interactive)
{
    P p;

    p.t = tokens.tokens;
    p.n = tokens.count;
    p.i = 0;
    p.in_condition = 0;
    p.depth = 0;
    p.interactive = interactive ? 1 : 0;

    return parse_block(&p, 0, "program", 1);
}

Node *parse(TokenList tokens)
{
    return parse_mode(tokens, false);
}
