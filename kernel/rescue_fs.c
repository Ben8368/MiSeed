// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

int print_file(const char *path) {
    FILE *file = fopen(path, "r");
    char buffer[512];
    if (!file) {
        printf("cannot read %s: %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(buffer, sizeof(buffer), file)) fputs(buffer, stdout);
    int failed = ferror(file) || ferror(stdout);
    if (fclose(file)) failed = 1;
    return failed ? -1 : 0;
}

void report_dt_string(const char *label, const char *path) {
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

void report_dt_u32(const char *label, const char *path) {
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

int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    fprintf(stderr, "mkdir %s failed: %s\n", path, strerror(errno));
    return -1;
}

int node_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void report_path(const char *path) {
    struct stat st;
    if (stat(path, &st)) {
        fprintf(stderr, "USB path missing %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(stderr, "USB path present %s (mode %o)\n", path,
            (unsigned int)(st.st_mode & 07777));
}

void list_directory(const char *label, const char *path) {
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

int contains_case_insensitive(const char *text, const char *needle) {
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
