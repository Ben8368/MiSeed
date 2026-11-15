// SPDX-License-Identifier: AGPL-3.0-only
#ifndef MISEED_RESCUE_NFC_NETLINK_H
#define MISEED_RESCUE_NFC_NETLINK_H

#include <stddef.h>
#include <stdint.h>

struct nfc_nl_client {
    int fd;
    uint16_t family;
    uint32_t sequence;
};

typedef int (*nfc_nl_visit_fn)(const unsigned char *, size_t, void *);

int nfc_nl_open(struct nfc_nl_client *client);
void nfc_nl_close(struct nfc_nl_client *client);
int nfc_nl_ack(struct nfc_nl_client *client, uint8_t command,
               const unsigned char *attrs, size_t attrs_length);
int nfc_nl_dump(struct nfc_nl_client *client, uint8_t command,
                const unsigned char *attrs, size_t attrs_length,
                nfc_nl_visit_fn visit, void *context);
int nfc_nl_attr_u32(unsigned char *attrs, size_t capacity, size_t *length,
                    uint16_t type, uint32_t value);
int nfc_nl_attr_read(const unsigned char *data, size_t length, uint16_t wanted,
                     void *value, size_t value_length);
int nfc_nl_attr_string(const unsigned char *data, size_t length,
                       uint16_t wanted, char *value, size_t capacity);

#endif
