// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#define _GNU_SOURCE
#include "rescue_internal.h"

void report_power_supply_diagnostics(void) {
    static const char *const properties[] = {
        "type", "online", "status", "usb_type", "voltage_now",
        "current_now", "voltage_max", "current_max", "pd_current_max",
        "input_current_limit", "charge_type", "scope"
    };
    const char *root = "/sys/class/power_supply";
    DIR *dir = opendir(root);
    struct dirent *entry;
    unsigned int count = 0;

    if (!dir) {
        printf("Power supply list unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        size_t i;
        int found = 0;

        if (entry->d_name[0] == '.') continue;
        if (count++ == 32) {
            puts("Power supply list truncated after 32 entries");
            break;
        }
        printf("Power supply %s:", entry->d_name);
        for (i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
            char path[512];
            char value[160];
            ssize_t length;
            size_t j;
            int fd;

            if (snprintf(path, sizeof(path), "%s/%s/%s", root,
                         entry->d_name, properties[i]) >= (int)sizeof(path))
                continue;
            fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) continue;
            length = read(fd, value, sizeof(value) - 1);
            close(fd);
            if (length <= 0) continue;
            value[length] = '\0';
            while (length > 0 && (value[length - 1] == '\n' ||
                                  value[length - 1] == '\r'))
                value[--length] = '\0';
            for (j = 0; j < (size_t)length; j++)
                if (!isprint((unsigned char)value[j])) value[j] = '?';
            printf("%s %s=%s", found ? "," : "", properties[i], value);
            found = 1;
        }
        puts(found ? "" : " no diagnostic properties");
    }
    closedir(dir);
}

/* TCPM's debugfs reader advances its in-kernel ring buffer. Keep this behind
 * an explicit console command so routine status sampling does not consume it.
 */
void report_tcpm_logs(void) {
    const char *root = "/sys/kernel/debug/usb";
    DIR *dir = opendir(root);
    struct dirent *entry;
    unsigned int ports = 0;

    if (!dir) {
        printf("TCPM debug log unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        char line[512];
        FILE *file;
        int fd;
        uint64_t lines = 0;

        if (strncmp(entry->d_name, "tcpm-", 5)) continue;
        if (ports++ == 8) {
            puts("TCPM log listing truncated after 8 ports");
            break;
        }
        if (snprintf(path, sizeof(path), "%s/%s/log", root,
                     entry->d_name) >= (int)sizeof(path))
            continue;
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            printf("TCPM log %s unavailable: %s\n", entry->d_name,
                   strerror(errno));
            continue;
        }
        file = fdopen(fd, "r");
        if (!file) {
            int error = errno;
            close(fd);
            printf("TCPM log %s unavailable: %s\n", entry->d_name,
                   strerror(error));
            continue;
        }
        printf("TCPM protocol log %s (read drains kernel ring):\n",
               entry->d_name);
        printf("TCPM_LOG_BEGIN port=%s\n", entry->d_name);
        while (fgets(line, sizeof(line), file)) {
            fputs(line, stdout);
            lines++;
        }
        if (ferror(file)) puts("TCPM log read failed; snapshot incomplete");
        if (!lines) puts("TCPM log empty");
        printf("TCPM_LOG_END port=%s lines=%" PRIu64 " complete=%u\n", entry->d_name,
               lines, (unsigned int)!ferror(file));
        fclose(file);
    }
    closedir(dir);
    if (!ports) puts("No TCPM debug log ports found");
}

/* Locate exactly one wired PPS controller; never infer an I2C bus number. */
int pps_control_path(char *path, size_t size) {
    const char *base = "/sys/class/power_supply";
    DIR *dir = opendir(base);
    struct dirent *entry;
    struct stat st;
    char candidate[512];
    int found = 0;
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char model[64];
        FILE *identity;
        if (entry->d_name[0] == '.') continue;
        int n = snprintf(candidate, sizeof(candidate), "%s/%s/model_name", base, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(candidate)) continue;
        identity = fopen(candidate, "r");
        if (!identity) continue;
        int matches = fgets(model, sizeof(model), identity) &&
                      !strcmp(model, "BQ25970\n");
        fclose(identity);
        if (!matches) continue;
        n = snprintf(candidate, sizeof(candidate), "%s/%s/charge_behaviour", base, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(candidate)) continue;
        if (stat(candidate, &st) || !S_ISREG(st.st_mode)) continue;
        if (++found != 1 || (size_t)n >= size) { found = -1; break; }
        memcpy(path, candidate, (size_t)n + 1);
    }
    closedir(dir);
    if (found != 1) { errno = found < 0 ? EEXIST : ENODEV; return -1; }
    return 0;
}

int pps_control(const char *value) {
    char path[512];
    int rc = pps_control_path(path, sizeof(path));
    if (!rc) {
        if (value) rc = write_value(path, value);
        else {
            char *attribute = strrchr(path, '/');
            if (!attribute) return -1;
            strcpy(attribute + 1, "uevent");
            rc = print_file(path);
        }
    }
    if (value) charge_log_append("CONTROL", "pps_control action=%s rc=%d", value, rc);
    return rc;
}
