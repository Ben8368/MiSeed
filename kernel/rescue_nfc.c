// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

static ssize_t nfc_read_property(const char *path, unsigned char *value, size_t size) {
    struct stat st;
    ssize_t length;
    int fd;

    if (lstat(path, &st) || !S_ISREG(st.st_mode)) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return -1;
    do {
        length = read(fd, value, size);
    } while (length < 0 && errno == EINTR);
    close(fd);
    return length;
}

void report_nfc_status(const char *root, const char *class_root) {
    static const char *const properties[] = {
        "reg", "enable-gpios", "firmware-gpios"
    };
    const char *compatible = "nxp,sn100t";
    DIR *dir = opendir(root);
    struct dirent *entry;
    unsigned int scanned = 0, found = 0;
    int incomplete = 0;

    puts("NFC metadata only: no device open, ioctl, NCI traffic, RF or power changes");
    if (!dir) {
        printf("NFC I2C metadata unavailable: %s\n", strerror(errno));
        incomplete = 1;
    }
    while (dir && (entry = readdir(dir)) != NULL) {
        char path[1024], driver[512];
        unsigned char value[512];
        ssize_t length;
        size_t offset = 0;
        int matches = 0;

        if (entry->d_name[0] == '.') continue;
        if (scanned++ == 256) {
            puts("NFC metadata scan truncated after 256 entries");
            incomplete = 1;
            break;
        }
        if (snprintf(path, sizeof(path), "%s/%s/of_node/compatible", root,
                     entry->d_name) >= (int)sizeof(path)) {
            incomplete = 1;
            continue;
        }
        length = nfc_read_property(path, value, sizeof(value));
        /* A missing compatible is normal for adapters and non-DT clients. */
        if (length <= 0) continue;
        if (length == (ssize_t)sizeof(value) || value[length - 1] != '\0') {
            printf("NFC compatible metadata invalid for %s\n", entry->d_name);
            incomplete = 1;
            continue;
        }
        while (offset < (size_t)length) {
            size_t part = strnlen((char *)value + offset, (size_t)length - offset);
            if (part == strlen(compatible) &&
                !memcmp(value + offset, compatible, part)) matches = 1;
            offset += part + 1;
        }
        if (!matches) continue;
        found++;
        snprintf(path, sizeof(path), "%s/%s/driver", root, entry->d_name);
        length = readlink(path, driver, sizeof(driver) - 1);
        if (length < 0) {
            printf("NFC I2C device=%s driver=unbound-or-unavailable\n", entry->d_name);
        } else if (length == (ssize_t)sizeof(driver) - 1) {
            printf("NFC I2C device=%s driver=path-too-long\n", entry->d_name);
            incomplete = 1;
        } else {
            const char *name;
            driver[length] = '\0';
            name = strrchr(driver, '/');
            printf("NFC I2C device=%s driver=%s\n", entry->d_name,
                   name ? name + 1 : driver);
        }
        for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
            unsigned int cell = i ? 1 : 0;
            uint32_t number;
            if (snprintf(path, sizeof(path), "%s/%s/of_node/%s", root,
                         entry->d_name, properties[i]) >= (int)sizeof(path)) {
                incomplete = 1;
                continue;
            }
            length = nfc_read_property(path, value, sizeof(value));
            if (length != (i ? 12 : 4)) {
                printf("NFC %s %s=unavailable-or-invalid\n", entry->d_name, properties[i]);
                incomplete = 1;
                continue;
            }
            number = ((uint32_t)value[cell * 4] << 24) |
                     ((uint32_t)value[cell * 4 + 1] << 16) |
                     ((uint32_t)value[cell * 4 + 2] << 8) | value[cell * 4 + 3];
            if (!i) printf("NFC %s reg=0x%x\n", entry->d_name, number);
            else printf("NFC %s %s=gpio%u (DT assignment, not pin level)\n",
                        entry->d_name, properties[i], number);
        }
    }
    if (dir) closedir(dir);
    printf("NFC DT matches=%u scan=%s\n", found, incomplete ? "incomplete" : "finished");
    dir = opendir(class_root);
    unsigned int registered = 0;
    scanned = 0;
    if (!dir) printf("NFC class=%s unavailable: %s\n", class_root, strerror(errno));
    while (dir && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (scanned++ == 256) {
            puts("NFC class scan truncated after 256 entries");
            break;
        }
        if (strncmp(entry->d_name, "nfc", 3) || !entry->d_name[3] ||
            strspn(entry->d_name + 3, "0123456789") != strlen(entry->d_name + 3)) continue;
        printf("NFC class device=%s (registration metadata only)\n", entry->d_name);
        registered++;
    }
    if (dir) closedir(dir);
    printf("NFC class devices=%u (all controllers, not a transport test)\n", registered);
    puts("NFC transport/IRQ/wakeup/recovery and reader/HCE/eSE functionality: not tested");
    puts("Interface: Linux NFC/NCI; generic netlink NFC_GENL_NAME and AF_NFC; no private character ABI");
}
