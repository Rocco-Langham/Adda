/* Colouring the code in the editor, shared by both GUIs. It only says which
 * bytes are what; each GUI picks the colours from its theme and draws them.
 *
 * Adda lets text go unquoted, so a word is only coloured as a keyword where
 * it really acts as one: `and` in `if [a] and [b]`, not in `print Tom and Jo`.
 * Text is lines ending in \n (a \r before it is ignored). */
#ifndef ADDA_COLOUR_H
#define ADDA_COLOUR_H

#include <stddef.h>

typedef enum {
    COL_KEYWORD,       /* print, if, while, end, insert, and ask/list/... starting a value */
    COL_CONSTANT,      /* true, false, nothing */
    COL_VARIABLE,      /* [name] */
    COL_NUMBER,        /* 30, 2.5, 15px */
    COL_STRING,        /* "quoted text" */
    COL_COMMENT,       /* # to the end of the line */
    COL_PUNCT,         /* { } ( ) = , ; */
    COL_ARROW,         /* the tidy view's arrows, -> and the long one: drawn bold */
    COL_KINDS
} ColourKind;

typedef struct {
    size_t     start, len;   /* bytes */
    ColourKind kind;
} ColourSpan;

/* Fills `out` with the coloured pieces of `text`, in order, and returns how
 * many (at most max). Anything not listed is ordinary text. */
int colour_spans(const char *text, ColourSpan *out, int max);

#endif
