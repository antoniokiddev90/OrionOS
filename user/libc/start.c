#include <stdint.h>
#include <stdlib.h>
int main(int argc, char **argv);
void _start(uint64_t argc, char **argv) {
    exit(main((int)argc, argv));
}
