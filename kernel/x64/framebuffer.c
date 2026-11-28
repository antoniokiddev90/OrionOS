#include "framebuffer.h"
#include "mb2.h"
#include "paging.h"
#include "vga.h"
#include "../mm/vmm.h"
#include <stdint.h>
#define FRAMEBUFFER_KERNEL_BASE 0xFFFF800000000000ULL
static struct orion_fb_info framebuffer_info;
static volatile uint8_t *framebuffer_memory;
static uint64_t framebuffer_size;
static int framebuffer_ready;
static int display_active;
static int supported_bpp(uint8_t bpp) {
    return bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
}
int framebuffer_init(uint32_t magic, uint32_t info_ptr) {
    struct mb2_framebuffer detected;
    if (mb2_find_framebuffer(magic, info_ptr, &detected) != 1) return -1;
    if (!detected.address || !detected.width || !detected.height || !detected.pitch ||
        !supported_bpp(detected.bits_per_pixel) || detected.type > 1) return -1;
    if (detected.type == 1 && (!detected.red_mask_size || !detected.green_mask_size || !detected.blue_mask_size)) return -1;
    uint64_t minimum_pitch = (uint64_t)detected.width * ((detected.bits_per_pixel + 7) / 8);
    if (detected.pitch < minimum_pitch) return -1;
    if (detected.type == 1 &&
        ((uint16_t)detected.red_field_position + detected.red_mask_size > detected.bits_per_pixel ||
         (uint16_t)detected.green_field_position + detected.green_mask_size > detected.bits_per_pixel ||
         (uint16_t)detected.blue_field_position + detected.blue_mask_size > detected.bits_per_pixel)) return -1;
    framebuffer_size = (uint64_t)detected.pitch * detected.height;
    uint64_t max_physical = PTE_ADDR_MASK | (PAGE_SIZE - 1);
    if (!framebuffer_size || detected.address > max_physical ||
        framebuffer_size - 1 > max_physical - detected.address) return -1;
    uint64_t page_base = detected.address & PAGE_MASK;
    uint64_t page_offset = detected.address - page_base;
    if (framebuffer_size > UINT64_MAX - page_offset) return -1;
    uint64_t map_span = page_offset + framebuffer_size;
    if (map_span > UINT64_MAX - (PAGE_SIZE - 1)) return -1;
    uint64_t mapped_size = (map_span + PAGE_SIZE - 1) & PAGE_MASK;
    if (!mapped_size || mapped_size > 0x0000008000000000ULL) return -1;
    if (vmm_map(FRAMEBUFFER_KERNEL_BASE, page_base, mapped_size,
                PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT) != 0) return -1;
    framebuffer_memory = (volatile uint8_t *)(FRAMEBUFFER_KERNEL_BASE + page_offset);
    framebuffer_info.width = detected.width;
    framebuffer_info.height = detected.height;
    framebuffer_info.pitch = detected.pitch;
    framebuffer_info.bits_per_pixel = detected.bits_per_pixel;
    framebuffer_info.type = detected.type;
    framebuffer_info.red_field_position = detected.red_field_position;
    framebuffer_info.red_mask_size = detected.red_mask_size;
    framebuffer_info.green_field_position = detected.green_field_position;
    framebuffer_info.green_mask_size = detected.green_mask_size;
    framebuffer_info.blue_field_position = detected.blue_field_position;
    framebuffer_info.blue_mask_size = detected.blue_mask_size;
    framebuffer_info.reserved = 0;
    framebuffer_info.size = framebuffer_size;
    framebuffer_ready = 1;
    display_active = 1;
    return 0;
}
int framebuffer_available(void) { return framebuffer_ready; }
int framebuffer_get_info(struct orion_fb_info *info) {
    if (!framebuffer_ready || !info) return -1;
    *info = framebuffer_info;
    return 0;
}
int framebuffer_set_active(int enabled) {
    if (!framebuffer_ready) return -1;
    if (enabled) {
        if (!display_active) vga_restore_mode();
        display_active = 1;
        return 0;
    }
    if (display_active) vga_set_text_mode();
    display_active = 0;
    return 0;
}
int framebuffer_active(void) { return display_active; }
int64_t framebuffer_read(uint64_t offset, void *buffer, size_t size) {
    if (!framebuffer_ready || (!buffer && size) || offset > framebuffer_size || size > framebuffer_size - offset) return -1;
    uint8_t *destination = (uint8_t *)buffer;
    for (size_t i = 0; i < size; ++i) destination[i] = framebuffer_memory[offset + i];
    return (int64_t)size;
}
int64_t framebuffer_write(uint64_t offset, const void *buffer, size_t size) {
    if (!framebuffer_ready || (!buffer && size) || offset > framebuffer_size || size > framebuffer_size - offset) return -1;
    const uint8_t *source = (const uint8_t *)buffer;
    for (size_t i = 0; i < size; ++i) framebuffer_memory[offset + i] = source[i];
    return (int64_t)size;
}
int framebuffer_ioctl(unsigned long request, void *argument) {
    if (!framebuffer_ready) return -1;
    if (request == ORION_FB_IOCTL_ENABLE) return framebuffer_set_active(1);
    if (request == ORION_FB_IOCTL_DISABLE) return framebuffer_set_active(0);
    if (request == ORION_FB_IOCTL_GET_INFO) return framebuffer_get_info((struct orion_fb_info *)argument);
    return -1;
}
