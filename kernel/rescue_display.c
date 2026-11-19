// SPDX-License-Identifier: AGPL-3.0-only
/* Standard DRM/backlight bring-up diagnostics; no storage or register writes. */
#define _GNU_SOURCE
#include "rescue_internal.h"
#include <sys/klog.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <time.h>

/* Linux DRM UAPI, include/uapi/drm/drm.h, using the 64-bit Linux ABI. */
struct mseed_drm_wait_vblank_request {
    unsigned int type;
    unsigned int sequence;
    unsigned long signal;
};

struct mseed_drm_wait_vblank_reply {
    unsigned int type;
    unsigned int sequence;
    long tval_sec;
    long tval_usec;
};

union mseed_drm_wait_vblank {
    struct mseed_drm_wait_vblank_request request;
    struct mseed_drm_wait_vblank_reply reply;
};

#define MSEED_DRM_VBLANK_RELATIVE 0x1U
#define MSEED_DRM_IOCTL_WAIT_VBLANK \
    _IOWR('d', 0x3a, union mseed_drm_wait_vblank)
#define DISPLAY_VBLANK_SAMPLES 91U

_Static_assert(sizeof(union mseed_drm_wait_vblank) == 24,
               "DRM vblank UAPI layout must match 64-bit Linux");

static volatile sig_atomic_t display_vblank_timeout;

static void display_vblank_alarm(int signal_number) {
    (void)signal_number;
    display_vblank_timeout = 1;
}

static uint64_t display_timespec_ns(const struct timespec *time) {
    return (uint64_t)time->tv_sec * 1000000000ULL + (uint64_t)time->tv_nsec;
}

void measure_display_vblank(void) {
    struct sigaction action = {0}, previous_action;
    struct timespec first = {0}, previous = {0}, now;
    uint64_t min_interval = UINT64_MAX, max_interval = 0, span = 0;
    uint32_t last_sequence = 0, sequence_gaps = 0;
    unsigned int events = 0;
    int fd, rc = -1, saved_errno = 0;

    display_vblank_timeout = 0;
    puts("DISPLAY_VBLANK_BEGIN");
    puts("DISPLAY_VBLANK device=/dev/dri/card1 crtc=0 source=DRM_IOCTL_WAIT_VBLANK");
    puts("DISPLAY_VBLANK_METRIC=DRM command-mode callback cadence; may follow TE/read-pointer and is not an optical panel-scan measurement.");
    puts("Read-only DRM_IOCTL_WAIT_VBLANK; no modeset, framebuffer or brightness writes.");
    fd = open("/dev/dri/card1", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        saved_errno = errno;
        goto done;
    }
    action.sa_handler = display_vblank_alarm;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, &previous_action)) {
        saved_errno = errno;
        goto close_done;
    }
    alarm(4);
    for (unsigned int i = 0; i < DISPLAY_VBLANK_SAMPLES; i++) {
        union mseed_drm_wait_vblank wait = {0};
        wait.request.type = MSEED_DRM_VBLANK_RELATIVE;
        wait.request.sequence = 1;
        if (ioctl(fd, MSEED_DRM_IOCTL_WAIT_VBLANK, &wait)) {
            saved_errno = errno;
            goto restore_done;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now)) {
            saved_errno = errno;
            goto restore_done;
        }
        if (events) {
            uint32_t step = wait.reply.sequence - last_sequence;
            uint64_t interval = display_timespec_ns(&now) -
                                display_timespec_ns(&previous);
            if (step > 1) sequence_gaps += step - 1;
            if (!step) sequence_gaps++;
            if (interval && interval < min_interval) min_interval = interval;
            if (interval > max_interval) max_interval = interval;
        } else first = now;
        previous = now;
        last_sequence = wait.reply.sequence;
        events++;
    }
    alarm(0);
    rc = 0;
restore_done:
    alarm(0);
    if (sigaction(SIGALRM, &previous_action, NULL) && !saved_errno) {
        saved_errno = errno;
        rc = -1;
    }
close_done:
    close(fd);
done:
    if (events > 1)
        span = display_timespec_ns(&previous) - display_timespec_ns(&first);
    if (span && min_interval != UINT64_MAX && max_interval) {
        double event_avg_hz = (double)(events - 1) * 1000000000.0 / (double)span;
        double event_min_hz = 1000000000.0 / (double)max_interval;
        double event_max_hz = 1000000000.0 / (double)min_interval;
        printf("DISPLAY_VBLANK_RESULT events=%u sequence_gaps=%u span_ms=%.3f "
               "event_avg_hz=%.3f event_min_hz=%.3f event_max_hz=%.3f errno=%d rc=%d\n",
               events, sequence_gaps, (double)span / 1000000.0,
               event_avg_hz, event_min_hz, event_max_hz, saved_errno, rc);
    } else {
        printf("DISPLAY_VBLANK_RESULT events=%u sequence_gaps=%u "
               "timed_out=%d errno=%d rc=%d\n", events, sequence_gaps,
               display_vblank_timeout, saved_errno, rc ? -1 : 0);
    }
    puts("DISPLAY_VBLANK_END");
}

static int display_path(char *path, size_t size, const char *root,
                        const char *entry, const char *attribute) {
    int n = snprintf(path, size, "%s/%s/%s", root, entry, attribute);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

static int display_number(const char *path, unsigned int *number) {
    char text[64], extra, *end;
    FILE *file = fopen(path, "r");
    int ret;
    unsigned long value;

    if (!file) return -1;
    ret = fscanf(file, "%63s %c", text, &extra);
    if (fclose(file)) return -1;
    if (ret != 1 || !isdigit((unsigned char)text[0])) return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value > UINT_MAX) return -1;
    *number = (unsigned int)value;
    return 0;
}

static void display_class(const char *root, const char *const *attributes) {
    DIR *dir = opendir(root);
    struct dirent *entry;
    unsigned int count = 0;

    if (!dir) {
        printf("DISPLAY unavailable %s: %s\n", root, strerror(errno));
        return;
    }
    while ((entry = readdir(dir))) {
        char path[1024], driver[512];
        ssize_t length;
        if (entry->d_name[0] == '.') continue;
        if (++count > 64) { puts("DISPLAY listing truncated"); break; }
        printf("DISPLAY device=%s/%s\n", root, entry->d_name);
        if (!display_path(path, sizeof(path), root, entry->d_name, "device/driver")) {
            length = readlink(path, driver, sizeof(driver) - 1);
            if (length >= 0) {
                driver[length] = '\0';
                printf("DISPLAY driver=%s\n", driver);
            } else printf("DISPLAY driver unavailable: %s\n", strerror(errno));
        }
        for (size_t i = 0; attributes[i]; i++) {
            if (display_path(path, sizeof(path), root, entry->d_name, attributes[i]))
                continue;
            printf("DISPLAY attribute=%s\n", path);
            print_file(path);
            putchar('\n');
        }
    }
    printf("DISPLAY entries=%u root=%s\n", count, root);
    closedir(dir);
}

void report_display_status(void) {
    static const char *const drm[] = {"status", "enabled", "dpms", "modes", NULL};
    static const char *const backlight[] = {
        "brightness", "actual_brightness", "max_brightness", "bl_power", NULL};
    static const char *const fb[] = {"name", "virtual_size", "blank", NULL};
    static const char *const debug[] = {"name", "state", NULL};

    puts("DISPLAY_BEGIN");
    report_platform_driver("MDSS", "qcom,sm8250-mdss");
    report_platform_driver("DPU", "qcom,sm8250-dpu");
    report_platform_driver("DSI", "qcom,sm8250-dsi-ctrl");
    report_platform_driver("DSI PHY", "qcom,dsi-phy-7nm");
    print_file("/proc/interrupts");
    print_file("/sys/kernel/debug/clk/clk_summary");
    print_file("/sys/kernel/debug/gpio");
    display_class("/sys/class/drm", drm);
    display_class("/sys/class/backlight", backlight);
    display_class("/sys/class/graphics", fb);
    display_class("/sys/kernel/debug/dri", debug);
    list_directory("DSI panel driver", "/sys/bus/mipi-dsi/drivers/panel-xiaomi-umi-csot");
    print_file("/proc/fb");
    puts("DISPLAY_END");
}

static int csot_backlight(const char *root, char *selected, size_t size) {
    DIR *dir = opendir(root);
    struct dirent *entry;
    unsigned int count = 0;
    int invalid = 0;

    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char path[1024], compatible[64];
        const char expected[] = "xiaomi,umi-csot";
        unsigned int maximum;
        int fd;
        ssize_t length;
        if (entry->d_name[0] == '.') continue;
        if (display_path(path, sizeof(path), root, entry->d_name,
                         "device/of_node/compatible")) { invalid = 1; break; }
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        length = read(fd, compatible, sizeof(compatible));
        close(fd);
        if (length != sizeof(expected) || memcmp(compatible, expected, sizeof(expected)))
            continue;
        count++;
        if (display_path(path, sizeof(path), root, entry->d_name, "max_brightness") ||
            display_number(path, &maximum) || maximum != 2047 ||
            display_path(selected, size, root, entry->d_name, "brightness"))
            invalid = 1;
    }
    closedir(dir);
    return count == 1 && !invalid ? 0 : -1;
}

static int display_brightness_at(const char *root, unsigned int value) {
    char path[1024], text[16];
    unsigned int readback = 0;
    int ret = -1;

    if (value != 0 && value != 1500) goto out;
    if (csot_backlight(root, path, sizeof(path))) goto out;
    /* Force an update even when the requested software level is unchanged. */
    if (write_value(path, "0")) goto out;
    if (value) {
        snprintf(text, sizeof(text), "%u", value);
        if (write_value(path, text)) goto out;
    }
    if (display_number(path, &readback) || readback != value) goto out;
    ret = 0;
out:
    printf("DISPLAY_CONTROL value=%u readback=%u rc=%d (software readback only)\n",
           value, readback, ret);
    return ret;
}

int set_display_brightness(unsigned int value) {
    puts("DISPLAY_CONTROL_BEGIN");
    int ret = display_brightness_at("/sys/class/backlight", value);
    puts("DISPLAY_CONTROL_END");
    return ret;
}

void export_kernel_log(void) {
    const int limit = 1024 * 1024;
    int size = klogctl(10, NULL, 0), length;
    char *buffer;

    puts("KMSG_BEGIN");
    if (size <= 0 || size > limit) {
        printf("KMSG_ERROR ring_size=%d errno=%d limit=%d\n", size, errno, limit);
        goto out;
    }
    buffer = malloc((size_t)size);
    if (!buffer) { puts("KMSG_ERROR allocation failed"); goto out; }
    /* READ_ALL leaves the ring intact; do not use READ_CLEAR or CLEAR. */
    length = klogctl(3, buffer, size);
    if (length < 0) printf("KMSG_ERROR read errno=%d\n", errno);
    else {
        printf("KMSG_SNAPSHOT bytes=%d ring_bytes=%d filtered=0 clear=0\n", length, size);
        fwrite(buffer, 1, (size_t)length, stdout);
        putchar('\n');
        puts("KMSG_NOTE overwritten early messages cannot be recovered from the ring");
    }
    free(buffer);
out:
    puts("KMSG_END");
}
