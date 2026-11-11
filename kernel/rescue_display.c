// SPDX-License-Identifier: AGPL-3.0-only
/* Standard DRM/backlight bring-up diagnostics; no storage or register writes. */
#define _GNU_SOURCE
#include "rescue_internal.h"
#ifdef __linux__
#include <sys/klog.h>
#endif
#include <limits.h>

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
