#ifndef ORION_SYS_FRAMEBUFFER_H
#define ORION_SYS_FRAMEBUFFER_H
#include <stdint.h>
#define ORION_VIDEO_GID 44
#define ORION_FB_IOCTL_ENABLE 0x4F460001UL
#define ORION_FB_IOCTL_DISABLE 0x4F460002UL
#define ORION_FB_IOCTL_GET_INFO 0x4F460003UL
#define ORION_FB_TYPE_INDEXED 0
#define ORION_FB_TYPE_RGB 1
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
#endif
