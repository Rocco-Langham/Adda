/* The tree-walking interpreter.
 *
 * Scopes are flat arrays searched by pointer comparison: source identifiers are
 * interned, and a scope holds a handful of names, so a linear scan beats
 * hashing and costs a fraction of the code. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "adda.h"

typedef struct Scope {
    struct Scope *parent;
    Text        **names;
    Value        *vals;
    uint32_t      count, cap;
} Scope;

typedef enum { FLOW_NORMAL, FLOW_RETURN } Flow;

static Scope *globals;

static Flow  exec(Node *n, Scope *sc, Value *ret);
static Value eval(Node *n, Scope *sc);

/* ------------------------------------------------------------------ scopes */

static Scope *scope_new(Scope *parent)
{
    Scope *s = adda_alloc(sizeof(Scope));
    s->parent = parent;
    return s;
}

static Value *scope_find(Scope *s, Text *name)
{
    for (; s; s = s->parent) {
        uint32_t i;
        for (i = 0; i < s->count; i++)
            if (s->names[i] == name) return &s->vals[i];
    }
    return NULL;
}

static void scope_define(Scope *s, Text *name, Value v)
{
    uint32_t i;

    for (i = 0; i < s->count; i++)
        if (s->names[i] == name) { s->vals[i] = v; return; }

    if (s->count + 1 > s->cap) {
        uint32_t cap = s->cap < 8 ? 8 : s->cap * 2;
        Text **names = adda_alloc(sizeof(Text *) * cap);
        Value *vals  = adda_alloc(sizeof(Value) * cap);
        if (s->count) {
            memcpy(names, s->names, sizeof(Text *) * s->count);
            memcpy(vals,  s->vals,  sizeof(Value) * s->count);
        }
        s->names = names;
        s->vals = vals;
        s->cap = cap;
    }
    s->names[s->count] = name;
    s->vals[s->count] = v;
    s->count++;
}

/* Assignment updates an existing name wherever it lives, so `total = total + n`
 * inside a loop keeps working on the same variable. */
static void scope_set(Scope *s, Text *name, Value v)
{
    Value *slot = scope_find(s, name);
    if (slot) *slot = v;
    else scope_define(s, name, v);
}

/* ------------------------------------------------- "did you mean ...?" */

static uint32_t edit_distance(const char *a, uint32_t alen, const char *b, uint32_t blen)
{
    uint32_t row[64], i, j;

    if (alen > 62 || blen > 62) return 99;
    for (j = 0; j <= blen; j++) row[j] = j;

    for (i = 1; i <= alen; i++) {
        uint32_t prev = row[0];
        row[0] = i;
        for (j = 1; j <= blen; j++) {
            uint32_t cur = row[j];
            uint32_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            uint32_t best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            if (prev + cost < best)    best = prev + cost;
            row[j] = best;
            prev = cur;
        }
    }
    return row[blen];
}

static Text *nearest_name(Scope *s, Text *name)
{
    Text *best = NULL;
    uint32_t best_d = 3;                 /* only suggest close misses */

    for (; s; s = s->parent) {
        uint32_t i;
        for (i = 0; i < s->count; i++) {
            Text *c = s->names[i];
            uint32_t d = edit_distance(name->bytes, name->len, c->bytes, c->len);
            if (d < best_d) { best_d = d; best = c; }
        }
    }
    return best;
}

/* ------------------------------------------------------------------ helpers */

static double as_number(Value v, uint32_t line, const char *what)
{
    if (!IS_NUMBER(v))
        adda_error(line, "%s needs a number, but got %s", what, type_name(v.type));
    return v.as.num;
}

/* Conditions must be true or false. `if count` is a mistake worth catching. */
static bool as_condition(Value v, uint32_t line)
{
    if (!IS_BOOL(v)) {
        if (IS_TEXT(v))
            adda_hint("cond-text",
                      "a test has to be true or false. If you meant maths, "
                      "put spaces around it: x > 5, not x>5");
        adda_error(line, "this test gave %s, but a test has to be true or false",
                   type_name(v.type));
    }
    return v.as.b;
}

static Value call_function(Func *fn, Value *args, uint8_t nargs, uint32_t line)
{
    Scope *sc;
    Value ret = nothing_value();
    uint8_t i;

    if (nargs != fn->nparams)
        adda_error(line, "'%s' needs %u input%s, but got %u",
                   fn->name->bytes, fn->nparams,
                   fn->nparams == 1 ? "" : "s", nargs);

    sc = scope_new(globals);              /* no closures: functions see globals */
    for (i = 0; i < nargs; i++) scope_define(sc, fn->params[i], args[i]);

    exec(fn->body, sc, &ret);
    return ret;
}

/* ---------------------------------------------------------------- evaluate */

static Value eval_binary(Node *n, Scope *sc)
{
    Value a = eval(n->a, sc);
    Value b;

    PUSH_TEMP(a);
    b = eval(n->b, sc);
    POP_TEMP(1);

    switch (n->op) {
        case TK_ASSIGN: return bool_value(value_equal(a, b));
        case TK_NE:     return bool_value(!value_equal(a, b));

        case TK_PLUS:
            if (IS_TEXT(a) || IS_TEXT(b)) {
                adda_hint("join-text",
                          "to join text, put the pieces in braces: {first} {last}");
                adda_error(n->line, "'+' adds numbers, and one of these is text");
            }
            return number_value(as_number(a, n->line, "+") + as_number(b, n->line, "+"));

        case TK_MINUS:
            return number_value(as_number(a, n->line, "-") - as_number(b, n->line, "-"));
        case TK_STAR:
            return number_value(as_number(a, n->line, "*") * as_number(b, n->line, "*"));
        case TK_SLASH: {
            double d = as_number(b, n->line, "/");
            if (d == 0) adda_error(n->line, "I cannot divide by zero");
            return number_value(as_number(a, n->line, "/") / d);
        }
        case TK_PERCENT: {
            double d = as_number(b, n->line, "%");
            if (d == 0) adda_error(n->line, "I cannot divide by zero");
            return number_value(fmod(as_number(a, n->line, "%"), d));
        }
        default: break;
    }

    /* < > <= >= : numbers compare as numbers, text compares alphabetically */
    {
        int cmp;

        if (IS_NUMBER(a) && IS_NUMBER(b)) {
            cmp = a.as.num < b.as.num ? -1 : a.as.num > b.as.num ? 1 : 0;
        } else if (IS_TEXT(a) && IS_TEXT(b)) {
            Text *x = AS_TEXT(a), *y = AS_TEXT(b);
            uint32_t len = x->len < y->len ? x->len : y->len;
            cmp = memcmp(x->bytes, y->bytes, len);
            if (cmp == 0) cmp = x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
        } else {
            adda_hint("cmp-types",
                      "if you meant maths, remember Adda needs spaces around it: x > 5");
            adda_error(n->line, "I cannot compare %s with %s",
                       type_name(a.type), type_name(b.type));
            return nothing_value();
        }

        switch (n->op) {
            case TK_LT: return bool_value(cmp <  0);
            case TK_GT: return bool_value(cmp >  0);
            case TK_LE: return bool_value(cmp <= 0);
            case TK_GE: return bool_value(cmp >= 0);
            default: break;
        }
    }
    adda_error(n->line, "I do not understand this operator");
    return nothing_value();
}

/* Reads one whole line, however long, with the ends trimmed. NULL at end of
 * input - a program reading from a file that has run out, or from the GUI,
 * which gives its child no input at all. */
static Text *read_user_line(void)
{
    char buf[512];
    char *acc = NULL;
    size_t len = 0, cap = 0;
    bool got = false;

    for (;;) {
        size_t n = 0;

        if (!fgets(buf, sizeof buf, stdin)) break;
        got = true;
        n = strlen(buf);

        if (len + n + 1 > cap) {
            size_t want = cap < 256 ? 256 : cap;
            char *grown;
            while (want < len + n + 1) want *= 2;
            grown = adda_alloc(want);
            if (len) memcpy(grown, acc, len);
            acc = grown;
            cap = want;
        }
        memcpy(acc + len, buf, n);
        len += n;
        acc[len] = 0;

        if (n > 0 && buf[n - 1] == '\n') break;
    }

    if (!got) return NULL;

    {
        size_t start = 0;
        while (start < len && (acc[start] == ' '  || acc[start] == '\t' ||
                               acc[start] == '\r' || acc[start] == '\n')) start++;
        while (len > start && (acc[len - 1] == ' '  || acc[len - 1] == '\t' ||
                               acc[len - 1] == '\r' || acc[len - 1] == '\n')) len--;
        return text_new(acc + start, len - start);
    }
}

static Value eval_ask(Node *n, Scope *sc)
{
    Text *answer;
    double number;

    if (n->a) {
        value_print(eval(n->a, sc));
        fputc(' ', stdout);
    }
    fflush(stdout);              /* the prompt has to appear before we wait */

    answer = read_user_line();
    if (!answer) return nothing_value();

    /* An answer becomes a number under exactly the rule source literals use,
     * so 30 is a number you can add to, while 007 keeps its zeros. */
    if (adda_number_from_text(answer->bytes, answer->len, &number))
        return number_value(number);

    return text_value(answer);
}

static Value eval_index(Node *n, Scope *sc)
{
    Value container = eval(n->b, sc);
    Value key;

    PUSH_TEMP(container);
    key = eval(n->a, sc);
    POP_TEMP(1);

    if (IS_LIST(container)) {
        List *l = AS_LIST(container);
        double d = as_number(key, n->line, "item");
        long pos = (long)d;

        if (d != floor(d))
            adda_error(n->line, "item needs a whole number, not %g", d);
        if (pos == 0)
            adda_error(n->line, "there is no item 0 - Adda counts from 1");
        if (pos < 1 || (uint32_t)pos > l->len)
            adda_error(n->line, "there is no item %ld - this list has %u",
                       pos, l->len);
        return l->items[pos - 1];
    }
    if (IS_MAP(container)) {
        Value out;
        Text *k = value_to_text(key);
        if (!map_get(AS_MAP(container), k, &out))
            adda_error(n->line, "this map has no '%s' in it", k->bytes);
        return out;
    }
    adda_error(n->line, "item needs a list or a map, but got %s", type_name(container.type));
    return nothing_value();
}

static Value eval(Node *n, Scope *sc)
{
    switch (n->kind) {
        case N_NUMBER:  return number_value(n->number);
        case N_TEXT:    return text_value(n->name);
        case N_BOOL:    return bool_value(n->flag);
        case N_NOTHING: return nothing_value();

        case N_VAR: {
            Value *slot = scope_find(sc, n->name);
            if (slot) return *slot;
            /* Inside a condition an unknown word is simply its own text, so
             * `if name is Rocco` reads the way it looks. */
            if (n->flag) return text_value(n->name);
            {
                Text *near = nearest_name(sc, n->name);
                if (near)
                    adda_error(n->line, "'%s' is not defined - did you mean '%s'?",
                               n->name->bytes, near->bytes);
                adda_error(n->line, "'%s' is not defined", n->name->bytes);
            }
            return nothing_value();
        }

        case N_TEMPLATE: {
            Text *out = text_from_cstr("");
            uint32_t i;
            for (i = 0; i < n->nkids; i++) {
                Value part = eval(n->kids[i], sc);
                PUSH_TEMP(part);
                out = text_concat(out, value_to_text(part));
                POP_TEMP(1);
            }
            return text_value(out);
        }

        case N_BINARY: return eval_binary(n, sc);

        case N_UNARY:
            if (n->op == TK_MINUS)
                return number_value(-as_number(eval(n->a, sc), n->line, "-"));
            return bool_value(!as_condition(eval(n->a, sc), n->line));

        case N_AND: {
            Value a = eval(n->a, sc);
            if (!as_condition(a, n->line)) return bool_value(false);
            return bool_value(as_condition(eval(n->b, sc), n->line));
        }
        case N_OR: {
            Value a = eval(n->a, sc);
            if (as_condition(a, n->line)) return bool_value(true);
            return bool_value(as_condition(eval(n->b, sc), n->line));
        }

        case N_LIST: {
            List *l = list_new();
            Value lv = obj_value(T_LIST, (Obj *)l);
            uint32_t i;
            PUSH_TEMP(lv);
            for (i = 0; i < n->nkids; i++) list_push(l, eval(n->kids[i], sc));
            POP_TEMP(1);
            return lv;
        }

        case N_MAP: return obj_value(T_MAP, (Obj *)map_new());

        case N_INDEX: return eval_index(n, sc);

        case N_FIELD: {
            Value container = eval(n->a, sc);
            Value out;
            if (!IS_MAP(container))
                adda_error(n->line, "'%s of ...' needs a map, but got %s",
                           n->name->bytes, type_name(container.type));
            if (!map_get(AS_MAP(container), n->name, &out))
                adda_error(n->line, "this map has no '%s' in it", n->name->bytes);
            return out;
        }

        case N_LENGTH: {
            Value v = eval(n->a, sc);
            if (IS_LIST(v)) return number_value(AS_LIST(v)->len);
            if (IS_MAP(v))  return number_value(AS_MAP(v)->count);
            if (IS_TEXT(v)) return number_value(AS_TEXT(v)->len);
            adda_error(n->line, "length needs a list, a map or text, but got %s",
                       type_name(v.type));
            return nothing_value();
        }

        case N_ASK: return eval_ask(n, sc);

        case N_HAS: {
            Value container = eval(n->b, sc);
            Value key;

            PUSH_TEMP(container);
            key = eval(n->a, sc);
            POP_TEMP(1);

            if (IS_MAP(container)) {
                Value ignored;
                return bool_value(map_get(AS_MAP(container), value_to_text(key), &ignored));
            }
            if (IS_LIST(container)) {
                List *l = AS_LIST(container);
                uint32_t i;
                for (i = 0; i < l->len; i++)
                    if (value_equal(l->items[i], key)) return bool_value(true);
                return bool_value(false);
            }
            adda_error(n->line, "has needs a list or a map, but got %s",
                       type_name(container.type));
            return nothing_value();
        }

        case N_CALL: {
            Value *slot = scope_find(globals, n->name);
            Value args[16];
            uint32_t i;

            if (!slot || !IS_FUNC(*slot))
                adda_error(n->line, "there is no function called '%s'", n->name->bytes);
            if (n->nkids > 16)
                adda_error(n->line, "that is too many inputs for one call");

            for (i = 0; i < n->nkids; i++) {
                args[i] = eval(n->kids[i], sc);
                PUSH_TEMP(args[i]);
            }
            {
                Value out = call_function(AS_FUNC(*slot), args, (uint8_t)n->nkids, n->line);
                POP_TEMP((int)n->nkids);
                return out;
            }
        }

        default: break;
    }
    adda_error(n->line, "I do not know how to work this out");
    return nothing_value();
}

/* ---------------------------------------------------------------- execute */

static void assign_to(Node *target, Value v, Scope *sc)
{
    switch (target->kind) {
        case N_VAR:
            scope_set(sc, target->name, v);
            return;

        case N_FIELD: {
            Value container = eval(target->a, sc);
            if (!IS_MAP(container))
                adda_error(target->line, "'%s of ...' needs a map, but got %s",
                           target->name->bytes, type_name(container.type));
            map_set(AS_MAP(container), target->name, v);
            return;
        }

        case N_INDEX: {
            Value container = eval(target->b, sc);
            Value key = eval(target->a, sc);

            if (IS_LIST(container)) {
                List *l = AS_LIST(container);
                double d = as_number(key, target->line, "item");
                long pos = (long)d;
                if (pos == 0)
                    adda_error(target->line, "there is no item 0 - Adda counts from 1");
                if (pos < 1 || (uint32_t)pos > l->len)
                    adda_error(target->line, "there is no item %ld - this list has %u",
                               pos, l->len);
                l->items[pos - 1] = v;
                return;
            }
            if (IS_MAP(container)) {
                map_set(AS_MAP(container), value_to_text(key), v);
                return;
            }
            adda_error(target->line, "item needs a list or a map, but got %s",
                       type_name(container.type));
            return;
        }

        default:
            adda_error(target->line, "I cannot put a value into that");
    }
}

static void do_print(Node *n, Scope *sc)
{
    Value v = eval(n->a, sc);

    /* n->flag marks a REPL echo rather than a real `print`. Calling a function
     * that returns nothing should not spray "nothing" down the screen. */
    if (n->flag && IS_NOTHING(v)) return;

    value_print(v);
    fputc('\n', stdout);

    /* Flush every line. Into a pipe stdout is block-buffered, so without this
     * a GUI or a log sees nothing until the program ends, and output arrives
     * out of order against the unbuffered error stream. */
    fflush(stdout);
}

static Flow exec(Node *n, Scope *sc, Value *ret)
{
    switch (n->kind) {
        case N_BLOCK: {
            uint32_t i;
            for (i = 0; i < n->nkids; i++) {
                Flow f = exec(n->kids[i], sc, ret);
                if (f != FLOW_NORMAL) return f;
            }
            return FLOW_NORMAL;
        }

        case N_ASSIGN:
            assign_to(n->a, eval(n->b, sc), sc);
            return FLOW_NORMAL;

        case N_PRINT:
            do_print(n, sc);
            return FLOW_NORMAL;

        case N_IF:
            if (as_condition(eval(n->a, sc), n->line)) return exec(n->b, sc, ret);
            if (n->c) return exec(n->c, sc, ret);
            return FLOW_NORMAL;

        case N_WHILE:
            while (as_condition(eval(n->a, sc), n->line)) {
                Flow f = exec(n->b, sc, ret);
                if (f != FLOW_NORMAL) return f;
            }
            return FLOW_NORMAL;

        case N_FOREACH: {
            Value seq = eval(n->a, sc);
            Scope *body = scope_new(sc);
            uint32_t i, len;

            if (IS_LIST(seq)) {
                /* Re-read items each time round: the body may append, and the
                 * arena hands back a new block when a list grows. */
                len = AS_LIST(seq)->len;
                for (i = 0; i < len && i < AS_LIST(seq)->len; i++) {
                    Flow f;
                    scope_define(body, n->name, AS_LIST(seq)->items[i]);
                    f = exec(n->b, body, ret);
                    if (f != FLOW_NORMAL) return f;
                }
                return FLOW_NORMAL;
            }
            if (IS_MAP(seq)) {
                len = AS_MAP(seq)->count;
                for (i = 0; i < len && i < AS_MAP(seq)->count; i++) {
                    Flow f;
                    scope_define(body, n->name, text_value(AS_MAP(seq)->entries[i].key));
                    f = exec(n->b, body, ret);
                    if (f != FLOW_NORMAL) return f;
                }
                return FLOW_NORMAL;
            }
            adda_error(n->line, "for each needs a list or a map, but got %s",
                       type_name(seq.type));
            return FLOW_NORMAL;
        }

        case N_DEFINE:
            return FLOW_NORMAL;            /* hoisted before the program runs */

        case N_RETURN:
            *ret = n->a ? eval(n->a, sc) : nothing_value();
            return FLOW_RETURN;

        case N_ADD: {
            Value v = eval(n->a, sc);
            Value target;
            PUSH_TEMP(v);
            target = eval(n->b, sc);
            POP_TEMP(1);
            if (!IS_LIST(target))
                adda_error(n->line, "add needs a list, but got %s", type_name(target.type));
            list_push(AS_LIST(target), v);
            return FLOW_NORMAL;
        }

        case N_REMOVE: {
            Node *t = n->a;
            if (t->kind == N_INDEX) {
                Value container = eval(t->b, sc);
                Value key = eval(t->a, sc);
                if (IS_LIST(container)) {
                    List *l = AS_LIST(container);
                    long pos = (long)as_number(key, n->line, "item");
                    if (pos == 0)
                        adda_error(n->line, "there is no item 0 - Adda counts from 1");
                    if (pos < 1 || (uint32_t)pos > l->len)
                        adda_error(n->line, "there is no item %ld - this list has %u",
                                   pos, l->len);
                    list_remove(l, (uint32_t)pos - 1);
                    return FLOW_NORMAL;
                }
                if (IS_MAP(container)) {
                    map_remove(AS_MAP(container), value_to_text(key));
                    return FLOW_NORMAL;
                }
                adda_error(n->line, "remove needs a list or a map");
            } else {
                Value container = eval(t->a, sc);
                if (!IS_MAP(container))
                    adda_error(n->line, "'%s of ...' needs a map", t->name->bytes);
                if (!map_remove(AS_MAP(container), t->name))
                    adda_error(n->line, "this map has no '%s' in it", t->name->bytes);
            }
            return FLOW_NORMAL;
        }

        case N_EXPRSTMT:
            eval(n->a, sc);
            return FLOW_NORMAL;

        default:
            eval(n, sc);
            return FLOW_NORMAL;
    }
}

/* Functions are registered before anything runs, so a program can call a
 * function that is defined further down the file. */
static void hoist_functions(Node *block)
{
    uint32_t i;

    for (i = 0; i < block->nkids; i++) {
        Node *n = block->kids[i];
        if (n->kind == N_DEFINE) {
            Func *fn = (Func *)adda_obj_new(sizeof(Func), T_FUNC);
            fn->name = n->name;
            fn->params = n->params;
            fn->nparams = n->nparams;
            fn->body = n->b;
            scope_define(globals, n->name, obj_value(T_FUNC, (Obj *)fn));
        }
    }
}

void interp_init(void)
{
    globals = scope_new(NULL);
}

void interp_run(Node *program)
{
    Value ret = nothing_value();

    hoist_functions(program);
    exec(program, globals, &ret);
}

void interpret(Node *program)
{
    interp_init();
    interp_run(program);
}

/* For the REPL's :vars command. */
void interp_show_variables(void)
{
    uint32_t i;

    if (globals->count == 0) {
        puts("nothing is defined yet");
        return;
    }
    for (i = 0; i < globals->count; i++) {
        printf("%s = ", globals->names[i]->bytes);
        value_print(globals->vals[i]);
        putchar('\n');
    }
}
