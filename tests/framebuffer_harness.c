// SPDX-License-Identifier: AGPL-3.0-only
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <linux/fb.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
static int fake_ioctl(int fd, unsigned long request, ...);
static ssize_t fake_pread(int fd, void *data, size_t size, off_t offset);
#define ioctl fake_ioctl
#define pread fake_pread
#include "../kernel/rescue_framebuffer.c"
#undef ioctl
#undef pread
static int mode, reads, ioctls;
static int fake_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    assert(fd == 7);
    ioctls++;
    if (mode == 1) return -1;
    va_start(ap, request);
    void *out = va_arg(ap, void *);
    va_end(ap);
    if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f = out;
        memset(f, 0, sizeof(*f));
        memcpy(f->id, "msmdrmfb", sizeof("msmdrmfb"));
        f->type = FB_TYPE_PACKED_PIXELS;
        f->visual = FB_VISUAL_TRUECOLOR;
        f->line_length = 4352;
        f->smem_len = 4352 * 2340;
        if (mode == 2) memcpy(f->id, "simplefb", sizeof("simplefb"));
        if (mode == 3) f->line_length = 4320 - 1;
        if (mode == 4) f->smem_len = 4352 * 2339 + 4320 - 1;
    } else {
        assert(request == FBIOGET_VSCREENINFO);
        struct fb_var_screeninfo *v = out;
        memset(v, 0, sizeof(*v));
        v->xres = v->xres_virtual = 1080;
        v->yres = v->yres_virtual = 2340;
        v->bits_per_pixel = 32;
        v->red = (struct fb_bitfield){16, 8, 0};
        v->green = (struct fb_bitfield){8, 8, 0};
        v->blue = (struct fb_bitfield){0, 8, 0};
        if (mode == 5) v->bits_per_pixel = 24;
        if (mode == 6) v->red.offset = 0;
        if (mode == 7) v->xoffset = UINT32_MAX;
        if (mode == 8) v->yoffset = UINT32_MAX;
        if (mode == 9) v->xres_virtual = UINT32_MAX;
        if (mode == 10) v->yres = 1080;
    }
    return 0;
}
static ssize_t fake_pread(int fd, void *data, size_t size, off_t offset) {
    assert(fd == 7 && size == 4320);
    assert(offset == (off_t)reads * 8 * 4352);
    if (mode == 11) return (ssize_t)size - 1;
    if (mode == 12) { errno = EIO; return -1; }
    if (mode == 13 && !reads) { mode = 0; errno = EINTR; return -1; }
    memset(data, 0, size);
    ((uint32_t *)data)[0] = 0x00123456;
    ((uint32_t *)data)[8] = 0xffffffff;
    reads++;
    return (ssize_t)size;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    mode = atoi(argv[1]);
    int expected = mode == 0 || mode == 13 ? 0 : -1;
    int ret = display_frame_fd(7);
    assert(ret == expected);
    if (expected == 0) assert(reads == 293 && ioctls == 2);
    else assert(!reads);
    return 0;
}
