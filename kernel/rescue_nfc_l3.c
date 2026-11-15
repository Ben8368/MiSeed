// SPDX-License-Identifier: AGPL-3.0-only
/* One-shot standard Linux NFC/NCI reset, RF discovery and recovery check. */
#define _GNU_SOURCE
#include "rescue_internal.h"
#include "rescue_nfc_netlink.h"

#include <linux/nfc.h>
#include <stdbool.h>

#define NFC_POLL_WINDOW_SECONDS 12

struct nfc_device_info {
    uint32_t index;
    uint32_t protocols;
    char name[16];
    unsigned int count;
};

struct nfc_target_info {
    uint32_t index;
    uint32_t protocol;
    unsigned int count;
};

static int device_visit(const unsigned char *attrs, size_t length, void *context) {
    struct nfc_device_info *device = context;
    uint32_t index, protocols;
    int rc;

    if ((rc = nfc_nl_attr_read(attrs, length, NFC_ATTR_DEVICE_INDEX,
                               &index, sizeof(index))) ||
        (rc = nfc_nl_attr_read(attrs, length, NFC_ATTR_PROTOCOLS,
                               &protocols, sizeof(protocols))) ||
        (rc = nfc_nl_attr_string(attrs, length, NFC_ATTR_DEVICE_NAME,
                                 device->name, sizeof(device->name))))
        return rc;
    if (++device->count > 1) return -E2BIG;
    device->index = index;
    device->protocols = protocols;
    return 0;
}

static int target_visit(const unsigned char *attrs, size_t length, void *context) {
    struct nfc_target_info *target = context;
    uint32_t index, protocol;
    int rc;

    if ((rc = nfc_nl_attr_read(attrs, length, NFC_ATTR_TARGET_INDEX,
                               &index, sizeof(index))) ||
        (rc = nfc_nl_attr_read(attrs, length, NFC_ATTR_PROTOCOLS,
                               &protocol, sizeof(protocol))))
        return rc;
    if (++target->count > 8) return -E2BIG;
    target->index = index;
    target->protocol = protocol;
    return 0;
}

static int nfc_index_attr(unsigned char *attrs, size_t capacity,
                          size_t *length, uint32_t index) {
    return nfc_nl_attr_u32(attrs, capacity, length,
                           NFC_ATTR_DEVICE_INDEX, index);
}

static int nfc_simple(struct nfc_nl_client *client, uint8_t command,
                      uint32_t index) {
    unsigned char attrs[32];
    size_t length = 0;
    int rc = nfc_index_attr(attrs, sizeof(attrs), &length, index);
    return rc ? rc : nfc_nl_ack(client, command, attrs, length);
}

static int nfc_poll(struct nfc_nl_client *client, uint32_t index,
                    uint32_t protocols) {
    unsigned char attrs[64];
    size_t length = 0;
    int rc = nfc_index_attr(attrs, sizeof(attrs), &length, index);
    if (!rc) rc = nfc_nl_attr_u32(attrs, sizeof(attrs), &length,
                                  NFC_ATTR_IM_PROTOCOLS, protocols);
    return rc ? rc : nfc_nl_ack(client, NFC_CMD_START_POLL, attrs, length);
}

static int get_nfc_device(struct nfc_nl_client *client,
                          struct nfc_device_info *device) {
    memset(device, 0, sizeof(*device));
    int rc = nfc_nl_dump(client, NFC_CMD_GET_DEVICE, NULL, 0,
                         device_visit, device);
    if (rc) return rc;
    return device->count == 1 ? 0 : -ENODEV;
}

static int nfc_get_targets(struct nfc_nl_client *client, uint32_t index,
                           struct nfc_target_info *target) {
    unsigned char attrs[32];
    size_t length = 0;
    int rc = nfc_index_attr(attrs, sizeof(attrs), &length, index);
    if (rc) return rc;
    memset(target, 0, sizeof(*target));
    return nfc_nl_dump(client, NFC_CMD_GET_TARGET, attrs, length,
                       target_visit, target);
}

static void report_nfc_error(const char *step, int error) {
    printf("NFC %s failed: %s (%d); inspect dmesg before retry.\n",
           step, strerror(-error), error);
}

int run_nfc_l3_probe(void) {
    struct nfc_nl_client client = { .fd = -1 };
    struct nfc_device_info device;
    struct nfc_target_info target;
    uint32_t supported;
    int rc, close_rc;
    bool up = false, polling = false;

    puts("NFC L3 base: standard NCI reset/init, RF target discovery and clean reopen.");
    puts("No firmware download, VEN low pulse, eSE, HCE, write APDU or UID logging.");
    rc = nfc_nl_open(&client);
    if (!rc) rc = get_nfc_device(&client, &device);
    if (rc) goto done;
    supported = device.protocols & (NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |
                NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO14443_MASK |
                NFC_PROTO_ISO14443_B_MASK);
    printf("NFC device=%s index=%u protocols=0x%x\n",
           device.name, device.index, device.protocols);
    if (!supported) { rc = -EOPNOTSUPP; goto done; }

    rc = nfc_simple(&client, NFC_CMD_DEV_UP, device.index);
    if (rc) {
        report_nfc_error("DEV_UP/reset-init", rc);
        goto done;
    }
    up = true;
    puts("NFC DEV_UP/reset-init passed.");

    rc = nfc_poll(&client, device.index, 0);
    if (rc >= 0) {
        (void)nfc_simple(&client, NFC_CMD_STOP_POLL, device.index);
        puts("NFC unsupported-protocol negative case unexpectedly accepted.");
        rc = -EUCLEAN;
        goto cleanup;
    }
    printf("NFC empty-protocol request rejected: %s (%d).\n",
           strerror(-rc), rc);

    rc = nfc_poll(&client, device.index, supported);
    if (rc) {
        report_nfc_error("START_POLL", rc);
        goto cleanup;
    }
    polling = true;
    printf("Hold one non-payment, non-access test card near the phone for %u seconds.\n",
           NFC_POLL_WINDOW_SECONDS);
    sleep(NFC_POLL_WINDOW_SECONDS);
    rc = nfc_get_targets(&client, device.index, &target);
    if (rc) {
        report_nfc_error("GET_TARGET", rc);
        goto cleanup;
    }
    if (!target.count) {
        puts("NFC RF discovery found no target; place a supported non-payment test card on the phone.");
        rc = -ENODATA;
        goto cleanup;
    }
    printf("NFC RF discovery passed: targets=%u protocol=%u; UID omitted.\n",
           target.count, target.protocol);
    puts("NDEF read not attempted; this probe never sends application or write APDUs.");

cleanup:
    {
    int result = rc;
    if (polling) {
        int stop_rc = nfc_simple(&client, NFC_CMD_STOP_POLL, device.index);
        if (stop_rc && stop_rc != -EINVAL) {
            report_nfc_error("STOP_POLL", stop_rc);
            if (!result) result = stop_rc;
        }
    }
    close_rc = nfc_simple(&client, NFC_CMD_DEV_DOWN, device.index);
    up = false;
    if (close_rc) {
        report_nfc_error("DEV_DOWN/CORE_RESET close", close_rc);
        if (!result) result = close_rc;
        goto cleanup_done;
    }
    rc = nfc_simple(&client, NFC_CMD_DEV_UP, device.index);
    if (rc) {
        report_nfc_error("clean reopen", rc);
        if (!result) result = rc;
        goto cleanup_done;
    }
    up = true;
    rc = nfc_simple(&client, NFC_CMD_DEV_DOWN, device.index);
    up = false;
    if (rc) {
        report_nfc_error("final DEV_DOWN", rc);
        if (!result) result = rc;
    } else puts("NFC close/CORE_RESET/reopen/final close passed; shared VEN was retained.");
cleanup_done:
    rc = result;
    }

done:
    if (up) (void)nfc_simple(&client, NFC_CMD_DEV_DOWN, device.index);
    nfc_nl_close(&client);
    if (rc) printf("NFC L3 base incomplete: %s (%d). Run dmesg and preserve the session.\n",
                   strerror(-rc), rc);
    return rc;
}
