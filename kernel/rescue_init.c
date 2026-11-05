// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <unistd.h>

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

int main(void) {
    (void)setsid();
    mount_virtual("devtmpfs", "/dev", "devtmpfs", MS_NOSUID);
    mount_virtual("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    mount_virtual("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC);

    for (;;) {
        char line[64];
        puts("\nMiSeed Umi/CSOT RAM diagnostic. No storage is mounted."
             "\nCommands: help, version, memory, cmdline, mounts, reboot");
        fputs("miseed> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { sleep(1); continue; }
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "help")) continue;
        if (!strcmp(line, "version")) print_file("/proc/version");
        else if (!strcmp(line, "memory")) print_file("/proc/meminfo");
        else if (!strcmp(line, "cmdline")) print_file("/proc/cmdline");
        else if (!strcmp(line, "mounts")) print_file("/proc/mounts");
        else if (!strcmp(line, "reboot")) {
            puts("Reboot requested.");
            sync();
            (void)reboot(RB_AUTOBOOT);
        } else if (*line) puts("Unknown command; type help.");
    }
}
