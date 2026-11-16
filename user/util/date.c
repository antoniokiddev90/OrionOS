#include "common.h"
int main(void) {
    if (util_print_file("/proc/uptime") != 0) { puts("date: clock unavailable"); return 1; }
    return 0;
}
