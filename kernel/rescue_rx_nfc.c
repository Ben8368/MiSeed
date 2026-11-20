// SPDX-License-Identifier: AGPL-3.0-only
/* Bounded RF coexistence experiment. All results survive USB disconnect in RAM. */
#define _GNU_SOURCE
#include "rescue_internal.h"
#include "rescue_nfc_probe.h"

#include <linux/nfc.h>
#include <sys/socket.h>

#define RX_SUPPLY "/sys/class/power_supply/rx1619/"
#define UDC_ROOT "/sys/class/udc/"
#define MIFARE NFC_PROTO_MIFARE_MASK

static int read_token(const char *path, char *text, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t n = read(fd, text, size);
    int rc = n < 0 ? -errno : n == 0 || (size_t)n >= size ? -EBADMSG : 0;
    close(fd);
    if (!rc) {
        text[n] = '\0';
        text[strcspn(text, "\r\n")] = '\0';
    }
    return rc;
}

static int udc_path(char *path, size_t size) {
    DIR *dir = opendir(UDC_ROOT);
    struct dirent *entry;
    int count = 0;
    if (!dir) return -errno;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        if (++count == 1 && snprintf(path, size, UDC_ROOT "%s/state", entry->d_name) >= (int)size)
            count = 2;
    }
    closedir(dir);
    return count == 1 ? 0 : count ? -E2BIG : -ENODEV;
}

static int read_state(const char *udc, bool allow_shared_vbus, bool *detached, bool *online) {
    char state[32], rx[8];
    int rc = read_token(udc, state, sizeof(state));
    int typec = umi_typec_partner();
    if (typec < 0) return typec;
    if (!typec && !rc) rc = umi_typec_detach_evidence(allow_shared_vbus);
    if (!rc) rc = read_token(RX_SUPPLY "online", rx, sizeof(rx));
    if (rc) return rc;
    if (strcmp(rx, "0") && strcmp(rx, "1")) return -EBADMSG;
    *online = rx[0] == '1';
    *detached = typec == 0;
    return 0;
}

static int wait_state(const char *udc, bool want_online, bool wait_for_detach,
                      unsigned int seconds) {
    for (unsigned int i = 0; i < seconds * 4; i++) {
        bool detached, online;
        int rc = read_state(udc, !wait_for_detach, &detached, &online);
        if (rc && rc != -EAGAIN && !(wait_for_detach && rc == -ENOLINK)) return rc;
        if (!rc) {
            if (!detached && !wait_for_detach) return -ENOLINK;
            if (wait_for_detach && online) return -EUCLEAN;
            if (detached && online == want_online) {
                /* Before post-seat RF restart require a return to off-pad VBUS. */
                rc = !want_online && !wait_for_detach ? umi_typec_detach_evidence(false) : 0;
                if (rc != -EAGAIN) return rc;
            }
        }
        usleep(250000);
    }
    return -ETIMEDOUT;
}

static int expect_state(const char *udc, bool want_detached, bool want_online) {
    bool detached, online;
    int rc = read_state(udc, want_detached && want_online, &detached, &online);
    if (rc) return rc;
    if (detached != want_detached) return -ENOLINK;
    return online == want_online ? 0 : -EUCLEAN;
}

static int wait_tag(struct nfc_nl_client *client, uint32_t index, const char *udc,
                    bool detached, struct nfc_target_info *tag) {
    for (unsigned int i = 0; i < 20; i++) {
        int rc = expect_state(udc, detached, false);
        if (!rc) rc = nfc_get_targets(client, index, tag);
        if (rc) return rc;
        if (tag->count)
            return tag->count == 1 && (tag->protocol & MIFARE) ? 0 : -EUCLEAN;
        sleep(1);
    }
    return -ENODATA;
}

static int connect_tag(uint32_t index, uint32_t target) {
    struct sockaddr_nfc addr = {
        .sa_family = AF_NFC, .dev_idx = index, .target_idx = target,
        .nfc_protocol = NFC_PROTO_MIFARE,
    };
    int fd = socket(AF_NFC, SOCK_SEQPACKET | SOCK_CLOEXEC, NFC_SOCKPROTO_RAW);
    if (fd < 0) return -errno;
    if (!connect(fd, (struct sockaddr *)&addr, sizeof(addr))) return fd;
    int rc = -errno;
    close(fd);
    return rc;
}

static int check_gate(struct nfc_nl_client *client, uint32_t index, const char *udc,
                      bool blocked) {
    for (unsigned int i = 0; i < 24; i++) {
        int rc = expect_state(udc, true, blocked);
        if (rc) return rc;
        rc = nfc_poll(client, index, MIFARE);
        if (blocked) {
            if (rc == -ERFKILL) return 0;
            if (!rc) return -EUCLEAN;
            if (rc != -EBUSY) return rc;
        } else {
            if (!rc) return 0;
            if (rc != -ERFKILL) return rc;
        }
        usleep(250000);
    }
    return -ETIMEDOUT;
}

static int observe_cycle(struct nfc_nl_client *client, uint32_t index,
                         const char *udc, const char **stage) {
    struct nfc_target_info tag;
    *stage = "detach";
    int rc = wait_state(udc, false, true, 90);
    if (rc) return rc;
    charge_log_append("RX_NFC", "stage=detached online=0");
    *stage = "seat";
    rc = wait_state(udc, true, false, 60);
    if (rc) return rc;
    charge_log_append("RX_NFC", "stage=seated online=1");
    *stage = "block";
    rc = check_gate(client, index, udc, true);
    if (rc) return rc;
    charge_log_append("RX_NFC", "stage=blocked start_poll=ERFKILL");
    *stage = "remove";
    rc = wait_state(udc, false, false, 60);
    if (rc) return rc;
    charge_log_append("RX_NFC", "stage=removed online=0");
    *stage = "resume";
    rc = check_gate(client, index, udc, false);
    if (rc) return rc;
    *stage = "rediscover";
    rc = wait_tag(client, index, udc, true, &tag);
    if (!rc) charge_log_append("RX_NFC", "stage=rediscovered targets=1 mifare=1");
    return rc;
}

static int cleanup_nfc(struct nfc_nl_client *client, uint32_t index, int fd) {
    if (fd >= 0) close(fd);
    int stop = nfc_simple(client, NFC_CMD_STOP_POLL, index);
    int down = nfc_simple(client, NFC_CMD_DEV_DOWN, index);
    return stop && stop != -EINVAL ? stop : down;
}

void arm_rx_nfc_probe(int activate_target) {
    struct nfc_nl_client client = { .fd = -1 };
    struct nfc_device_info device;
    struct nfc_target_info tag;
    const char *mode = activate_target ? "target" : "poll";
    const char *stage = "arm";
    char udc[320];
    int fd = -1, cleanup = 0, rc;
    bool up = false;

    puts("RX_NFC_ARM_BEGIN");
    rc = claim_rx_nfc_probe() ? udc_path(udc, sizeof(udc)) : -EALREADY;
    if (!rc) rc = expect_state(udc, false, false);
    if (!rc) rc = nfc_nl_open(&client);
    if (!rc) rc = get_nfc_device(&client, &device);
    if (!rc && !(device.protocols & MIFARE)) rc = -EOPNOTSUPP;
    if (!rc) {
        rc = nfc_simple(&client, NFC_CMD_DEV_UP, device.index);
        up = !rc;
    }
    if (!rc) rc = nfc_poll(&client, device.index, MIFARE);
    if (!rc && activate_target) {
        rc = wait_tag(&client, device.index, udc, false, &tag);
        if (!rc) {
            fd = connect_tag(device.index, tag.index);
            if (fd < 0) rc = fd;
        }
    }
    if (!rc) {
        charge_log_append("RX_NFC", "stage=armed mode=%s", mode);
        pid_t child = fork();
        if (child < 0) rc = -errno;
        if (!child) {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            rc = observe_cycle(&client, device.index, udc, &stage);
            cleanup = cleanup_nfc(&client, device.index, fd);
            nfc_nl_close(&client);
            charge_log_append("RX_NFC", "stage=result mode=%s at=%s rc=%d cleanup=%d",
                              mode, stage, rc, cleanup);
            _exit(rc || cleanup ? 1 : 0);
        }
    }
    if (rc && up) cleanup = cleanup_nfc(&client, device.index, fd);
    else if (fd >= 0) close(fd);
    nfc_nl_close(&client);
    printf("RX_NFC_ARM_RESULT mode=%s rc=%d cleanup=%d\n", mode, rc, cleanup);
    puts("RX_NFC_ARM_END");
}
