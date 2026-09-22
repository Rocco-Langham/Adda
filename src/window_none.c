/* openApplication where there is no window system Adda knows how to use. */
#include "adda.h"

bool adda_open_window(const char *title) { (void)title; return false; }
bool adda_window_is_open(void) { return false; }
void adda_window_print(const char *text, size_t len) { (void)text; (void)len; }
void adda_window_wait_ms(double ms) { (void)ms; }
void adda_window_run(void) {}
void adda_window_shape(int kind, const double spec[SHAPE_SPEC], const char *name)
{
    (void)kind; (void)spec; (void)name;
}
bool adda_window_shape_text(const char *name, const char *text, const char *font,
                            double size, int location)
{
    (void)name; (void)text; (void)font; (void)size; (void)location;
    return false;
}
const char *adda_window_input(const char *name, const char *hint, const char *question)
{
    (void)name; (void)hint; (void)question;
    return NULL;
}
void adda_window_background(unsigned rgb) { (void)rgb; }
