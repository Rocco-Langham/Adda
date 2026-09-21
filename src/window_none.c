/* openApplication where there is no window system Adda knows how to use. */
#include "adda.h"

bool adda_open_window(const char *title)
{
    (void)title;
    return false;
}
