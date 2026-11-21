#include "ld.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LD_PT_LOAD 1
#define LD_PF_X 1
#define LD_PF_W 2
#define LD_PF_R 4

static void write_output_file(const char *path, const unsigned char *data, size_t size) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (descriptor < 0) ld_fail("cannot create output");
    size_t written = 0;
    while (written < size) {
        size_t request = size - written;
        if (request > 4096) request = 4096;
        ssize_t count = write(descriptor, data + written, request);
        if (count <= 0) {
            close(descriptor);
            ld_fail("cannot write output");
        }
        written += (size_t)count;
    }
    close(descriptor);
}

void ld_write_executable(Linker *linker) {
    if (linker->file_end < linker->base_address || linker->image_end < linker->file_end) ld_fail("invalid output layout");
    uint64_t file_size = linker->file_end - linker->base_address;
    uint64_t memory_size = linker->image_end - linker->base_address;
    if (file_size < 0x1000 || file_size > 16 * 1024 * 1024 || memory_size > 64 * 1024 * 1024 || memory_size < file_size) ld_fail("output image is too large");
    linker->output.size = 0;
    linker->output.capacity = 0;
    linker->output.data = NULL;
    ld_buffer_zero(&linker->output, (size_t)file_size);
    for (size_t i = 0; i < linker->input_count; ++i) {
        LdInput *input = &linker->inputs[i];
        for (uint16_t j = 1; j < input->header->section_count; ++j) {
            if (!input->kinds[j] || input->sections[j].type == 8 || input->sections[j].size == 0) continue;
            uint64_t offset = input->addresses[j] - linker->base_address;
            uint64_t size = input->sections[j].size;
            if (offset > linker->output.size || size > linker->output.size - offset || !ld_range_ok(input->size, input->sections[j].offset, size)) ld_fail("section is outside the output image");
            memcpy(linker->output.data + offset, input->data + input->sections[j].offset, (size_t)size);
        }
    }
    LdElfHeader header;
    memset(&header, 0, sizeof(header));
    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = 2;
    header.ident[5] = 1;
    header.ident[6] = 1;
    header.type = 2;
    header.machine = 62;
    header.version = 1;
    header.entry = linker->entry_address;
    header.program_offset = sizeof(LdElfHeader);
    header.header_size = sizeof(LdElfHeader);
    header.program_entry_size = sizeof(LdProgramHeader);
    header.program_count = 1;
    LdProgramHeader program;
    memset(&program, 0, sizeof(program));
    program.type = LD_PT_LOAD;
    program.flags = LD_PF_R | LD_PF_W | LD_PF_X;
    program.offset = 0;
    program.address = linker->base_address;
    program.physical_address = linker->base_address;
    program.file_size = file_size;
    program.memory_size = memory_size;
    program.alignment = 0x1000;
    if (sizeof(header) + sizeof(program) > linker->output.size) ld_fail("output header is too large");
    memcpy(linker->output.data, &header, sizeof(header));
    memcpy(linker->output.data + sizeof(header), &program, sizeof(program));
    ld_apply_relocations(linker);
    write_output_file(linker->output_path, linker->output.data, linker->output.size);
}
