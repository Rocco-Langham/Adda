/* openApplication where there is no window system Adda knows how to use. */
#include "adda.h"

bool adda_open_window(const char *title) { (void)title; return false; }
bool adda_window_is_open(void) { return false; }
void adda_window_print(const char *text, size_t len) { (void)text; (void)len; }
void adda_window_wait_ms(double ms) { (void)ms; }
void adda_window_run(void) {}
