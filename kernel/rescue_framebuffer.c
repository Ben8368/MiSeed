// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only fbdev thumbnail for distinguishing buffer content from scanout.
 * Diagnostic only: no mmap, register access, blanking, mode or pixel writes.
 */
#define _GNU_SOURCE
#include "rescue_internal.h"
#include <linux/fb.h>
#include <sys/ioctl.h>

#define FRAME_STEP 8U
#define FRAME_WIDTH 1080U
#define FRAME_HEIGHT 2340U

static int frame_layout(const struct fb_fix_screeninfo *fix,
                        const struct fb_var_screeninfo *var) {
    uint64_t last;

    if (memcmp(fix->id, "msmdrmfb", sizeof("msmdrmfb")) ||
        fix->type != FB_TYPE_PACKED_PIXELS || fix->visual != FB_VISUAL_TRUECOLOR ||
        var->xres != FRAME_WIDTH || var->yres != FRAME_HEIGHT ||
        var->bits_per_pixel != 32 || var->grayscale || var->nonstd ||
        var->red.offset != 16 || var->red.length != 8 || var->red.msb_right ||
        var->green.offset != 8 || var->green.length != 8 || var->green.msb_right ||
        var->blue.offset || var->blue.length != 8 || var->blue.msb_right ||
        var->xres_virtual < var->xres || var->xres_virtual > 4096 ||
        var->yres_virtual < var->yres || var->yres_virtual > 4096 ||
        var->xoffset > var->xres_virtual - var->xres ||
        var->yoffset > var->yres_virtual - var->yres ||
        fix->line_length < (uint64_t)var->xres_virtual * 4 ||
        fix->line_length > 16384 || fix->smem_len > 64U * 1024 * 1024)
        return -1;
    last = ((uint64_t)var->yoffset + var->yres - 1) * fix->line_length +
           ((uint64_t)var->xoffset + var->xres) * 4;
    return last <= fix->smem_len ? 0 : -1;
}

static int display_frame_fd(int fd) {
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    uint32_t row[FRAME_WIDTH];
    char hex[FRAME_WIDTH / FRAME_STEP * 6 + 1];
    static const char digits[] = "0123456789abcdef";

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) ||
        ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
        puts("FRAMEBUFFER_ERROR ioctl failed");
        return -1;
    }
    printf("FRAMEBUFFER_INFO id=%.*s width=%u height=%u virtual=%ux%u "
           "offset=%u,%u bpp=%u stride=%u rotate=%u\n", 16, fix.id,
           var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           var.xoffset, var.yoffset, var.bits_per_pixel, fix.line_length, var.rotate);
    if (frame_layout(&fix, &var)) {
        puts("FRAMEBUFFER_ERROR unsupported identity, layout or bounds");
        return -1;
    }
    printf("FRAMEBUFFER_RGB width=%u height=%u step=%u atomic=0 orientation=physical\n",
           FRAME_WIDTH / FRAME_STEP, (FRAME_HEIGHT + FRAME_STEP - 1) / FRAME_STEP,
           FRAME_STEP);
    for (unsigned int y = 0; y < FRAME_HEIGHT; y += FRAME_STEP) {
        uint64_t offset = ((uint64_t)var.yoffset + y) * fix.line_length +
                          (uint64_t)var.xoffset * 4;
        ssize_t got;
        do { got = pread(fd, row, sizeof(row), (off_t)offset); }
        while (got < 0 && errno == EINTR);
        if (got != (ssize_t)sizeof(row)) {
            printf("FRAMEBUFFER_ERROR read row=%u bytes=%zd\n", y, got);
            return -1;
        }
        for (unsigned int x = 0; x < FRAME_WIDTH; x += FRAME_STEP) {
            unsigned int i = x / FRAME_STEP * 6;
            uint32_t rgb = row[x];
            for (unsigned int j = 0; j < 6; j++)
                hex[i + j] = digits[(rgb >> (20 - 4 * j)) & 15];
        }
        hex[sizeof(hex) - 1] = '\0';
        printf("FRAMEBUFFER_ROW y=%u rgb=%s\n", y, hex);
    }
    puts("FRAMEBUFFER_COMPLETE rows=293");
    return 0;
}

void export_display_frame(void) {
    int fd, ret = -1;

    puts("FRAMEBUFFER_BEGIN");
    /* With early simpledrm removed by MSM, the native fbdev reuses fb0.
     * Any different identity is reported and rejected, never written to.
     */
    fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) puts("FRAMEBUFFER_ERROR fb0 unavailable");
    else {
        struct stat st;
        if (fstat(fd, &st) || !S_ISCHR(st.st_mode))
            puts("FRAMEBUFFER_ERROR fb0 is not a character device");
        else
            ret = display_frame_fd(fd);
        close(fd);
    }
    printf("FRAMEBUFFER_END rc=%d\n", ret);
}
