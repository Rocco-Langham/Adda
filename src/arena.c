/* Bump allocator. Everything Adda allocates lives here and is released in one
 * go at exit, which is what lets error handling be a plain longjmp: there is
 * never anything to unwind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adda.h"

#define CHUNK_SIZE  (64 * 1024)
#define LARGE_ALLOC (32 * 1024)
#define ALIGNMENT   16

typedef struct Chunk {
    struct Chunk *next;
    size_t        used, cap;
    char         *bytes;
} Chunk;

static Chunk *chunks;
static size_t total_used;

static Chunk *chunk_new(size_t cap)
{
    Chunk *c = malloc(sizeof(Chunk));
    if (!c) { fputs("adda: out of memory\n", stderr); exit(70); }
    c->bytes = malloc(cap);
    if (!c->bytes) { fputs("adda: out of memory\n", stderr); exit(70); }
#ifdef ADDA_DEBUG
    memset(c->bytes, 0xDD, cap);   /* poison, so use-before-init is loud */
#endif
    c->used = 0;
    c->cap  = cap;
    c->next = chunks;
    chunks  = c;
    return c;
}

void arena_init(void)
{
    chunks = NULL;
    total_used = 0;
    chunk_new(CHUNK_SIZE);
}

void *adda_alloc(size_t size)
{
    size_t aligned = (size + (ALIGNMENT - 1)) & ~(size_t)(ALIGNMENT - 1);
    Chunk *c;

    total_used += aligned;

    /* A big request gets its own chunk rather than wasting the current one. */
    if (aligned >= LARGE_ALLOC) {
        c = chunk_new(aligned);
        c->used = aligned;
        return c->bytes;
    }

    c = chunks;
    if (c->used + aligned > c->cap)
        c = chunk_new(CHUNK_SIZE);

    {
        void *p = c->bytes + c->used;
        c->used += aligned;
        memset(p, 0, aligned);
        return p;
    }
}

void arena_free_all(void)
{
    Chunk *c = chunks;
    while (c) {
        Chunk *next = c->next;
        free(c->bytes);
        free(c);
        c = next;
    }
    chunks = NULL;
}

size_t arena_bytes_used(void) { return total_used; }
