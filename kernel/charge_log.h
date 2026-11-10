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

extern int charge_kmsg_fd, charge_screen_fd;

int charge_log_init(void);
void charge_log_append(const char *kind, const char *format, ...);
void charge_log_export(unsigned int recent);
void charge_snapshot(void);
void charge_kernel_drain(void);

#endif
