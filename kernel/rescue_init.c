// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <stdint.h>
#include <signal.h>
#include <sys/wait.h>

static void mount_virtual(const char *source, const char *target,
                          const char *type, unsigned long flags) {
    if (mount(source, target, type, flags, NULL) && errno != EBUSY)
        fprintf(stderr, "mount %s failed: %s\n", target, strerror(errno));
}

static void print_file(const char *path) {
    FILE *file = fopen(path, "r");
    char buffer[512];
    if (!file) {
        printf("cannot read %s: %s\n", path, strerror(errno));
        return;
    }
    while (fgets(buffer, sizeof(buffer), file)) fputs(buffer, stdout);
    fclose(file);
}

static void report_dt_string(const char *label, const char *path) {
    char value[256];
    ssize_t length;
    size_t i;
    int error;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        printf("DT %s unavailable: %s\n", label, strerror(errno));
        return;
    }
    length = read(fd, value, sizeof(value) - 1);
    error = errno;
    close(fd);
    if (length < 0) {
        printf("DT %s read failed: %s\n", label, strerror(error));
        return;
    }
    for (i = 0; i < (size_t)length; i++) {
        unsigned char ch = (unsigned char)value[i];
        value[i] = ch == '\0' ? ' ' : (isprint(ch) ? (char)ch : '?');
    }
    value[length] = '\0';
    printf("DT %s=%s\n", label, value);
}

static void report_dt_u32(const char *label, const char *path) {
    unsigned char value[4];
    uint32_t number;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t length;

    if (fd < 0) {
        printf("DT %s unavailable: %s\n", label, strerror(errno));
        return;
    }
    length = read(fd, value, sizeof(value));
    close(fd);
    if (length != (ssize_t)sizeof(value)) {
        printf("DT %s read failed\n", label);
        return;
    }
    number = ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
             ((uint32_t)value[2] << 8) | (uint32_t)value[3];
    printf("DT %s=0x%08x\n", label, number);
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    fprintf(stderr, "mkdir %s failed: %s\n", path, strerror(errno));
    return -1;
}

static int node_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void report_path(const char *path) {
    struct stat st;
    if (stat(path, &st)) {
        fprintf(stderr, "USB path missing %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(stderr, "USB path present %s (mode %o)\n", path,
            (unsigned int)(st.st_mode & 07777));
}

static void list_directory(const char *label, const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) {
        fprintf(stderr, "%s directory unavailable %s: %s\n", label, path,
                strerror(errno));
        return;
    }
    printf("%s %s:", label, path);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') printf(" %s", entry->d_name);
    }
    putchar('\n');
    closedir(dir);
}

static int is_usb_probe_device(const char *name) {
    return strstr(name, "a6f8800") != NULL ||
           strstr(name, "a600000") != NULL ||
           strstr(name, "88e3000") != NULL;
}

static int contains_case_insensitive(const char *text, const char *needle) {
    size_t needle_length = strlen(needle);

    for (; *text; text++) {
        size_t i;
        for (i = 0; i < needle_length; i++) {
            if (!text[i] || tolower((unsigned char)text[i]) !=
                            tolower((unsigned char)needle[i]))
                break;
        }
        if (i == needle_length) return 1;
    }
    return 0;
}

#include "charge_log.h"

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

static void report_rpmh_kernel_messages(void) {
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

static void report_usb_probe_devices(void) {
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

static int report_platform_driver(const char *label, const char *compatible) {
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

static void report_i2c_devices(void) {
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

static void report_spmi_devices(void) {
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

static void report_spmi_dt_nodes(void) {
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

static void report_runtime_status(void) {
    report_platform_driver("PDC", "qcom,sm8250-pdc");
    report_platform_driver("RPMh RSC", "qcom,rpmh-rsc");
    report_platform_driver("RPMh power-domain provider", "qcom,sm8250-rpmhpd");
    list_directory("RPMh power-domain driver",
                   "/sys/bus/platform/drivers/qcom-rpmhpd");
    report_rpmh_kernel_messages();
    report_dt_u32("PON mode-bootloader",
        "/sys/firmware/devicetree/base/soc@0/spmi@c440000/pmic@0/pon@800/mode-bootloader");
    report_platform_driver("SPMI controller", "qcom,spmi-pmic-arb");
    report_platform_driver("PON", "qcom,pm8998-pon");
    report_i2c_devices();
    list_directory("USB UDC entries", "/sys/class/udc");
    report_spmi_dt_nodes();
    report_spmi_devices();
    print_file("/sys/kernel/reboot/mode");
    report_usb_probe_devices();
}

static int write_value(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    size_t left = strlen(value);
    const char *p = value;
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        return -1;
    }
    while (left) {
        ssize_t n = write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            fprintf(stderr, "write %s failed: %s\n", path,
                    n < 0 ? strerror(errno) : "short write");
            close(fd);
            return -1;
        }
        p += n;
        left -= (size_t)n;
    }
    close(fd);
    return 0;
}

static int setup_usb_acm(void) {
    const char *root = "/sys/kernel/config/usb_gadget/miseed";
    const char *config = "/sys/kernel/config/usb_gadget/miseed/configs/c.1";
    const char *function = "/sys/kernel/config/usb_gadget/miseed/functions/acm.usb0";

    if (ensure_dir("/sys/kernel/config") ||
        (mount("configfs", "/sys/kernel/config", "configfs", MS_NOSUID, NULL) &&
         errno != EBUSY)) {
        fprintf(stderr, "mount configfs failed: %s\n", strerror(errno));
        return -1;
    }
    if (ensure_dir("/sys/kernel/config/usb_gadget") || ensure_dir(root) ||
        ensure_dir("/sys/kernel/config/usb_gadget/miseed/strings") ||
        ensure_dir("/sys/kernel/config/usb_gadget/miseed/strings/0x409") ||
        ensure_dir("/sys/kernel/config/usb_gadget/miseed/configs") ||
        ensure_dir(config) || ensure_dir("/sys/kernel/config/usb_gadget/miseed/configs/c.1/strings") ||
        ensure_dir("/sys/kernel/config/usb_gadget/miseed/configs/c.1/strings/0x409") ||
        ensure_dir("/sys/kernel/config/usb_gadget/miseed/functions") || ensure_dir(function))
        return -1;

    if (write_value("/sys/kernel/config/usb_gadget/miseed/idVendor", "0x1d6b") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/idProduct", "0x0104") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/bcdUSB", "0x0200") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/bcdDevice", "0x0100") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/strings/0x409/manufacturer", "MiSeed") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/strings/0x409/product", "Umi CSOT USB diagnostic") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/strings/0x409/serialnumber", "miseed-umi-csot") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/configs/c.1/strings/0x409/configuration", "ACM diagnostic console") ||
        write_value("/sys/kernel/config/usb_gadget/miseed/functions/acm.usb0/console", "1"))
        return -1;

    list_directory("USB entries", "/sys/kernel/config/usb_gadget/miseed/functions");
    list_directory("USB entries", "/sys/kernel/config/usb_gadget/miseed/configs/c.1");
    report_dt_string("dwc3 compatible",
        "/sys/firmware/devicetree/base/soc@0/usb@a6f8800/usb@a600000/compatible");
    report_dt_string("dwc3 dr_mode",
        "/sys/firmware/devicetree/base/soc@0/usb@a6f8800/usb@a600000/dr_mode");
    report_dt_string("dwc3 status",
        "/sys/firmware/devicetree/base/soc@0/usb@a6f8800/usb@a600000/status");
    report_runtime_status();
    if (symlink(function,
                "/sys/kernel/config/usb_gadget/miseed/configs/c.1/acm.usb0") && errno != EEXIST) {
        fprintf(stderr, "link ACM function failed: %s\n", strerror(errno));
        report_path(function);
        report_path(config);
        return -1;
    }
    puts("USB ACM function linked into config c.1");
    list_directory("USB entries", config);

    return 0;
}

static int bind_usb_acm(void) {
    DIR *dir = opendir("/sys/class/udc");
    struct dirent *entry;
    char udc[128] = { 0 };
    if (!dir) {
        fprintf(stderr, "open UDC list failed: %s\n", strerror(errno));
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            size_t n = strnlen(entry->d_name, sizeof(udc));
            if (n == sizeof(udc)) continue;
            memcpy(udc, entry->d_name, n + 1);
            break;
        }
    }
    closedir(dir);
    if (!udc[0]) return -1;
    if (write_value("/sys/kernel/config/usb_gadget/miseed/UDC", udc))
        return -1;
    printf("USB CDC-ACM diagnostic gadget bound to %s\n", udc);
    return 0;
}

static void wait_for_usb_acm(void) {
    puts("Waiting for USB device controller; discovery will be retried.");
    while (bind_usb_acm()) sleep(1);
}

static void request_bootloader(void) {
    static const char target[] = "bootloader";
    if (!report_platform_driver("PON", "qcom,pm8998-pon")) {
        fputs("Bootloader request refused: PON reboot-mode driver is not bound.\n",
              stderr);
        return;
    }
    long rc = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                      LINUX_REBOOT_CMD_RESTART2, target);
    fprintf(stderr, "bootloader restart request failed: %s\n", strerror(errno));
    (void)rc;
}

static int reopen_usb_console(void) {
    int fd;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    fd = open("/dev/ttyGS0", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 ||
        dup2(fd, STDERR_FILENO) < 0) {
        if (fd > STDERR_FILENO) close(fd);
        return -1;
    }
    if (fd > STDERR_FILENO) close(fd);
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);
    return 0;
}

/* Locate exactly one bound PPS controller; never infer an I2C bus number. */
static int pps_control_path(char *path, size_t size) {
    const char *base = "/sys/bus/i2c/drivers/umi-bq25970-pps";
    DIR *dir = opendir(base);
    struct dirent *entry;
    struct stat st;
    char candidate[512];
    int found = 0;
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        int n = snprintf(candidate, sizeof(candidate), "%s/%s/fastcharge_test", base, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(candidate)) continue;
        if (stat(candidate, &st) || !S_ISREG(st.st_mode)) continue;
        if (++found != 1 || (size_t)n >= size) { found = -1; break; }
        memcpy(path, candidate, (size_t)n + 1);
    }
    closedir(dir);
    if (found != 1) { errno = found < 0 ? EEXIST : ENODEV; return -1; }
    return 0;
}

static int pps_control(const char *value) {
    char path[512];
    int rc = pps_control_path(path, sizeof(path));
    if (!rc) {
        if (value) rc = write_value(path, value);
        else print_file(path);
    }
    if (value) charge_log_append("CONTROL", "pps_control action=%s rc=%d", value, rc);
    return rc;
}

static void usb_console(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    while (reopen_usb_console()) sleep(1);
    for (;;) {
        char line[64];
        puts("\nMiSeed Umi/CSOT USB diagnostic. Storage is not mounted.");
        puts("Commands: help, status, version, memory, cmdline, mounts, charge, charge-log, logs, charge-test, charge-test-10min, charge-stop, pps-status, pps-stop, reboot, bootloader");
        puts("charge/charge-log/logs show read-only samples and the retained RAM timeline.");
        puts("Charging controls: charge-test (60 s), charge-test-10min (600 s), charge-stop.");
        puts("PPS starts automatically on an eligible USB-PD attach when the qualified board profile is enabled; use pps-status or pps-stop.");
        fputs("miseed> ", stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            sleep(1);
            while (reopen_usb_console()) sleep(1);
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "help")) continue;
        if (!strcmp(line, "status")) report_runtime_status();
        else if (!strcmp(line, "version")) print_file("/proc/version");
        else if (!strcmp(line, "memory")) print_file("/proc/meminfo");
        else if (!strcmp(line, "cmdline")) print_file("/proc/cmdline");
        else if (!strcmp(line, "mounts")) print_file("/proc/mounts");
        else if (!strcmp(line, "charge")) charge_log_export(32);
        else if (!strcmp(line, "charge-log") || !strcmp(line, "logs")) charge_log_export(0);
        else if (!strcmp(line, "charge-test") || !strcmp(line, "charge-test-10min")) {
            int extended = !strcmp(line, "charge-test-10min");
            int rc = write_value("/sys/class/power_supply/pm8150b-charger/charge_test",
                                 extended ? "arm-10min" : "arm");
            charge_log_append("CONTROL", "charge_test_arm seconds=%u rc=%d", extended ? 600U : 60U, rc);
            if (rc) puts("Charge test arm failed; inspect logs.");
            else printf("Test armed: disconnect computer, attach charger. Ends on unplug or after %u seconds; LOS limits and safety checks stay active.\n",
                        extended ? 600U : 60U);
        } else if (!strcmp(line, "pps-status")) {
            if (pps_control(NULL)) puts("PPS controller unavailable or ambiguous; board qualification may be incomplete.");
        } else if (!strcmp(line, "pps-stop")) {
            puts(pps_control("stop") ? "PPS stop unavailable/failed; disconnect charger." : "PPS stopped; SMB5 fallback policy resumed.");
        } else if (!strcmp(line, "charge-stop")) {
            /* Also stop PPS when present, before releasing the ordinary charger. */
            char path[512];
            int pps_rc = pps_control_path(path, sizeof(path));
            if (!pps_rc) pps_rc = write_value(path, "stop");
            else if (errno == ENOENT || errno == ENODEV) pps_rc = 0;
            int rc = write_value("/sys/class/power_supply/pm8150b-charger/charge_test", "stop");
            if (pps_rc) rc = pps_rc;
            charge_log_append("CONTROL", "charge_test_stop rc=%d", rc);
            puts(rc ? "Charge stop failed; disconnect the charger." : "Charging inhibited; test cancelled.");
        } else if (!strcmp(line, "reboot")) {
            puts("Normal reboot requested.");
            sync();
            (void)reboot(RB_AUTOBOOT);
        } else if (!strcmp(line, "bootloader")) {
            puts("Requesting bootloader via PMIC PON mode; no storage write.");
            sync();
            request_bootloader();
        } else if (*line) puts("Unknown command; type help.");
    }
}

int main(void) {
    int usb_ready;
    int usb_configured;
    unsigned int tick = 0;
    (void)setsid();
    mount_virtual("devtmpfs", "/dev", "devtmpfs", MS_NOSUID);
    mount_virtual("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    mount_virtual("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    if (!ensure_dir("/sys/kernel/debug"))
        mount_virtual("debugfs", "/sys/kernel/debug", "debugfs",
                      MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RDONLY);

    (void)signal(SIGPIPE, SIG_IGN);
    if (charge_log_init()) fprintf(stderr, "Charge RAM recorder unavailable: %s\n", strerror(errno));
    charge_kmsg_fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (charge_kmsg_fd < 0) charge_log_append("ERROR", "kernel_log_unavailable errno=%d", errno);
    charge_screen_fd = open("/dev/tty0", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    charge_log_append("START", "samples_read_only=1 controls=charge-test,charge-test-10min,charge-stop sample_interval_s=2; USB disconnect does not stop recording; reboot loses RAM log");
    charge_kernel_drain();
    charge_snapshot();

    usb_ready = node_exists("/dev/ttyGS0");
    usb_configured = usb_ready;
    if (usb_ready) {
        puts("Reusing kernel-bound CDC-ACM gadget for early logs and console.");
    } else {
        usb_configured = setup_usb_acm() == 0;
    }
    if (usb_configured) {
        pid_t child;
        fflush(NULL);
        child = fork();
        if (child == 0) {
            if (charge_kmsg_fd >= 0) close(charge_kmsg_fd);
            if (charge_screen_fd >= 0) close(charge_screen_fd);
            if (!usb_ready) wait_for_usb_acm();
            usb_console();
        }
        if (child < 0) fprintf(stderr, "USB console fork failed: %s\n", strerror(errno));
    }
    puts("\nMiSeed Umi/CSOT RAM diagnostic. No storage is mounted.");
    puts("USB ACM console is available when the gadget binds successfully.");
    /* PID 1 records independently of every USB open/read/write and reconnect. */
    for (;;) {
        int status;
        charge_kernel_drain();
        if (++tick % 2 == 0) charge_snapshot();
        if (waitpid(-1, &status, WNOHANG) > 0)
            charge_log_append("ERROR", "USB_console_exited status=%d", status);
        sleep(1);
    }
}
