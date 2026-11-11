// SPDX-License-Identifier: AGPL-3.0-only
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int fake_klogctl(int action, char *buffer, int size);
#define klogctl fake_klogctl
#include "../kernel/rescue_display.c"
#undef klogctl
static int writes, calls, mode;
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
int main(int argc, char **argv) {
    assert(argc == 3);
    mode = atoi(argv[2]);
    if (!strcmp(argv[1], "kmsg")) {
        export_kernel_log();
        assert(calls == (mode ? 1 : 2));
        return 0;
    }
    int rc = display_brightness_at(argv[1], mode == 5 ? 2048 : 1500);
    printf("TEST writes=%d rc=%d\n", writes, rc);
    return 0;
}
