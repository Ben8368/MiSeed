#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Read-only Linux NFC generic-netlink inventory (no RF/power changes)."""
import argparse
import errno
import json
import socket
import struct
import sys

# Linux UAPI: include/uapi/linux/{netlink,genetlink,nfc}.h, see sources.lock.
GENL_ID_CTRL = 0x10
CTRL_CMD_GETFAMILY = 3
CTRL_ATTR_FAMILY_ID, CTRL_ATTR_FAMILY_NAME = 1, 2
NFC_CMD_GET_DEVICE = 1
NFC_ATTR_DEVICE_INDEX, NFC_ATTR_DEVICE_NAME, NFC_ATTR_PROTOCOLS = 1, 2, 3
NFC_ATTR_DEVICE_POWERED = 12
NLM_F_REQUEST, NLM_F_DUMP, NLM_F_DUMP_INTR = 1, 0x300, 0x10
NLMSG_ERROR, NLMSG_DONE, NLMSG_OVERRUN = 2, 3, 4


def align(length):
    return (length + 3) & ~3


def attribute(kind, payload):
    length = len(payload) + 4
    return struct.pack('=HH', length, kind) + payload + bytes(align(length) - length)


def attributes(data):
    result = {}
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise ValueError('truncated netlink attribute')
        length, kind = struct.unpack_from('=HH', data, offset)
        if length < 4 or offset + align(length) > len(data):
            raise ValueError('invalid netlink attribute length')
        kind &= 0x3fff
        if kind in result:
            raise ValueError('duplicate netlink attribute')
        result[kind] = data[offset + 4:offset + length]
        offset += align(length)
    return result


def messages(data):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 16:
            raise ValueError('truncated netlink header')
        length, kind, flags, seq, pid = struct.unpack_from('=IHHII', data, offset)
        if length < 16 or offset + align(length) > len(data):
            raise ValueError('invalid netlink message length')
        yield kind, flags, seq, pid, data[offset + 16:offset + length]
        offset += align(length)


class Netlink:
    def __init__(self, sock):
        self.sock = sock
        self.sequence = 0

    def request(self, family, command, payload=b'', dump=False):
        if (family == GENL_ID_CTRL and command != CTRL_CMD_GETFAMILY or
                family != GENL_ID_CTRL and command != NFC_CMD_GET_DEVICE):
            raise ValueError('only read-only discovery is supported')
        self.sequence += 1
        body = struct.pack('=BBH', command, 1, 0) + payload
        flags = NLM_F_REQUEST | (NLM_F_DUMP if dump else 0)
        packet = struct.pack('=IHHII', 16 + len(body), family, flags,
                             self.sequence, 0) + body
        self.sock.sendto(packet, (0, 0))
        replies = []
        for _ in range(256):
            data, _, msg_flags, peer = self.sock.recvmsg(65536)
            if peer[0] != 0 or msg_flags & 0x20:
                raise ValueError('untrusted or truncated netlink datagram')
            for kind, flags, seq, _, body in messages(data):
                if seq != self.sequence:
                    raise ValueError('netlink sequence mismatch')
                if flags & NLM_F_DUMP_INTR:
                    raise OSError(errno.EAGAIN, 'NFC inventory changed; retry')
                if kind == NLMSG_OVERRUN:
                    raise OSError(errno.ENOBUFS, 'netlink overrun')
                if kind == NLMSG_ERROR:
                    if len(body) < 4:
                        raise ValueError('truncated netlink error')
                    code, = struct.unpack_from('=i', body)
                    if code:
                        raise OSError(abs(code), 'netlink request failed')
                    continue
                if kind == NLMSG_DONE:
                    if not dump or (body and len(body) < 4):
                        raise ValueError('unexpected netlink dump completion')
                    if body:
                        code, = struct.unpack_from('=i', body)
                        if code:
                            raise OSError(abs(code), 'netlink dump failed')
                    return replies
                if kind != family or len(body) < 4:
                    raise ValueError('unexpected generic-netlink response')
                replies.append(attributes(body[4:]))
                if not dump:
                    return replies
        raise OSError(errno.EOVERFLOW, 'netlink response bound exceeded')


def decode_device(attrs):
    index = attrs.get(NFC_ATTR_DEVICE_INDEX, b'')
    protocols = attrs.get(NFC_ATTR_PROTOCOLS, b'')
    name = attrs.get(NFC_ATTR_DEVICE_NAME, b'')
    powered = attrs.get(NFC_ATTR_DEVICE_POWERED)
    if len(index) != 4 or len(protocols) != 4:
        raise ValueError('missing or invalid NFC device integers')
    if not 2 <= len(name) <= 9 or name[-1:] != b'\0' or b'\0' in name[:-1]:
        raise ValueError('invalid NFC device name')
    if powered is not None and powered not in (b'\0', b'\1'):
        raise ValueError('invalid NFC powered attribute')
    return {'index': struct.unpack('=I', index)[0],
            'name': name[:-1].decode('ascii'),
            'protocols': struct.unpack('=I', protocols)[0],
            'powered': None if powered is None else bool(powered[0])}


def inventory(sock):
    client = Netlink(sock)
    replies = client.request(GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
                             attribute(CTRL_ATTR_FAMILY_NAME, b'nfc\0'))
    family = replies[0].get(CTRL_ATTR_FAMILY_ID, b'')
    if len(family) != 2:
        raise ValueError('missing generic-netlink NFC family')
    family, = struct.unpack('=H', family)
    if family <= GENL_ID_CTRL:
        raise ValueError('invalid generic-netlink NFC family ID')
    devices = [decode_device(attrs) for attrs in
               client.request(family, NFC_CMD_GET_DEVICE, dump=True)]
    return {'interface': 'Linux NFC generic netlink', 'devices': devices,
            'transport_rf_ndef_ese': 'not tested'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=3.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 30:
        parser.error('--timeout must be > 0 and <= 30 seconds')
    try:
        if not sys.platform.startswith('linux'):
            raise OSError(errno.EAFNOSUPPORT, 'Linux generic netlink is required')
        with socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, 16) as sock:
            sock.settimeout(args.timeout)
            sock.bind((0, 0))
            print(json.dumps(inventory(sock), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as error:
        print(json.dumps({'error': str(error), 'errno': getattr(error, 'errno', None)}),
              file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
