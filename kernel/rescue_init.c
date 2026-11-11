// SPDX-License-Identifier: AGPL-3.0-only
/* Diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot; no storage is mounted. */
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

static void report_thermal_devices(void) {
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

static int set_wired_pd_cooling_state(long requested) {
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

static void mount_virtual(const char *source, const char *target,
                          const char *type, unsigned long flags) {
    if (mount(source, target, type, flags, NULL) && errno != EBUSY)
        fprintf(stderr, "mount %s failed: %s\n", target, strerror(errno));
}

void report_runtime_status(void) {
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
    report_platform_driver("QUP wrapper", "qcom,geni-se-qup");
    report_i2c_devices();
    report_nfc_status("/sys/bus/i2c/devices", "/sys/class/nfc");
    report_power_supply_diagnostics();
    list_directory("USB UDC entries", "/sys/class/udc");
    report_spmi_dt_nodes();
    report_spmi_devices();
    print_file("/sys/kernel/reboot/mode");
    report_usb_probe_devices();
}

static void usb_console(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    while (reopen_usb_console()) sleep(1);
    for (;;) {
        char line[64];
        puts("\nMiSeed Umi/CSOT USB diagnostic. Storage is not mounted.");
        puts("Commands: help, status, thermal, thermal-stop, thermal-clear, nfc-status, tcpm-log, version, memory, cmdline, mounts, charge, charge-log, logs, charge-test, charge-test-10min, charge-stop, pps-status, pps-stop, reboot, bootloader");
        puts("charge/charge-log/logs show read-only samples and the retained RAM timeline.");
        puts("thermal reads thermal sysfs; thermal-stop/thermal-clear set only umi-wired-pd cooling state 1/0.");
        puts("Wired controls: charge-stop; bounded charge-test commands require a diagnostic kernel.");
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
        else if (!strcmp(line, "thermal")) report_thermal_devices();
        else if (!strcmp(line, "thermal-stop")) (void)set_wired_pd_cooling_state(1);
        else if (!strcmp(line, "thermal-clear")) (void)set_wired_pd_cooling_state(0);
        else if (!strcmp(line, "nfc-status"))
            report_nfc_status("/sys/bus/i2c/devices", "/sys/class/nfc");
        else if (!strcmp(line, "tcpm-log")) report_tcpm_logs();
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
            puts(pps_control("inhibit-charge") ? "PPS stop unavailable/failed; disconnect charger." : "PPS stopped; SMB5 fallback policy resumed.");
        } else if (!strcmp(line, "charge-stop")) {
            /* Also stop PPS when present, before releasing the ordinary charger. */
            char path[512];
            int pps_rc = pps_control_path(path, sizeof(path));
            if (!pps_rc) pps_rc = write_value(path, "inhibit-charge");
            else if (errno == ENOENT || errno == ENODEV) pps_rc = 0;
            int rc = write_value("/sys/class/power_supply/pm8150b-charger/charge_behaviour", "inhibit-charge");
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
    charge_log_append("START", "samples_read_only=1 controls=thermal-stop,thermal-clear,charge-test,charge-test-10min,charge-stop sample_interval_s=2; USB disconnect does not stop recording; reboot loses RAM log");
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
