// SPDX-License-Identifier: AGPL-3.0-only
/* Wired fixed-PD thermal diagnostics and standard cooling-device controls. */
#define _GNU_SOURCE
#include "rescue_internal.h"

static int read_sysfs_line(const char *path, char *value, size_t size) {
    FILE *file = fopen(path, "r");
    size_t length;

    if (!file) return -1;
    if (!fgets(value, size, file)) {
        fclose(file);
        return -1;
    }
    if (fclose(file)) return -1;
    length = strcspn(value, "\r\n");
    value[length] = '\0';
    return 0;
}

static int read_sysfs_integer(const char *path, long *value) {
    char text[64], *end;
    long parsed;

    if (read_sysfs_line(path, text, sizeof(text))) return -1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || end == text || *end) return -1;
    *value = parsed;
    return 0;
}

static int wired_pd_cooling_device(char *path, size_t size,
                                   long *state, long *max_state) {
    DIR *dir = opendir("/sys/class/thermal");
    struct dirent *entry;
    unsigned int found = 0;

    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char type_path[256], state_path[256], max_path[256], type[64];
        size_t i;
        int n;

        if (strncmp(entry->d_name, "cooling_device", 14)) continue;
        for (i = 14; entry->d_name[i]; i++)
            if (!isdigit((unsigned char)entry->d_name[i])) break;
        if (!entry->d_name[14] || entry->d_name[i]) continue;
        n = snprintf(type_path, sizeof(type_path),
                     "/sys/class/thermal/%s/type", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(type_path) ||
            read_sysfs_line(type_path, type, sizeof(type)) ||
            strcmp(type, "umi-wired-pd"))
            continue;
        if (++found != 1) break;
        n = snprintf(path, size, "/sys/class/thermal/%s/cur_state", entry->d_name);
        if (n < 0 || (size_t)n >= size ||
            snprintf(state_path, sizeof(state_path), "/sys/class/thermal/%s/cur_state",
                     entry->d_name) >= (int)sizeof(state_path) ||
            snprintf(max_path, sizeof(max_path), "/sys/class/thermal/%s/max_state",
                     entry->d_name) >= (int)sizeof(max_path) ||
            read_sysfs_integer(state_path, state) ||
            read_sysfs_integer(max_path, max_state)) {
            found = 0;
            break;
        }
    }
    closedir(dir);
    if (found != 1 || *max_state != 1 || *state < 0 || *state > *max_state)
        return -1;
    return 0;
}

void report_thermal_devices(void) {
    DIR *dir = opendir("/sys/class/thermal");
    struct dirent *entry;
    unsigned int count = 0;

    if (!dir) {
        printf("Thermal sysfs unavailable: %s\n", strerror(errno));
        return;
    }
    while ((entry = readdir(dir)) && count < 128) {
        char type_path[256], value_path[256], type[64], value[64];
        int n;

        if (!strncmp(entry->d_name, "thermal_zone", 12)) {
            n = snprintf(type_path, sizeof(type_path),
                         "/sys/class/thermal/%s/type", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(type_path) ||
                read_sysfs_line(type_path, type, sizeof(type)))
                strcpy(type, "unavailable");
            n = snprintf(value_path, sizeof(value_path),
                         "/sys/class/thermal/%s/temp", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(value_path) ||
                read_sysfs_line(value_path, value, sizeof(value)))
                strcpy(value, "unavailable");
            printf("THERMAL_ZONE name=%s type=%s temp_millic=%s\n",
                   entry->d_name, type, value);
            count++;
        } else if (!strncmp(entry->d_name, "cooling_device", 14)) {
            char state_path[256], max_path[256];
            long state, max_state;

            n = snprintf(type_path, sizeof(type_path),
                         "/sys/class/thermal/%s/type", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(type_path) ||
                read_sysfs_line(type_path, type, sizeof(type)))
                strcpy(type, "unavailable");
            n = snprintf(state_path, sizeof(state_path),
                         "/sys/class/thermal/%s/cur_state", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(state_path) ||
                read_sysfs_integer(state_path, &state))
                state = -1;
            n = snprintf(max_path, sizeof(max_path),
                         "/sys/class/thermal/%s/max_state", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(max_path) ||
                read_sysfs_integer(max_path, &max_state))
                max_state = -1;
            printf("COOLING_DEVICE name=%s type=%s cur_state=%ld max_state=%ld\n",
                   entry->d_name, type, state, max_state);
            count++;
        }
    }
    if (count == 128) puts("Thermal sysfs listing truncated at 128 entries");
    closedir(dir);
}

int set_wired_pd_cooling_state(long requested) {
    char path[256];
    long old_state = -1, max_state = -1, readback = -1;
    int ret;

    ret = requested > 1 ? -1 :
        wired_pd_cooling_device(path, sizeof(path), &old_state, &max_state);
    if (!ret && old_state != requested)
        ret = write_value(path, requested ? "1" : "0");
    if (!ret && (read_sysfs_integer(path, &readback) || readback != requested))
        ret = -1;
    charge_log_append("CONTROL",
        "wired_pd_cooling old=%ld requested=%ld readback=%ld max=%ld rc=%d",
        old_state, requested, readback, max_state, ret);
    printf("WIRED_PD_COOLING old_state=%ld requested_state=%ld readback=%ld max_state=%ld rc=%d\n",
           old_state, requested, readback, max_state, ret);
    return ret;
}
