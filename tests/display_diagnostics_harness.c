// SPDX-License-Identifier: AGPL-3.0-only
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static int fake_klogctl(int action, char *buffer, int size);
static int fake_open(const char *path, int flags, ...);
static int fake_ioctl(int fd, unsigned long request, ...);
static int fake_close(int fd);
static int fake_clock_gettime(clockid_t clock, struct timespec *time);
#define klogctl fake_klogctl
#define open fake_open
#define ioctl fake_ioctl
#define close fake_close
#define clock_gettime fake_clock_gettime
#include "../kernel/rescue_display.c"
#undef klogctl
#undef open
#undef ioctl
#undef close
#undef clock_gettime
#ifdef DISPLAY_TEST_CANONICAL_DRM_UAPI
_Static_assert(MSEED_DRM_IOCTL_WAIT_VBLANK == DRM_IOCTL_WAIT_VBLANK,
               "local DRM vblank ioctl must match the Linux UAPI");
#endif
static int writes, calls, mode;
static unsigned int vblank_calls;
static unsigned int vblank_sequence = 100;
static unsigned int clock_calls;
static unsigned int close_calls;
int print_file(const char *p) { return 0; }
int report_platform_driver(const char *l, const char *c) { return 0; }
void list_directory(const char *l, const char *p) { }
int write_value(const char *path, const char *value) {
    FILE *file;
    writes++;
    if (mode == 3) return -1;
    file = fopen(path, "w");
    if (!file) return -1;
    fputs(mode == 4 ? "99" : value, file);
    return fclose(file);
}
static int fake_klogctl(int action, char *buffer, int size) {
    const char message[] = "<6>unfiltered drm ready\n";
    calls++;
    assert(action == 3 || action == 10); /* Never clear kernel messages. */
    if (mode == 1) { errno = EPERM; return -1; }
    if (action == 10) return mode == 2 ? 2 * 1024 * 1024 : 4096;
    assert(size == 4096);
    memcpy(buffer, message, sizeof(message) - 1);
    return sizeof(message) - 1;
}
static int fake_open(const char *path, int flags, ...) {
    if (!strcmp(path, "/dev/dri/card1")) {
        assert((flags & (O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) ==
               (O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (mode == 7) {
            errno = ENODEV;
            return -1;
        }
        return 17;
    }
    return open(path, flags);
}
static int fake_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    union mseed_drm_wait_vblank *wait;
    assert(fd == 17);
    assert(request == MSEED_DRM_IOCTL_WAIT_VBLANK);
    va_start(args, request);
    wait = va_arg(args, union mseed_drm_wait_vblank *);
    va_end(args);
    assert(wait->request.type == MSEED_DRM_VBLANK_RELATIVE);
    assert(wait->request.sequence == 1);
    vblank_calls++;
    if (mode == 6 && vblank_calls == 5) {
        errno = EINVAL;
        return -1;
    }
    vblank_sequence += mode == 9 && vblank_calls == 5 ? 2U : 1U;
    wait->reply.sequence = vblank_sequence;
    return 0;
}
static int fake_close(int fd) {
    if (fd == 17) {
        close_calls++;
        return 0;
    }
    return close(fd);
}
static int fake_clock_gettime(clockid_t clock, struct timespec *time) {
    unsigned long long ns;
    assert(clock == CLOCK_MONOTONIC);
    ns = (unsigned long long)++clock_calls * 11111111ULL;
    time->tv_sec = (time_t)(ns / 1000000000ULL);
    time->tv_nsec = (long)(ns % 1000000000ULL);
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    mode = atoi(argv[2]);
    if (!strcmp(argv[1], "vblank")) {
        measure_display_vblank();
        assert(vblank_calls == (mode == 6 ? 5U : mode == 7 ? 0U : 91U));
        assert(clock_calls == (mode == 6 ? 4U : mode == 7 ? 0U : 91U));
        assert(close_calls == (mode == 7 ? 0U : 1U));
        return 0;
    }
    if (!strcmp(argv[1], "kmsg")) {
        export_kernel_log();
        assert(calls == (mode ? 1 : 2));
        return 0;
    }
    int rc = display_brightness_at(argv[1], mode == 5 ? 2048 : 1500);
    printf("TEST writes=%d rc=%d\n", writes, rc);
    return 0;
}
