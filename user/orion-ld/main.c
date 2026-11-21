#include "ld.h"
#include <stdlib.h>

int main(int argc, char **argv) {
    Linker linker;
    ld_parse_arguments(&linker, argc, argv);
    ld_read_script(&linker);
    ld_load_inputs(&linker);
    ld_resolve_and_layout(&linker);
    ld_write_executable(&linker);
    for (size_t i = 0; i < linker.input_count; ++i) {
        free(linker.inputs[i].data);
        free(linker.inputs[i].kinds);
        free(linker.inputs[i].addresses);
    }
    for (size_t i = 0; i < linker.definition_count; ++i) free(linker.definitions[i].name);
    free(linker.definitions);
    free(linker.inputs);
    free(linker.input_paths);
    free(linker.output.data);
    return 0;
}
