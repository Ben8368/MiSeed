// SPDX-License-Identifier: AGPL-3.0-only
#ifndef MISEED_RESCUE_NFC_PROBE_H
#define MISEED_RESCUE_NFC_PROBE_H

#include "rescue_nfc_netlink.h"
#include <stdbool.h>

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

int get_nfc_device(struct nfc_nl_client *client, struct nfc_device_info *device);
int nfc_get_targets(struct nfc_nl_client *client, uint32_t index, struct nfc_target_info *target);
int nfc_simple(struct nfc_nl_client *client, uint8_t command, uint32_t index);
int nfc_poll(struct nfc_nl_client *client, uint32_t index, uint32_t protocols);
bool claim_rx_nfc_probe(void);

#endif
