/* Values: text, lists, and the helpers every other file leans on.
 *
 * Text is immutable. Growth of any array is by doubling, never by one, because
 * an arena cannot realloc - each growth abandons the old block. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "adda.h"

/* Every object is threaded onto this list as it is born. Nothing walks it yet;
 * it is here so a mark-sweep collector can be added later without touching any
 * of the allocation sites. */
static Obj *all_objects;

Obj *adda_obj_new(size_t size, Type type)
{
    Obj *o = adda_alloc(size);
    o->type = (uint8_t)type;
    o->mark = 0;
    o->next = all_objects;
    all_objects = o;
    return o;
}

Value nothing_value(void)       { Value v; v.type = T_NOTHING; v.as.obj = NULL;  return v; }
Value bool_value(bool b)        { Value v; v.type = T_BOOL;    v.as.b   = b;     return v; }
Value number_value(double n)    { Value v; v.type = T_NUMBER;  v.as.num = n;     return v; }
Value text_value(Text *t)       { Value v; v.type = T_TEXT;    v.as.obj = (Obj *)t; return v; }
Value obj_value(Type t, Obj *o) { Value v; v.type = (uint8_t)t; v.as.obj = o;    return v; }

const char *type_name(Type t)
{
    switch (t) {
        case T_NOTHING: return "nothing";
        case T_BOOL:    return "a true/false value";
        case T_NUMBER:  return "a number";
        case T_TEXT:    return "text";
        case T_LIST:    return "a list";
        case T_MAP:     return "a map";
        case T_FUNC:    return "a function";
    }
    return "something";
}

/* ------------------------------------------------------------------- text */

static uint32_t hash_bytes(const char *b, size_t len)
{
    uint32_t h = 2166136261u;            /* FNV-1a */
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= (uint8_t)b[i];
        h *= 16777619u;
    }
    return h;
}

Text *text_new(const char *bytes, size_t len)
{
    Text *t = (Text *)adda_obj_new(sizeof(Text) + len, T_TEXT);
    t->len = (uint32_t)len;
    if (len) memcpy(t->bytes, bytes, len);
    t->bytes[len] = 0;
    t->hash = hash_bytes(t->bytes, len);
    return t;
}

Text *text_from_cstr(const char *s) { return text_new(s, strlen(s)); }

Text *text_concat(Text *a, Text *b)
{
    Text *t = (Text *)adda_obj_new(sizeof(Text) + a->len + b->len, T_TEXT);
    t->len = a->len + b->len;
    memcpy(t->bytes, a->bytes, a->len);
    memcpy(t->bytes + a->len, b->bytes, b->len);
    t->bytes[t->len] = 0;
    t->hash = hash_bytes(t->bytes, t->len);
    return t;
}

/* Identifiers, keywords and map keys written in the source are interned, so
 * variable lookup is a pointer comparison. Runtime-built text is deliberately
 * NOT interned: that table would otherwise keep every string ever made alive,
 * which is exactly the kind of root a collector cannot work around. */
static Text   **intern_tab;
static uint32_t intern_cap, intern_count;

static void intern_grow(void)
{
    uint32_t new_cap = intern_cap < 64 ? 64 : intern_cap * 2;
    Text   **new_tab = adda_alloc(sizeof(Text *) * new_cap);
    uint32_t i;

    for (i = 0; i < intern_cap; i++) {
        Text *t = intern_tab[i];
        if (t) {
            uint32_t slot = t->hash & (new_cap - 1);
            while (new_tab[slot]) slot = (slot + 1) & (new_cap - 1);
            new_tab[slot] = t;
        }
    }
    intern_tab = new_tab;
    intern_cap = new_cap;
}

Text *intern(const char *bytes, size_t len)
{
    uint32_t hash, slot;

    if (intern_count + 1 > intern_cap - intern_cap / 4) intern_grow();

    hash = hash_bytes(bytes, len);
    slot = hash & (intern_cap - 1);
    for (;;) {
        Text *t = intern_tab[slot];
        if (!t) break;
        if (t->hash == hash && t->len == len && memcmp(t->bytes, bytes, len) == 0)
            return t;
        slot = (slot + 1) & (intern_cap - 1);
    }
    intern_tab[slot] = text_new(bytes, len);
    intern_count++;
    return intern_tab[slot];
}

/* ------------------------------------------------------------------- list */

List *list_new(void)
{
    List *l = (List *)adda_obj_new(sizeof(List), T_LIST);
    l->len = 0;
    l->cap = 0;
    l->items = NULL;
    return l;
}

static void list_reserve(List *l, uint32_t need)
{
    uint32_t cap;
    Value *items;

    if (need <= l->cap) return;
    cap = l->cap < 8 ? 8 : l->cap;
    while (cap < need) cap *= 2;
    items = adda_alloc(sizeof(Value) * cap);
    if (l->len) memcpy(items, l->items, sizeof(Value) * l->len);
    l->items = items;
    l->cap = cap;
}

void list_push(List *l, Value v)
{
    list_reserve(l, l->len + 1);
    l->items[l->len++] = v;
}

void list_insert(List *l, uint32_t at, Value v)
{
    list_reserve(l, l->len + 1);
    memmove(l->items + at + 1, l->items + at, sizeof(Value) * (l->len - at));
    l->items[at] = v;
    l->len++;
}

void list_remove(List *l, uint32_t at)
{
    memmove(l->items + at, l->items + at + 1, sizeof(Value) * (l->len - at - 1));
    l->len--;
}

/* -------------------------------------------------------------- temp roots */

static Value temps[256];
static int   temps_top;

void temp_push(Value v) { if (temps_top < 256) temps[temps_top++] = v; }
void temp_pop(int n)    { temps_top -= n; if (temps_top < 0) temps_top = 0; }

/* ---------------------------------------------------------------- printing */

/* %.15g rather than %.17g, so 0.1 + 0.2 prints as 0.3 instead of exposing the
 * float noise underneath it. */
static void number_to_buf(double n, char *buf, size_t size)
{
    if (n == 0) { snprintf(buf, size, "0"); return; }
    if (n == floor(n) && fabs(n) < 1e15)
        snprintf(buf, size, "%lld", (long long)n);
    else
        snprintf(buf, size, "%.15g", n);
}

Text *value_to_text(Value v)
{
    char buf[64];

    switch (v.type) {
        case T_NOTHING: return text_from_cstr("nothing");
        case T_BOOL:    return text_from_cstr(v.as.b ? "true" : "false");
        case T_NUMBER:
            number_to_buf(v.as.num, buf, sizeof buf);
            return text_from_cstr(buf);
        case T_TEXT:    return AS_TEXT(v);
        case T_LIST: {
            List *l = AS_LIST(v);
            Text *out = text_from_cstr("[");
            uint32_t i;
            PUSH_TEMP(v);
            for (i = 0; i < l->len; i++) {
                if (i) out = text_concat(out, text_from_cstr(", "));
                out = text_concat(out, value_to_text(l->items[i]));
            }
            POP_TEMP(1);
            return text_concat(out, text_from_cstr("]"));
        }
        case T_MAP: {
            Map *m = AS_MAP(v);
            Text *out = text_from_cstr("{");
            uint32_t i;
            PUSH_TEMP(v);
            for (i = 0; i < m->count; i++) {
                if (i) out = text_concat(out, text_from_cstr(", "));
                out = text_concat(out, m->entries[i].key);
                out = text_concat(out, text_from_cstr(": "));
                out = text_concat(out, value_to_text(m->entries[i].val));
            }
            POP_TEMP(1);
            return text_concat(out, text_from_cstr("}"));
        }
        case T_FUNC:
            snprintf(buf, sizeof buf, "<function %s>", AS_FUNC(v)->name->bytes);
            return text_from_cstr(buf);
    }
    return text_from_cstr("?");
}

void value_print(Value v)
{
    Text *t = value_to_text(v);
    fwrite(t->bytes, 1, t->len, stdout);
}

/* Text and numbers compare by how they print, so `if age is 30` works whether
 * age arrived from maths or from a bare word. Everything else is strict. */
bool value_equal(Value a, Value b)
{
    if (a.type == b.type) {
        switch (a.type) {
            case T_NOTHING: return true;
            case T_BOOL:    return a.as.b == b.as.b;
            case T_NUMBER:  return a.as.num == b.as.num;
            case T_TEXT: {
                Text *x = AS_TEXT(a), *y = AS_TEXT(b);
                return x == y || (x->hash == y->hash && x->len == y->len &&
                                  memcmp(x->bytes, y->bytes, x->len) == 0);
            }
            default: return a.as.obj == b.as.obj;
        }
    }
    if ((a.type == T_TEXT || a.type == T_NUMBER) &&
        (b.type == T_TEXT || b.type == T_NUMBER)) {
        Text *x = value_to_text(a), *y = value_to_text(b);
        return x->len == y->len && memcmp(x->bytes, y->bytes, x->len) == 0;
    }
    return false;
}
