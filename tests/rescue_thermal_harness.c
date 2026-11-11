// SPDX-License-Identifier: AGPL-3.0-only
static const char *thermal_prefix = "/sys/class/thermal/";

static int test_thermal_path(const char *path) {
    return getenv("TEST_THERMAL_ROOT") &&
           !strncmp(path, thermal_prefix, strlen(thermal_prefix));
}

static char *test_thermal_map(const char *path, char *mapped, size_t size) {
    if (!test_thermal_path(path)) return NULL;
    snprintf(mapped, size, "%s/%s", getenv("TEST_THERMAL_ROOT"),
             path + strlen(thermal_prefix));
    return mapped;
}

static FILE *test_thermal_fopen(const char *path, const char *mode) {
    char mapped[1024];
    return fopen(test_thermal_map(path, mapped, sizeof(mapped)), mode);
}

static int test_thermal_open(const char *path, int flags, mode_t mode) {
    char mapped[1024];
    if (getenv("TEST_THERMAL_WRITE_FAIL") && (flags & O_WRONLY)) {
        errno = EPERM;
        return -1;
    }
    return open(test_thermal_map(path, mapped, sizeof(mapped)), flags, mode);
}

static DIR *test_thermal_opendir(void) {
    return opendir(getenv("TEST_THERMAL_ROOT"));
}

static int run_thermal_case(const char *command) {
    if (!strcmp(command, "thermal-report")) {
        report_thermal_devices();
        return 0;
    }
    else if (!strcmp(command, "thermal-stop")) return set_wired_pd_cooling_state(1) ? 24 : 0;
    else if (!strcmp(command, "thermal-clear")) return set_wired_pd_cooling_state(0) ? 25 : 0;
    return 2;
}
