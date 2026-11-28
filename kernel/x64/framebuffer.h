#ifndef X64_FRAMEBUFFER_H
#define X64_FRAMEBUFFER_H
#include <stddef.h>
#include <stdint.h>
#define ORION_VIDEO_GID 44
#define ORION_FB_IOCTL_ENABLE 0x4F460001UL
#define ORION_FB_IOCTL_DISABLE 0x4F460002UL
#define ORION_FB_IOCTL_GET_INFO 0x4F460003UL
struct orion_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t type;
    uint32_t red_field_position;
    uint32_t red_mask_size;
    uint32_t green_field_position;
    uint32_t green_mask_size;
    uint32_t blue_field_position;
    uint32_t blue_mask_size;
    uint32_t reserved;
    uint64_t size;
};
int framebuffer_init(uint32_t magic, uint32_t info_ptr);
int framebuffer_available(void);
int framebuffer_get_info(struct orion_fb_info *info);
int framebuffer_set_active(int enabled);
int framebuffer_active(void);
int64_t framebuffer_read(uint64_t offset, void *buffer, size_t size);
int64_t framebuffer_write(uint64_t offset, const void *buffer, size_t size);
int framebuffer_ioctl(unsigned long request, void *argument);
#endif
