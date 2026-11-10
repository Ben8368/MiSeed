// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

int write_value(const char *path, const char *value) {
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

int setup_usb_acm(void) {
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

void wait_for_usb_acm(void) {
    puts("Waiting for USB device controller; discovery will be retried.");
    while (bind_usb_acm()) sleep(1);
}

void request_bootloader(void) {
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

int reopen_usb_console(void) {
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
