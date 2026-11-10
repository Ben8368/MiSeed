/* SPDX-License-Identifier: AGPL-3.0-only */
/* RAM-only recorder shared by PID 1 and its reconnectable USB console. */
#ifndef MISEED_CHARGE_LOG_H
#define MISEED_CHARGE_LOG_H

#include <inttypes.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <time.h>

#ifndef CHARGE_LOG_INITIAL_CAPACITY
#ifdef CHARGE_LOG_CAPACITY
#define CHARGE_LOG_INITIAL_CAPACITY CHARGE_LOG_CAPACITY
#else
#define CHARGE_LOG_INITIAL_CAPACITY 8192U
#endif
#endif
#define CHARGE_LOG_LINE 768
#ifndef CHARGE_LOG_MAX_BYTES
#define CHARGE_LOG_MAX_BYTES (64U * 1024U * 1024U)
#endif

struct charge_log_header {
    uint64_t total;
    uint64_t capacity;
    uint64_t storage_errors;
    char boot_id[40];
};

struct charge_log_record { char text[CHARGE_LOG_LINE]; };
static int charge_log_fd = -1;
static int charge_kmsg_fd = -1;
static int charge_screen_fd = -1;

/* POSIX locks are process-associated, including on an inherited memfd. */
static int charge_log_lock(short type) {
    struct flock lock = { .l_type = type, .l_whence = SEEK_SET };
    int rc;
    do { rc = fcntl(charge_log_fd, F_SETLKW, &lock); } while (rc < 0 && errno == EINTR);
    return rc;
}

static int charge_log_io(void *data, size_t length, off_t offset, int writing) {
    char *cursor = data;
    while (length) {
        ssize_t n = writing ? pwrite(charge_log_fd, cursor, length, offset) :
                              pread(charge_log_fd, cursor, length, offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        cursor += n;
        offset += n;
        length -= (size_t)n;
    }
    return 0;
}

static uint64_t charge_boot_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_BOOTTIME, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int charge_log_init(void) {
    struct charge_log_header header = {
        .capacity = CHARGE_LOG_INITIAL_CAPACITY,
        .boot_id = "unavailable"
    };
    int fd;
    ssize_t n;
    charge_log_fd = memfd_create("miseed-charge-log", MFD_CLOEXEC);
    if (charge_log_fd < 0) return -1;
    if (ftruncate(charge_log_fd, sizeof(header) +
                  CHARGE_LOG_INITIAL_CAPACITY * sizeof(struct charge_log_record))) goto fail;
    fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        n = read(fd, header.boot_id, sizeof(header.boot_id) - 1);
        close(fd);
        if (n > 0) {
            header.boot_id[n] = '\0';
            header.boot_id[strcspn(header.boot_id, "\r\n")] = '\0';
        }
    }
    if (charge_log_io(&header, sizeof(header), 0, 1)) goto fail;
    return 0;
fail:
    close(charge_log_fd);
    charge_log_fd = -1;
    return -1;
}

static void charge_log_append(const char *kind, const char *format, ...) {
    struct charge_log_header header;
    struct charge_log_record record = { { 0 } };
    char message[CHARGE_LOG_LINE - 100];
    va_list args;
    int rc, n;
    off_t offset;
    if (charge_log_fd < 0 || charge_log_lock(F_WRLCK)) return;
    if (charge_log_io(&header, sizeof(header), 0, 0)) goto unlock;
    va_start(args, format);
    n = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (n < 0) goto unlock;
    if ((size_t)n >= sizeof(message)) {
        static const char suffix[] = " [truncated]";
        memcpy(message + sizeof(message) - sizeof(suffix), suffix, sizeof(suffix));
    }
    /* Escape terminal controls and keep one event per line. */
    for (char *p = message; *p; p++) {
        if ((unsigned char)*p < 32 || (unsigned char)*p == 127) *p = ' ';
    }
    snprintf(record.text, sizeof(record.text), "seq=%" PRIu64 " boot_ms=%" PRIu64
             " %s %s\n", header.total, charge_boot_ms(), kind, message);
    if (header.total == header.capacity) {
        uint64_t new_capacity = header.capacity <= UINT64_MAX / 2 ?
                                header.capacity * 2 : UINT64_MAX;
        uint64_t max_capacity = ((uint64_t)CHARGE_LOG_MAX_BYTES - sizeof(header)) /
                                sizeof(record);
        if (new_capacity > max_capacity) new_capacity = max_capacity;
        if (new_capacity <= header.capacity ||
            ftruncate(charge_log_fd, (off_t)(sizeof(header) +
                      new_capacity * sizeof(record)))) {
            header.storage_errors++;
            (void)charge_log_io(&header, sizeof(header), 0, 1);
            goto unlock;
        }
        header.capacity = new_capacity;
    }
    offset = sizeof(header) + header.total * sizeof(record);
    rc = charge_log_io(&record, sizeof(record), offset, 1);
    if (!rc) {
        header.total++;
        (void)charge_log_io(&header, sizeof(header), 0, 1);
    } else {
        header.storage_errors++;
        (void)charge_log_io(&header, sizeof(header), 0, 1);
    }
unlock:
    (void)charge_log_lock(F_UNLCK);
}

/* Published records are append-only: capture bounds while locked, then read
 * their immutable prefix with one record of memory and no lock during USB I/O.
 */
static void charge_log_export(unsigned int recent) {
    struct charge_log_header header;
    struct charge_log_record record;
    uint64_t first, count, i;
    if (charge_log_fd < 0) { puts("CHARGE_LOG_UNAVAILABLE recorder_not_started"); return; }
    if (charge_log_lock(F_RDLCK)) { puts("CHARGE_LOG_UNAVAILABLE lock_failed"); return; }
    int failed = charge_log_io(&header, sizeof(header), 0, 0);
    (void)charge_log_lock(F_UNLCK);
    if (failed) { puts("CHARGE_LOG_UNAVAILABLE header_read_failed"); return; }
    count = header.total;
    if (recent && count > recent) count = recent;
    first = header.total - count;
    /* Preflight the tail before publishing BEGIN; no retained record changes. */
    if (count && charge_log_io(&record, sizeof(record),
            sizeof(header) + (header.total - 1) * sizeof(record), 0)) {
        puts("CHARGE_LOG_UNAVAILABLE snapshot_read_failed"); return;
    }
    printf("CHARGE_LOG_BEGIN boot_id=%s first=%" PRIu64 " next=%" PRIu64
           " count=%" PRIu64 " storage_errors=%" PRIu64 " ram_only=1\n",
           header.boot_id, first, header.total, count, header.storage_errors);
    for (i = 0; i < count; i++) {
        if (charge_log_io(&record, sizeof(record),
                sizeof(header) + (first + i) * sizeof(record), 0)) {
            puts("CHARGE_LOG_UNAVAILABLE snapshot_read_failed"); return;
        }
        record.text[CHARGE_LOG_LINE - 1] = '\0';
        if (fputs(record.text, stdout) == EOF) return;
    }
    if (!ferror(stdout)) printf("CHARGE_LOG_END next=%" PRIu64 "\n", header.total);
}

static int charge_read_value(const char *path, char *value, size_t size) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    ssize_t n;
    if (fd < 0) return -1;
    do { n = read(fd, value, size - 1); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n <= 0) return -1;
    value[n] = '\0';
    value[strcspn(value, "\r\n")] = '\0';
    return 0;
}

static void charge_snapshot(void) {
    static const char * const attrs[] = {
        "type", "status", "present", "online", "capacity", "temp",
        "voltage_now", "current_now", "health", "usb_type", "charge_type",
        "charge_behaviour", "input_current_limit",
    };
    DIR *dir = opendir("/sys/class/power_supply");
    struct dirent *entry;
    unsigned int supplies = 0;
    char path[512], value[80], line[600];
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            size_t used;
            if (entry->d_name[0] == '.') continue;
            if (strspn(entry->d_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
                != strlen(entry->d_name)) continue;
            if (supplies++ == 8) { charge_log_append("SAMPLE", "supply_limit=8"); break; }
            used = (size_t)snprintf(line, sizeof(line), "supply=%.64s", entry->d_name);
            for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
                int n;
                snprintf(path, sizeof(path), "/sys/class/power_supply/%.255s/%s", entry->d_name, attrs[i]);
                if (charge_read_value(path, value, sizeof(value))) strcpy(value, "unavailable");
                n = snprintf(line + used, sizeof(line) - used, " %s=%s", attrs[i], value);
                if (n < 0 || (size_t)n >= sizeof(line) - used) {
                    static const char suffix[] = " [truncated]";
                    memcpy(line + sizeof(line) - sizeof(suffix), suffix, sizeof(suffix));
                    break;
                }
                used += (size_t)n;
            }
            charge_log_append("SAMPLE", "%s", line);
        }
        closedir(dir);
    }
    if (!supplies) charge_log_append("SAMPLE", "power_supply=unavailable temperature=unavailable SOC=unavailable; see miseed-power hardware observations");

    dir = opendir("/sys/class/udc");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "/sys/class/udc/%.255s/state", entry->d_name);
            if (charge_read_value(path, value, sizeof(value))) strcpy(value, "unavailable");
            charge_log_append("USB", "udc=%.64s state=%s", entry->d_name, value);
            break;
        }
        closedir(dir);
    }
}

static int charge_kernel_message(const char *content) {
    static const char * const terms[] = {
        "miseed-power", "miseed-pps", "charg", "battery", "power_supply", "smb5", "smb2",
        "typec", "tcpm", "pdphy", "dwc3", "usb", "spmi", "pmic",
    };
    for (size_t i = 0; i < sizeof(terms) / sizeof(terms[0]); i++)
        if (contains_case_insensitive(content, terms[i])) return 1;
    return 0;
}

static void charge_kernel_drain(void) {
    char message[8192];
    if (charge_kmsg_fd < 0) return;
    /* Bound each pass so a kernel-message flood cannot starve sampling. */
    for (unsigned int i = 0; i < 128; i++) {
        ssize_t n = read(charge_kmsg_fd, message, sizeof(message) - 1);
        char *content;
        if (n < 0) {
            if (errno == EINTR) { i--; continue; }
            if (errno == EPIPE) { charge_log_append("LOSS", "kernel_ring_overrun"); continue; }
            if (errno != EAGAIN) charge_log_append("ERROR", "kernel_read_errno=%d", errno);
            return;
        }
        if (!n) return;
        message[n] = '\0';
        content = strchr(message, ';');
        content = content ? content + 1 : message;
        if (!charge_kernel_message(content)) continue;
        /* Preserve original kernel sequence/time prefix, separate from collector time. */
        charge_log_append("KERNEL", "%s", message);
        if (charge_screen_fd >= 0 && contains_case_insensitive(content, "miseed-power")) {
            /* Independent, nonblocking display output; never the USB tty. */
            ssize_t sent = write(charge_screen_fd, content, strlen(content));
            (void)sent;
        }
    }
}

#endif
