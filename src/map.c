/* A map with a dense, insertion-ordered entry array plus an open-addressed
 * slot table pointing into it.
 *
 * The extra indirection buys one thing that matters a lot here: `for each key
 * in person` visits keys in the order they were added, which is what anyone
 * writing their first program expects. A plain hash table would hand back an
 * order that looks random. */
#include <string.h>
#include "adda.h"

#define SLOT_EMPTY (-1)

static bool text_same(Text *a, Text *b)
{
    return a == b || (a->hash == b->hash && a->len == b->len &&
                      memcmp(a->bytes, b->bytes, a->len) == 0);
}

/* Rebuild the slot table from the dense entries. Called on growth and after a
 * removal, since removing shifts every later entry index along. */
static void reindex(Map *m, uint32_t index_cap)
{
    uint32_t i;

    m->index_cap = index_cap;
    m->index = adda_alloc(sizeof(int32_t) * index_cap);
    for (i = 0; i < index_cap; i++) m->index[i] = SLOT_EMPTY;

    for (i = 0; i < m->count; i++) {
        uint32_t slot = m->entries[i].key->hash & (index_cap - 1);
        while (m->index[slot] != SLOT_EMPTY) slot = (slot + 1) & (index_cap - 1);
        m->index[slot] = (int32_t)i;
    }
}

static void map_reserve(Map *m, uint32_t need)
{
    uint32_t cap;
    Entry *entries;

    if (need <= m->cap) return;

    cap = m->cap < 8 ? 8 : m->cap;
    while (cap < need) cap *= 2;

    entries = adda_alloc(sizeof(Entry) * cap);
    if (m->count) memcpy(entries, m->entries, sizeof(Entry) * m->count);
    m->entries = entries;
    m->cap = cap;

    /* Keep the slot table under a 0.7 load factor. */
    {
        uint32_t index_cap = 16;
        while (index_cap * 7 / 10 < cap) index_cap *= 2;
        if (index_cap != m->index_cap) reindex(m, index_cap);
    }
}

Map *map_new(void)
{
    Map *m = (Map *)adda_obj_new(sizeof(Map), T_MAP);
    m->count = 0;
    m->cap = 0;
    m->entries = NULL;
    m->index = NULL;
    m->index_cap = 0;
    return m;
}

/* Returns the dense entry index, or -1. */
static int32_t map_find(Map *m, Text *key)
{
    uint32_t slot;

    if (m->count == 0 || m->index_cap == 0) return -1;

    slot = key->hash & (m->index_cap - 1);
    for (;;) {
        int32_t at = m->index[slot];
        if (at == SLOT_EMPTY) return -1;
        if (text_same(m->entries[at].key, key)) return at;
        slot = (slot + 1) & (m->index_cap - 1);
    }
}

bool map_get(Map *m, Text *key, Value *out)
{
    int32_t at = map_find(m, key);
    if (at < 0) return false;
    *out = m->entries[at].val;
    return true;
}

void map_set(Map *m, Text *key, Value v)
{
    int32_t at = map_find(m, key);
    uint32_t slot;

    if (at >= 0) {                 /* existing key keeps its position */
        m->entries[at].val = v;
        return;
    }

    map_reserve(m, m->count + 1);

    m->entries[m->count].key = key;
    m->entries[m->count].val = v;

    slot = key->hash & (m->index_cap - 1);
    while (m->index[slot] != SLOT_EMPTY) slot = (slot + 1) & (m->index_cap - 1);
    m->index[slot] = (int32_t)m->count;

    m->count++;
}

bool map_remove(Map *m, Text *key)
{
    int32_t at = map_find(m, key);
    if (at < 0) return false;

    memmove(m->entries + at, m->entries + at + 1,
            sizeof(Entry) * (m->count - (uint32_t)at - 1));
    m->count--;
    reindex(m, m->index_cap);      /* indices shifted, so rebuild */
    return true;
}
