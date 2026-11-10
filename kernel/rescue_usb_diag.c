// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

static int is_usb_probe_device(const char *name) {
    return strstr(name, "a6f8800") != NULL ||
           strstr(name, "a600000") != NULL ||
           strstr(name, "88e3000") != NULL;
}

static void report_usb_kernel_messages(void) {
    enum { LOG_COUNT = 64, LOG_LENGTH = 256 };
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    char message[2048];
    char recent[LOG_COUNT][LOG_LENGTH];
    unsigned int matches = 0;
    unsigned int i;

    if (fd < 0) {
        fprintf(stderr, "USB kernel log unavailable: %s\n", strerror(errno));
        return;
    }
    while (1) {
        ssize_t length = read(fd, message, sizeof(message) - 1);
        char *content;
        int error_level;

        if (length < 0) {
            if (errno == EPIPE) {
                fprintf(stderr, "USB kernel ring overrun; continuing from retained cursor\n");
                continue;
            }
            if (errno != EAGAIN && errno != EINTR)
                fprintf(stderr, "USB kernel log read failed: %s\n",
                        strerror(errno));
            if (errno != EINTR) break;
            continue;
        }
        if (!length) break;
        message[length] = '\0';
        error_level = length > 1 && message[0] >= '0' &&
                      message[0] <= '3' && message[1] == ',';
        content = strchr(message, ';');
        if (content) content++;
        else content = message;
        if (error_level || contains_case_insensitive(content, "pon") ||
            contains_case_insensitive(content, "pdc") ||
            contains_case_insensitive(content, "psci") ||
            contains_case_insensitive(content, "spmi") ||
            contains_case_insensitive(content, "pmic") ||
            contains_case_insensitive(content, "dwc3") ||
            contains_case_insensitive(content, "usb") ||
            contains_case_insensitive(content, "phy") ||
            contains_case_insensitive(content, "udc") ||
            contains_case_insensitive(content, "pending") ||
            contains_case_insensitive(content, "error") ||
            contains_case_insensitive(content, "failed") ||
            contains_case_insensitive(content, "could not")) {
            size_t copy_length = strnlen(content, LOG_LENGTH - 1);
            memcpy(recent[matches % LOG_COUNT], content, copy_length);
            recent[matches % LOG_COUNT][copy_length] = '\0';
            matches++;
        }
    }
    close(fd);
    if (!matches) {
        puts("No DWC3/USB/PON/SPMI kernel messages retained");
        return;
    }
    for (i = matches > LOG_COUNT ? matches % LOG_COUNT : 0;
         i < (matches > LOG_COUNT ? matches % LOG_COUNT + LOG_COUNT : matches);
         i++)
        printf("Kernel diag %.228s\n", recent[i % LOG_COUNT]);
}

void report_rpmh_kernel_messages(void) {
    enum { LOG_COUNT = 32, LOG_LENGTH = 384 };
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    char message[2048];
    char recent[LOG_COUNT][LOG_LENGTH];
    unsigned int matches = 0;
    unsigned int i;

    if (fd < 0) {
        fprintf(stderr, "RPMh kernel log unavailable: %s\n", strerror(errno));
        return;
    }
    while (1) {
        ssize_t length = read(fd, message, sizeof(message) - 1);
        char *content;

        if (length < 0) {
            if (errno == EPIPE) {
                fprintf(stderr, "RPMh kernel ring overrun; continuing from retained cursor\n");
                continue;
            }
            if (errno != EAGAIN && errno != EINTR)
                fprintf(stderr, "RPMh kernel log read failed: %s\n",
                        strerror(errno));
            if (errno != EINTR) break;
            continue;
        }
        if (!length) break;
        message[length] = '\0';
        content = strchr(message, ';');
        if (content) content++;
        else content = message;
        if (contains_case_insensitive(content, "rpmh") ||
            contains_case_insensitive(content, "cmd-db") ||
            contains_case_insensitive(content, "cmd_db") ||
            contains_case_insensitive(content, "power-controller") ||
            contains_case_insensitive(content, "power domain")) {
            size_t copy_length = strnlen(content, LOG_LENGTH - 1);
            memcpy(recent[matches % LOG_COUNT], content, copy_length);
            recent[matches % LOG_COUNT][copy_length] = '\0';
            matches++;
        }
    }
    close(fd);
    if (!matches) {
        puts("No RPMh/CMD DB/power-domain kernel messages retained");
        return;
    }
    for (i = matches > LOG_COUNT ? matches % LOG_COUNT : 0;
         i < (matches > LOG_COUNT ? matches % LOG_COUNT + LOG_COUNT : matches);
         i++)
        printf("Kernel RPMh diag %.340s\n", recent[i % LOG_COUNT]);
}

void report_usb_probe_devices(void) {
    const char *root = "/sys/bus/platform/devices";
    DIR *dir = opendir(root);
    struct dirent *entry;
    char path[256];
    char driver[256];
    int found = 0;

    if (!dir) {
        fprintf(stderr, "USB platform device list unavailable: %s\n",
                strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        ssize_t length;

        if (entry->d_name[0] == '.' || !is_usb_probe_device(entry->d_name))
            continue;
        found = 1;
        if (snprintf(path, sizeof(path), "%s/%s/driver", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        length = readlink(path, driver, sizeof(driver) - 1);
        if (length < 0) {
            printf("USB probe device %s driver=unbound (%s)\n",
                   entry->d_name, strerror(errno));
            continue;
        }
        driver[length] = '\0';
        printf("USB probe device %s driver=%s\n", entry->d_name, driver);
    }
    closedir(dir);
    if (!found) puts("USB platform devices missing (DWC3/QUSB2 PHY)");
    list_directory("USB entries", "/sys/bus/platform/drivers/dwc3-qcom-legacy");
    list_directory("USB entries", "/sys/bus/platform/drivers/dwc3");
    list_directory("PON driver devices", "/sys/bus/platform/drivers/qcom-pon");
    list_directory("PMIC driver bindings", "/sys/bus/spmi/drivers/pmic-spmi");

    if (node_exists("/sys/kernel/debug/devices_deferred"))
        print_file("/sys/kernel/debug/devices_deferred");
    else
        puts("USB deferred-probe list unavailable");
    report_usb_kernel_messages();
}
