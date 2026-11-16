#include "common.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2) { puts("usage: sleep seconds"); return 1; }
    int seconds = atoi(argv[1]);
    if (seconds < 0 || seconds > 60) return 1;
    uint64_t turns = (uint64_t)seconds * 10000;
    for (uint64_t i = 0; i < turns; ++i) sched_yield();
    return 0;
}
