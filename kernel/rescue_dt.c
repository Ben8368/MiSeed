// SPDX-License-Identifier: AGPL-3.0-only
/* Read-only, bounded boot FDT export. No storage or hardware register writes. */
#include "rescue_internal.h"

void export_runtime_dtb(const char *path)
{
    unsigned char header[8], *data;
    size_t total, offset;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("DTB_ERROR open: %s\n", strerror(errno));
        return;
    }
    offset = 0;
    while (offset < sizeof(header)) {
        ssize_t n = read(fd, header + offset, sizeof(header) - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto failed;
        offset += (size_t)n;
    }
    total = ((size_t)header[4] << 24) | ((size_t)header[5] << 16) |
            ((size_t)header[6] << 8) | header[7];
    if (memcmp(header, "\xd0\x0d\xfe\xed", 4) || total < 40 || total > 2 * 1024 * 1024)
        goto failed;
    data = malloc(total);
    if (!data) goto failed;
    memcpy(data, header, sizeof(header));
    while (offset < total) {
        ssize_t n = read(fd, data + offset, total - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { free(data); goto failed; }
        offset += (size_t)n;
    }
    close(fd);
    printf("DTB_BEGIN %zu\n", total);
    for (offset = 0; offset < total; offset += 32) {
        size_t end = offset + 32 < total ? offset + 32 : total;
        printf("DTB %08zx ", offset);
        for (size_t i = offset; i < end; i++) printf("%02x", data[i]);
        putchar('\n');
    }
    puts("DTB_END");
    free(data);
    return;
failed:
    close(fd);
    puts("DTB_ERROR incomplete or invalid FDT; no export emitted");
}
