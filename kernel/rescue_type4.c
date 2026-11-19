// SPDX-License-Identifier: AGPL-3.0-only
/* Read the public MiSeed text fixture on one Type 4 v2 test tag. */
#include "rescue_internal.h"
#include <linux/nfc.h>
#include <sys/socket.h>
#include <sys/time.h>

static int type4_exchange(int fd, const uint8_t *cmd, size_t n, uint8_t *out, size_t expected) {
    uint8_t reply[32];
    if (expected + 3 > sizeof(reply)) return -EINVAL;
    ssize_t rc = send(fd, cmd, n, MSG_NOSIGNAL);
    if (rc < 0) return -errno;
    if ((size_t)rc != n) return -EIO;
    rc = recv(fd, reply, sizeof(reply), MSG_TRUNC);
    if (rc < 0) return -errno;
    /* Linux AF_NFC prepends a zero header; the tag appends SW1/SW2. */
    if ((size_t)rc != expected + 3 || reply[0] || reply[rc-2] != 0x90 || reply[rc-1])
        return -EBADMSG;
    if (expected) memcpy(out, reply + 1, expected);
    return 0;
}

static int type4_fixture(int fd) {
    static const uint8_t app[] = {0,0xa4,4,0,7,0xd2,0x76,0,0,0x85,1,1,0};
    static const uint8_t ccfile[] = {0,0xa4,0,0x0c,2,0xe1,3};
    static const uint8_t cc_read[] = {0,0xb0,0,0,15};
    static const uint8_t expected[] = {0xd1,1,9,'T',2,'e','n','M','i','S','e','e','d'};
    uint8_t file[] = {0,0xa4,0,0x0c,2,0,0};
    uint8_t read[] = {0,0xb0,0,0,2}, cc[15], data[sizeof(expected)];
    int rc = type4_exchange(fd, app, sizeof(app), NULL, 0);
    if (!rc) rc = type4_exchange(fd, ccfile, sizeof(ccfile), NULL, 0);
    if (!rc) rc = type4_exchange(fd, cc_read, sizeof(cc_read), cc, sizeof(cc));
    if (rc) return rc;
    /* Narrow fixture profile: one standard TLV, E104 file, free read access. */
    if (cc[0] || cc[1] != 15 || cc[2] != 0x20 || (cc[3]*256 + cc[4]) < 15 ||
        cc[7] != 4 || cc[8] != 6 || cc[9] != 0xe1 || cc[10] != 4 ||
        (unsigned)(cc[11]*256 + cc[12]) < sizeof(data) + 2 || cc[13]) return -EOPNOTSUPP;
    file[5] = cc[9]; file[6] = cc[10];
    rc = type4_exchange(fd, file, sizeof(file), NULL, 0);
    if (!rc) rc = type4_exchange(fd, read, sizeof(read), data, 2);
    if (rc) return rc;
    if (data[0] || data[1] != sizeof(expected)) return -ENOMSG;
    read[3] = 2; read[4] = sizeof(expected);
    rc = type4_exchange(fd, read, sizeof(read), data, sizeof(data));
    if (rc) return rc;
    return memcmp(data, expected, sizeof(data)) ? -ENOMSG : 0;
}

int read_type4_fixture(uint32_t dev, uint32_t target, uint32_t protocols) {
    struct sockaddr_nfc addr = {.sa_family = AF_NFC, .dev_idx = dev, .target_idx = target};
    struct timeval timeout = {.tv_sec = 2};
    if (protocols == NFC_PROTO_ISO14443_MASK) addr.nfc_protocol = NFC_PROTO_ISO14443;
    else if (protocols == NFC_PROTO_ISO14443_B_MASK) addr.nfc_protocol = NFC_PROTO_ISO14443_B;
    else return -EOPNOTSUPP;
    int fd = socket(AF_NFC, SOCK_SEQPACKET | SOCK_CLOEXEC, NFC_SOCKPROTO_RAW);
    if (fd < 0) return -errno;
    int rc = 0;
    if (setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) ||
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) ||
        connect(fd,(struct sockaddr *)&addr,sizeof(addr))) rc = -errno;
    else rc = type4_fixture(fd);
    close(fd);
    return rc;
}
