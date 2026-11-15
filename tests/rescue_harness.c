// SPDX-License-Identifier: AGPL-3.0-only
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define CHARGE_LOG_CAPACITY 8
#define CHARGE_LOG_MAX_BYTES (sizeof(struct charge_log_header) + 16 * sizeof(struct charge_log_record))
static int test_open(const char *path, int flags, ...);
static DIR *test_opendir(const char *path);
static int test_stat(const char *path, struct stat *st);
static ssize_t test_read(int fd, void *buffer, size_t size);
static ssize_t test_readlink(const char *path, char *buffer, size_t size);
static FILE *test_fopen(const char *path, const char *mode);
static unsigned int test_sleep(unsigned int seconds);
static long test_reboot_syscall(long number, ...);
#define open test_open
#define opendir test_opendir
#define stat(...) test_stat(__VA_ARGS__)
#define read test_read
#define readlink test_readlink
#define fopen test_fopen
#define sleep test_sleep
#define syscall test_reboot_syscall
#define main miseed_pid1_main
#include "@SOURCE@"
#undef main
#undef open
#undef opendir
#undef stat
#undef read
#undef readlink
#undef fopen
#undef sleep
#undef syscall
#include "@THERMAL_HELPERS@"
static int kmsg_fd = -1;
static int delayed_udc;
static int retries;
static int missing_pon;
static int reboot_calls;
static int kmsg_overrun;

static long test_reboot_syscall(long number, ...) {
    (void)number;
    reboot_calls++;
    errno = EPERM;
    return -1;
}
static const char *pps_prefix = "/sys/class/power_supply";
static ssize_t test_readlink(const char *path, char *buffer, size_t size) {
    char mapped[1024];
    if (getenv("TEST_I2C_ROOT") && !strncmp(path, "/sys/bus/i2c/devices/", 21)) {
        snprintf(mapped, sizeof(mapped), "%s/%s", getenv("TEST_I2C_ROOT"), path + 21);
        path = mapped;
    }
    return readlink(path, buffer, size);
}
static FILE *test_fopen(const char *path, const char *mode) {
    char mapped[1024];
    if (test_thermal_path(path)) return test_thermal_fopen(path, mode);
    if (getenv("TEST_PPS_ROOT") && !strncmp(path, pps_prefix, strlen(pps_prefix))) {
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_PPS_ROOT"), path + strlen(pps_prefix));
        path = mapped;
        if (getenv("TEST_PPS_READ_FAIL") && strstr(path, "/uevent")) path = getenv("TEST_PPS_ROOT");
    }
    return fopen(path, mode);
}

static int test_stat(const char *path, struct stat *st) {
    char mapped[1024];
    if (getenv("TEST_PPS_ROOT") && !strncmp(path, pps_prefix, strlen(pps_prefix))) {
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_PPS_ROOT"), path + strlen(pps_prefix));
        return stat(mapped, st);
    }
    if (getenv("TEST_TCPM_ROOT") && !strncmp(path, "/sys/kernel/debug/usb", 21)) {
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_TCPM_ROOT"), path + 21);
        return stat(mapped, st);
    }
    return stat(path, st);
}

static int test_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (!strcmp(path, "/dev/ttyGS0")) path = getenv("TEST_TTY");
    if (!strcmp(path, "/sys/kernel/config/usb_gadget/miseed/UDC"))
        path = getenv("TEST_UDC_BIND");
    if (test_thermal_path(path)) return test_thermal_open(path, flags, mode);
    if (!strcmp(path, "/dev/kmsg")) {
        kmsg_fd = open("/dev/null", flags);
        return kmsg_fd;
    }
    if (!strncmp(path, "/sys/class/power_supply/", 24) && getenv("TEST_POWER_ROOT")) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s/%s", getenv("TEST_POWER_ROOT"), path + 24);
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_I2C_ROOT") && !strncmp(path, "/sys/bus/i2c/devices/", 21)) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s/%s", getenv("TEST_I2C_ROOT"), path + 21);
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_PPS_ROOT") && !strncmp(path, pps_prefix, strlen(pps_prefix))) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_PPS_ROOT"), path + strlen(pps_prefix));
        return open(mapped, flags, mode);
    }
    if (getenv("TEST_TCPM_ROOT") && !strncmp(path, "/sys/kernel/debug/usb", 21)) {
        char mapped[1024];
        snprintf(mapped, sizeof(mapped), "%s%s", getenv("TEST_TCPM_ROOT"), path + 21);
        return open(mapped, flags, mode);
    }
    return open(path, flags, mode);
}

static DIR *test_opendir(const char *path) {
    if (missing_pon && !strcmp(path, "/sys/bus/platform/devices")) {
        errno = ENOENT;
        return NULL;
    }
    if (!strcmp(path, "/sys/class/udc")) path = getenv("TEST_UDC_DIR");
    if (!path) { errno = ENOENT; return NULL; }
    if (!strcmp(path, "/sys/class/thermal")) return test_thermal_opendir();
    if (!strcmp(path, "/sys/class/power_supply") && getenv("TEST_POWER_ROOT"))
        path = getenv("TEST_POWER_ROOT");
    if (getenv("TEST_I2C_ROOT") && !strcmp(path, "/sys/bus/i2c/devices"))
        path = getenv("TEST_I2C_ROOT");
    if (getenv("TEST_PPS_ROOT") && !strcmp(path, pps_prefix)) path = getenv("TEST_PPS_ROOT");
    if (getenv("TEST_TCPM_ROOT") && !strcmp(path, "/sys/kernel/debug/usb"))
        path = getenv("TEST_TCPM_ROOT");
    return opendir(path);
}
static ssize_t test_read(int fd, void *buffer, size_t size) {
    static const char *records[] = {
        "6,1,0,-;dwc3 controller ready\n",
        "6,2,0,-;USB connection ready\n",
        "6,3,0,-;88e3000.PHY ready\n",
        "6,4,0,-;UDC registered\n",
        "6,5,0,-;sync_state() pending due to supplier\n",
        "6,6,0,-;PON ready\n",
        "6,7,0,-;SPMI ready\n",
        "6,8,0,-;PMIC ready\n",
        "6,9,0,-;unrelated failed\n",
        "6,10,0,-;unrelated could not start\n",
        "6,11,0,-;unrelated error\n",
        "3,12,0,-;critical message\n",
        "6,13,0,-;unrelated information\n",
        "6,14,0,-;PDC ready\n",
        "6,15,0,-;PSCI ready\n",
        "6,16,0,-;miseed-power usb_present=1 hw_phase=fullon\n",
        "6,17,0,-;battery temperature unavailable\n",
        "6,18,0,-;rpmh provider ready\n",
    };
    static size_t index;
    if (fd == kmsg_fd) {
        size_t length;
        if (kmsg_overrun) { kmsg_overrun = 0; errno = EPIPE; return -1; }
        if (index == sizeof(records) / sizeof(records[0])) {
            errno = EAGAIN;
            return -1;
        }
        length = strlen(records[index]);
        if (length > size) abort();
        memcpy(buffer, records[index++], length);
        return (ssize_t)length;
    }
    return read(fd, buffer, size);
}

static unsigned int test_sleep(unsigned int seconds) {
    if (delayed_udc) {
        char path[1024];
        int fd;
        if (++retries > 3) exit(9);
        if (retries == 1) {
            snprintf(path, sizeof(path), "%s/a600000.usb", getenv("TEST_UDC_DIR"));
            fd = open(path, O_WRONLY | O_CREAT, 0600);
        } else {
            fd = open(getenv("TEST_UDC_BIND"), O_WRONLY | O_CREAT, 0600);
        }
        if (fd < 0) exit(10);
        close(fd);
        return 0;
    }
    return sleep(seconds);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "logs")) report_usb_kernel_messages();
    else if (!strcmp(argv[1], "rpmh-overrun")) { kmsg_overrun=1; report_rpmh_kernel_messages(); }
    else if (!strcmp(argv[1], "i2c-report")) report_i2c_devices();
    else if (!strcmp(argv[1], "tcpm-log")) report_tcpm_logs();
    else if (!strncmp(argv[1], "thermal-", 8)) return run_thermal_case(argv[1]);
    else if (!strcmp(argv[1], "bind")) {
        delayed_udc = 1;
        wait_for_usb_acm();
        if (retries != 2) return 3;
    } else if (!strcmp(argv[1], "pps-discovery")) {
        char path[512];
        int ret = pps_control_path(path, sizeof(path));
        printf("discovery=%d errno=%d\n", ret, errno);
        if (!ret && pps_control(NULL)) return 23;
    } else if (!strcmp(argv[1], "console")) usb_console();
    else if (!strcmp(argv[1], "console-record")) {
        pid_t child;
        char previous[128] = { 0 };
        if (charge_log_init()) return 10;
        charge_log_append("START", "session");
        child = fork();
        if (child < 0) return 11;
        if (!child) usb_console();
        for (;;) {
            char state[128];
            if (!charge_read_value(getenv("TEST_PHASE_FILE"), state, sizeof(state)) &&
                strcmp(state, previous)) {
                charge_log_append("SAMPLE", "%s", state);
                strcpy(previous, state);
            }
            usleep(100000);
        }
    }
    else if (!strcmp(argv[1], "log-ring")) {
        if (charge_log_init()) return 12;
        for (int i = 0; i < CHARGE_LOG_CAPACITY + 3; i++)
            charge_log_append("SAMPLE", "sample=%d", i);
        charge_log_export(0);
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-limit")) {
        if (charge_log_init()) return 12;
        for (int i=0; i<20; i++) charge_log_append("SAMPLE", "sample=%d", i);
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-unavailable")) charge_log_export(0);
    else if (!strcmp(argv[1], "log-corrupt")) {
        if (charge_log_init()) return 13;
        charge_log_append("SAMPLE", "before truncation");
        if (ftruncate(charge_log_fd, sizeof(struct charge_log_header))) return 14;
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-sample")) {
        if (charge_log_init()) return 15;
        charge_snapshot();
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-kernel")) {
        if (charge_log_init()) return 16;
        charge_kmsg_fd = test_open("/dev/kmsg", O_RDONLY);
        kmsg_overrun = 1;
        charge_kernel_drain();
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "log-blocked-export")) {
        int pipefd[2], status;
        pid_t child;
        char message[680];
        if (charge_log_init()) return 17;
        memset(message, 'x', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        for (int i = 0; i < CHARGE_LOG_CAPACITY; i++) charge_log_append("SAMPLE", "%s", message);
        if (pipe(pipefd) || fcntl(pipefd[1], F_SETPIPE_SZ, 4096) < 0) return 18;
        child = fork();
        if (child < 0) return 19;
        if (!child) {
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(20);
            setvbuf(stdout, NULL, _IONBF, 0);
            charge_log_export(0);
            _exit(0);
        }
        close(pipefd[1]);
        usleep(50000);
        alarm(2);
        charge_log_append("SAMPLE", "recording continued during blocked USB export");
        alarm(0);
        kill(child, SIGTERM);
        waitpid(child, &status, 0);
        close(pipefd[0]);
        charge_log_export(0);
    }
    else if (!strcmp(argv[1], "bootloader-unbound")) {
        missing_pon = 1;
        request_bootloader();
        if (reboot_calls) return 5;
    }
    else return 4;
    return 0;
}
