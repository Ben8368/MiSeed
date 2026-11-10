// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

int report_platform_driver(const char *label, const char *compatible) {
    const char *root = "/sys/bus/platform/devices";
    DIR *dir = opendir(root);
    struct dirent *entry;
    char path[256];
    char driver[256];
    unsigned char compatibles[512];
    int found = 0;
    int bound = 0;

    if (!dir) {
        printf("%s device list unavailable: %s\n", label, strerror(errno));
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        ssize_t length;
        int fd;

        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "%s/%s/of_node/compatible", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        length = read(fd, compatibles, sizeof(compatibles));
        close(fd);
        if (length <= 0 || !memmem(compatibles, (size_t)length,
                                   compatible, strlen(compatible)))
            continue;

        found = 1;
        if (snprintf(path, sizeof(path), "%s/%s/driver", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        length = readlink(path, driver, sizeof(driver) - 1);
        if (length < 0) {
            printf("%s device %s driver=unbound (%s)\n", label,
                   entry->d_name, strerror(errno));
            continue;
        }
        driver[length] = '\0';
        printf("%s device %s driver=%s\n", label, entry->d_name, driver);
        bound = 1;
    }
    closedir(dir);
    if (!found) printf("%s compatible device missing: %s\n", label,
                       compatible);
    return bound;
}

void report_i2c_devices(void) {
    const char *root = "/sys/bus/i2c/devices";
    DIR *dir = opendir(root);
    struct dirent *entry;
    char path[512];
    char driver[256];
    char compatible[256];
    unsigned int count = 0;

    if (!dir) {
        printf("I2C device list unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        ssize_t length;
        int fd;

        if (entry->d_name[0] == '.') continue;
        if (count++ == 64) {
            puts("I2C device list truncated after 64 entries");
            break;
        }
        if (snprintf(path, sizeof(path), "%s/%s/driver", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        length = readlink(path, driver, sizeof(driver) - 1);
        if (length < 0) strcpy(driver, "unbound");
        else {
            driver[length] = '\0';
        }

        if (snprintf(path, sizeof(path), "%s/%s/of_node/compatible", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            printf("I2C device %s driver=%s compatible=unavailable\n",
                   entry->d_name, driver);
            continue;
        }
        length = read(fd, compatible, sizeof(compatible) - 1);
        close(fd);
        if (length < 0) {
            printf("I2C device %s driver=%s compatible=read-error\n",
                   entry->d_name, driver);
            continue;
        }
        for (ssize_t i = 0; i < length; i++)
            if (compatible[i] == '\0') compatible[i] = ' ';
        compatible[length] = '\0';
        printf("I2C device %s driver=%s compatible=%s\n",
               entry->d_name, driver, compatible);
    }
    closedir(dir);
    if (!count) puts("I2C device list empty");
    list_directory("Type-C class devices", "/sys/class/typec");
}

void report_spmi_devices(void) {
    const char *root = "/sys/bus/spmi/devices";
    DIR *dir = opendir(root);
    struct dirent *entry;
    char path[256];
    char compatible[256];
    char driver[256];
    int found = 0;

    if (!dir) {
        printf("SPMI device list unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        ssize_t length;
        int fd;

        if (entry->d_name[0] == '.') continue;
        found = 1;
        if (snprintf(path, sizeof(path), "%s/%s/of_node/compatible", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            printf("SPMI device %s compatible unavailable: %s\n",
                   entry->d_name, strerror(errno));
            continue;
        }
        length = read(fd, compatible, sizeof(compatible) - 1);
        close(fd);
        if (length < 0) {
            printf("SPMI device %s compatible read failed: %s\n",
                   entry->d_name, strerror(errno));
            continue;
        }
        for (ssize_t i = 0; i < length; i++)
            if (compatible[i] == '\0') compatible[i] = ' ';
        compatible[length] = '\0';

        if (snprintf(path, sizeof(path), "%s/%s/driver", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        length = readlink(path, driver, sizeof(driver) - 1);
        if (length < 0) {
            printf("SPMI device %s compatible=%s driver=unbound (%s)\n",
                   entry->d_name, compatible, strerror(errno));
            continue;
        }
        driver[length] = '\0';
        printf("SPMI device %s compatible=%s driver=%s\n",
               entry->d_name, compatible, driver);
    }
    closedir(dir);
    if (!found) puts("SPMI device list empty");
}

void report_spmi_dt_nodes(void) {
    const char *root = "/sys/firmware/devicetree/base/soc@0/spmi@c440000";
    DIR *dir = opendir(root);
    struct dirent *entry;

    if (!dir) {
        printf("SPMI DT list unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[320];
        char compatible[256];
        char status[32];
        unsigned char reg[32];
        ssize_t length;
        int fd;

        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "%s/%s/compatible", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        length = read(fd, compatible, sizeof(compatible) - 1);
        close(fd);
        if (length < 0) continue;
        for (ssize_t i = 0; i < length; i++)
            if (compatible[i] == '\0') compatible[i] = ' ';
        compatible[length] = '\0';

        if (snprintf(path, sizeof(path), "%s/%s/status", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            strcpy(status, "okay(default)");
        } else {
            length = read(fd, status, sizeof(status) - 1);
            close(fd);
            if (length < 0) strcpy(status, "unreadable");
            else {
                while (length > 0 && status[length - 1] == '\0') length--;
                status[length] = '\0';
            }
        }

        if (snprintf(path, sizeof(path), "%s/%s/reg", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            printf("SPMI DT child %s status=%s compatible=%s reg=missing\n",
                   entry->d_name, status, compatible);
            continue;
        }
        length = read(fd, reg, sizeof(reg));
        close(fd);
        printf("SPMI DT child %s status=%s compatible=%s reg-bytes=%zd",
               entry->d_name, status, compatible, length);
        if (length > 0) {
            printf(" reg=");
            for (ssize_t i = 0; i + 3 < length; i += 4) {
                uint32_t value = ((uint32_t)reg[i] << 24) |
                    ((uint32_t)reg[i + 1] << 16) |
                    ((uint32_t)reg[i + 2] << 8) | reg[i + 3];
                printf("%s%08x", i ? "," : "", value);
            }
        }
        puts("");
    }
    closedir(dir);
}
