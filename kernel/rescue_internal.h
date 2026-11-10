// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only diagnostic PID 1 for the first MiSeed Umi/CSOT RAM boot. */
#ifndef MISEED_RESCUE_INTERNAL_H
#define MISEED_RESCUE_INTERNAL_H

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <stdint.h>
#include <signal.h>
#include <sys/wait.h>

#include "charge_log.h"

int print_file(const char *path);
void report_dt_string(const char *label, const char *path);
void report_dt_u32(const char *label, const char *path);
int ensure_dir(const char *path);
int node_exists(const char *path);
void report_path(const char *path);
void list_directory(const char *label, const char *path);
int contains_case_insensitive(const char *text, const char *needle);
void report_rpmh_kernel_messages(void);
void report_usb_probe_devices(void);
int report_platform_driver(const char *label, const char *compatible);
void report_i2c_devices(void);
void report_spmi_devices(void);
void report_spmi_dt_nodes(void);
void report_power_supply_diagnostics(void);
void report_tcpm_logs(void);
int pps_control_path(char *path, size_t size);
int pps_control(const char *value);
void report_nfc_status(const char *root, const char *class_root);
int write_value(const char *path, const char *value);
int setup_usb_acm(void);
void wait_for_usb_acm(void);
void request_bootloader(void);
int reopen_usb_console(void);
void report_runtime_status(void);
int charge_log_init(void);
void charge_log_append(const char *kind, const char *format, ...);
void charge_log_export(unsigned int recent);
void charge_snapshot(void);
void charge_kernel_drain(void);


#endif
