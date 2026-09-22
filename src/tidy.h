/* The tidy view of named shape blocks and delay blocks, shared by both GUIs.
 *
 *   insert rounded box; name = [box1]        insert rounded box; name ——> box1
 *   15px top,right,left                  =>  15px ——> top, right, left
 *   Height = 15px                            Height ——> 15px
 *   text [box1] Hello; font impact; size15   text [box1] --> Hello --> font impact --> size 15
 *   end                                      end
 *
 *   delay - 500                              delay ——> 500
 *       print hello                      =>  └─>print hello
 *   end                                      end
 *
 * It is only ever a way of SHOWING the code: files are always saved raw, and
 * whatever is run or checked is turned back to raw first. The long arrow and
 * the branch (└─>) are passed in, because each GUI draws text differently.
 * A delay block is only tidied when nothing inside it opens a block of its
 * own, and it stays tidy with the caret in it - only the caret's own line is
 * left as typed. Turned back to raw, its lines are indented four spaces.
 *
 * Text is lines ending in \n (a \r before it is kept, for the Windows EDIT). */
#ifndef ADDA_TIDY_H
#define ADDA_TIDY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t start, len;     /* the bytes to replace */
    char  *with;           /* what to put there instead (malloc'd) */
} TidyEdit;

/* The changes that bring `text` up to date. With `on`, every finished raw
 * shape the caret is not in is tidied, and a tidy shape the caret has moved
 * into is turned back to raw; every finished delay is tidied but for the
 * caret's line. Without it, everything tidy goes back to raw.
 * `caretLine` is 0-based, or -1 for none. The caret is "in" a block on any of
 * its lines but the closing end. Edits come in order and never overlap, so
 * apply them from the last to the first. Returns how many (at most max). */
int  tidy_edits(const char *text, const char *arrow, const char *branch, long caretLine,
                bool on, TidyEdit *out, int max);
void tidy_free_edits(TidyEdit *edits, int n);

/* The whole of `text` with every tidy block turned back to raw (malloc'd). */
char *tidy_raw(const char *text, const char *arrow, const char *branch);

#endif
