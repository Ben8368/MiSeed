// SPDX-License-Identifier: AGPL-3.0-only
/* Minimal bounded generic-netlink helpers for the standard Linux NFC API. */
#define _GNU_SOURCE
#include "rescue_nfc_netlink.h"

#include <errno.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nfc.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NFC_NL_BUFFER 4096
#define NFC_NL_TIMEOUT_MS 3000
#define CTRL_GENL_VERSION 2

static size_t nla_space(size_t length) {
    return (length + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1);
}

static int nla_append(unsigned char *buffer, size_t capacity, size_t *used,
                      uint16_t type, const void *value, size_t length) {
    struct nlattr attr;
    size_t space = nla_space(sizeof(attr) + length);

    if (*used > capacity || space > capacity - *used) return -EMSGSIZE;
    attr.nla_len = (uint16_t)(sizeof(attr) + length);
    attr.nla_type = type;
    memcpy(buffer + *used, &attr, sizeof(attr));
    memcpy(buffer + *used + sizeof(attr), value, length);
    memset(buffer + *used + attr.nla_len, 0, space - attr.nla_len);
    *used += space;
    return 0;
}

int nfc_nl_attr_u32(unsigned char *attrs, size_t capacity, size_t *length,
                    uint16_t type, uint32_t value) {
    return nla_append(attrs, capacity, length, type, &value, sizeof(value));
}

static int nl_send(struct nfc_nl_client *client, uint16_t family,
                   uint8_t command, uint16_t flags,
                   const unsigned char *attrs, size_t attrs_length,
                   uint32_t *sequence) {
    unsigned char packet[NFC_NL_BUFFER];
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    struct nlmsghdr header;
    struct genlmsghdr generic;
    size_t length = sizeof(header) + sizeof(generic) + attrs_length;
    ssize_t sent;

    if (length > sizeof(packet)) return -EMSGSIZE;
    memset(packet, 0, length);
    *sequence = ++client->sequence;
    header.nlmsg_len = (uint32_t)length;
    header.nlmsg_type = family;
    header.nlmsg_flags = NLM_F_REQUEST | flags;
    header.nlmsg_seq = *sequence;
    generic.cmd = command;
    generic.version = family == GENL_ID_CTRL ? CTRL_GENL_VERSION : NFC_GENL_VERSION;
    memcpy(packet, &header, sizeof(header));
    memcpy(packet + sizeof(header), &generic, sizeof(generic));
    if (attrs_length)
        memcpy(packet + sizeof(header) + sizeof(generic), attrs, attrs_length);
    sent = sendto(client->fd, packet, length, 0,
                  (struct sockaddr *)&kernel, sizeof(kernel));
    if (sent < 0) return -errno;
    return (size_t)sent == length ? 0 : -EIO;
}

static int nl_receive(struct nfc_nl_client *client, uint32_t sequence,
                      unsigned char *buffer, size_t capacity,
                      size_t *received) {
    struct pollfd wait = { .fd = client->fd, .events = POLLIN };
    struct sockaddr_nl peer;
    socklen_t peer_length = sizeof(peer);
    int ready = poll(&wait, 1, NFC_NL_TIMEOUT_MS);
    ssize_t length;

    if (ready < 0) return -errno;
    if (!ready) return -ETIMEDOUT;
    length = recvfrom(client->fd, buffer, capacity, 0,
                      (struct sockaddr *)&peer, &peer_length);
    if (length < 0) return -errno;
    if (peer_length != sizeof(peer) || peer.nl_pid != 0 ||
        (size_t)length > capacity)
        return -EBADMSG;
    int remaining = (int)length;
    for (struct nlmsghdr *message = (struct nlmsghdr *)buffer;
         NLMSG_OK(message, remaining);
         message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_seq != sequence) return -EBADMSG;
    }
    if (remaining) return -EBADMSG;
    *received = (size_t)length;
    return 0;
}

int nfc_nl_ack(struct nfc_nl_client *client, uint8_t command,
               const unsigned char *attrs, size_t attrs_length) {
    unsigned char response[NFC_NL_BUFFER];
    uint32_t sequence;
    size_t received;
    int rc = nl_send(client, client->family, command, NLM_F_ACK,
                     attrs, attrs_length, &sequence);

    if (rc) return rc;
    rc = nl_receive(client, sequence, response, sizeof(response), &received);
    if (rc) return rc;
    int remaining = (int)received;
    for (struct nlmsghdr *message = (struct nlmsghdr *)response;
         NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_type != NLMSG_ERROR ||
            message->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
            continue;
        return ((struct nlmsgerr *)NLMSG_DATA(message))->error;
    }
    return -EBADMSG;
}

int nfc_nl_attr_read(const unsigned char *data, size_t length, uint16_t wanted,
                     void *value, size_t value_length) {
    size_t offset = 0;

    while (offset + sizeof(struct nlattr) <= length) {
        struct nlattr attr;
        size_t space;
        memcpy(&attr, data + offset, sizeof(attr));
        if (attr.nla_len < sizeof(attr) || attr.nla_len > length - offset)
            return -EBADMSG;
        space = nla_space(attr.nla_len);
        if (space > length - offset) return -EBADMSG;
        if ((attr.nla_type & NLA_TYPE_MASK) == wanted) {
            size_t payload = attr.nla_len - sizeof(attr);
            if (payload != value_length) return -EBADMSG;
            memcpy(value, data + offset + sizeof(attr), payload);
            return 0;
        }
        offset += space;
    }
    return -ENOENT;
}

int nfc_nl_attr_string(const unsigned char *data, size_t length,
                       uint16_t wanted, char *value, size_t capacity) {
    size_t offset = 0;

    while (offset + sizeof(struct nlattr) <= length) {
        struct nlattr attr;
        size_t space;
        memcpy(&attr, data + offset, sizeof(attr));
        if (attr.nla_len < sizeof(attr) || attr.nla_len > length - offset)
            return -EBADMSG;
        space = nla_space(attr.nla_len);
        if (space > length - offset) return -EBADMSG;
        if ((attr.nla_type & NLA_TYPE_MASK) == wanted) {
            size_t payload = attr.nla_len - sizeof(attr);
            if (!payload || payload > capacity ||
                !memchr(data + offset + sizeof(attr), '\0', payload))
                return -EBADMSG;
            memcpy(value, data + offset + sizeof(attr), payload);
            return 0;
        }
        offset += space;
    }
    return -ENOENT;
}

int nfc_nl_dump(struct nfc_nl_client *client, uint8_t command,
                const unsigned char *attrs, size_t attrs_length,
                nfc_nl_visit_fn visit, void *context) {
    unsigned char response[NFC_NL_BUFFER];
    uint32_t sequence;
    int rc = nl_send(client, client->family, command, NLM_F_DUMP,
                     attrs, attrs_length, &sequence);

    if (rc) return rc;
    for (unsigned int batches = 0; batches < 64; batches++) {
        size_t received;
        rc = nl_receive(client, sequence, response, sizeof(response), &received);
        if (rc) return rc;
        int remaining = (int)received;
        for (struct nlmsghdr *message = (struct nlmsghdr *)response;
             NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
            if (message->nlmsg_type == NLMSG_DONE) return 0;
            if (message->nlmsg_type == NLMSG_ERROR) {
                if (message->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
                    return -EBADMSG;
                return ((struct nlmsgerr *)NLMSG_DATA(message))->error;
            }
            if (message->nlmsg_type != client->family ||
                message->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN))
                return -EBADMSG;
            rc = visit((const unsigned char *)NLMSG_DATA(message) + GENL_HDRLEN,
                       message->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN), context);
            if (rc) return rc;
        }
        if (remaining) return -EBADMSG;
    }
    return -EOVERFLOW;
}

static int family_visit(const unsigned char *attrs, size_t length, void *context) {
    uint16_t id;
    int rc = nfc_nl_attr_read(attrs, length, CTRL_ATTR_FAMILY_ID,
                              &id, sizeof(id));
    if (!rc) *(uint16_t *)context = id;
    return rc;
}

int nfc_nl_open(struct nfc_nl_client *client) {
    unsigned char attrs[64], response[NFC_NL_BUFFER];
    size_t length = 0, received;
    struct sockaddr_nl local = { .nl_family = AF_NETLINK };
    uint32_t sequence;
    uint16_t family = 0;
    int rc;

    client->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    if (client->fd < 0) return -errno;
    if (bind(client->fd, (struct sockaddr *)&local, sizeof(local))) return -errno;
    if ((rc = nla_append(attrs, sizeof(attrs), &length, CTRL_ATTR_FAMILY_NAME,
                         NFC_GENL_NAME, sizeof(NFC_GENL_NAME))))
        return rc;
    if ((rc = nl_send(client, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0,
                      attrs, length, &sequence)))
        return rc;
    if ((rc = nl_receive(client, sequence, response, sizeof(response), &received)))
        return rc;
    int remaining = (int)received;
    for (struct nlmsghdr *message = (struct nlmsghdr *)response;
         NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_type == NLMSG_ERROR) {
            if (message->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
                return -EBADMSG;
            return ((struct nlmsgerr *)NLMSG_DATA(message))->error;
        }
        if (message->nlmsg_type != GENL_ID_CTRL ||
            message->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN))
            return -EBADMSG;
        rc = family_visit((const unsigned char *)NLMSG_DATA(message) + GENL_HDRLEN,
                          message->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN), &family);
        if (rc) return rc;
    }
    if (remaining) return -EBADMSG;
    if (family <= GENL_ID_CTRL) return -ENOENT;
    client->family = family;
    return 0;
}

void nfc_nl_close(struct nfc_nl_client *client) {
    if (client->fd >= 0) close(client->fd);
    client->fd = -1;
}
