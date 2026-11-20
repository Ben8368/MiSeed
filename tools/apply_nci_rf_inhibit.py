"""Stage the NCI RF inhibition mechanism on the locked Linux baseline."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import subprocess
from pathlib import Path

if __package__:
    from .apply_nfc_port import REVISION, safe
else:
    from apply_nfc_port import REVISION, safe


CORE = "net/nfc/nci/core.c"
HEADER = "include/net/nfc/nci_core.h"
RESPONSE = "net/nfc/nci/rsp.c"
INCLUDE = "net/nfc/nci/miseed-rf-inhibit.inc"
GATE = "\tif (ndev->rf_inhibited)\n\t\treturn -ERFKILL;\n\n"
DATA_GATE = ("\tif (ndev->rf_inhibited) {\n\t\tkfree_skb(skb);\n"
             "\t\treturn -ERFKILL;\n\t}\n\n")
STOP_GATE = "\tif (ndev->rf_inhibited)\n\t\treturn;\n\n"
HOOKS = (
    ("int nci_start_poll", "\tif ((atomic_read(&ndev->state) == NCI_DISCOVERY)", GATE),
    ("int nci_activate_target", '\tpr_debug("target_idx', GATE),
    ("int nci_transceive", "\tconn_info = ndev->rf_conn_info;", DATA_GATE),
    ("int nci_tm_send", "\trc = nci_send_data", DATA_GATE),
    ("void nci_stop_poll", "\tif ((atomic_read", STOP_GATE),
    ("void nci_deactivate_target", "\tif (!ndev->target_active_prot)", STOP_GATE),
)
RESPONSE_STATE = "\t    (atomic_read(&ndev->state) != NCI_POLL_ACTIVE)) {"


def replace(text, old, new):
    if text.count(new) == 1:
        return text
    if text.count(old) != 1:
        raise ValueError("NCI baseline changed around " + repr(old))
    return text.replace(old, new, 1)


def transform(name, original):
    text = original.decode("utf-8")
    if name == CORE:
        text = replace(text, "\nstruct nci_cmd_param {",
                       '\n#include "miseed-rf-inhibit.inc"\n\nstruct nci_cmd_param {')
        for function, anchor, gate in HOOKS:
            start = text.index("static " + function + "(")
            end = text.index("\n}\n", start)
            text = text[:start] + replace(text[start:end], anchor, gate + anchor) + text[end:]
    elif name == HEADER:
        fields = "\tatomic_t\t\tstate;\n\tunsigned long\t\tflags;"
        text = replace(text, fields, fields + "\n\tbool\t\t\trf_inhibited;"
                       "\n\tbool\t\t\trf_deactivate_pending;")
        api = "void nci_unregister_device(struct nci_dev *ndev);"
        text = replace(text, api, api + "\nint nci_set_rf_inhibited(struct nci_dev *ndev, bool inhibit);")
    elif name == RESPONSE:
        text = replace(text, RESPONSE_STATE,
                       "\t    (atomic_read(&ndev->state) != NCI_POLL_ACTIVE &&\n"
                       "\t     !(READ_ONCE(ndev->rf_inhibited) &&\n"
                       "\t       atomic_read(&ndev->state) == NCI_LISTEN_ACTIVE))) {")
    else:
        raise ValueError("unexpected NCI source: " + name)
    return text.encode("utf-8")


def apply(source, project):
    if source.is_symlink() or safe(source, ".miseed-source-revision", True).read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")
    include = safe(source, INCLUDE)
    implementation = safe(project, "kernel/nfc/nci-rf-inhibit.inc", True).read_bytes()
    changes = []
    for name in (CORE, HEADER, RESPONSE):
        path = safe(source, name, True)
        changes.append((path, transform(name, path.read_bytes())))
    for path, contents in (*changes, (include, implementation)):
        path.write_bytes(contents)


def restore(source, repository):
    # A cached experiment may be reused for a normal build. Only undo the
    # exact known transformation of the reviewed Git blobs.
    changes = []
    for name in (CORE, HEADER, RESPONSE):
        path = safe(source, name, True)
        original = subprocess.check_output(["git", "-C", str(repository), "show", REVISION + ":" + name])
        current = path.read_bytes()
        if current == original:
            continue
        if current != transform(name, original):
            raise ValueError("unexpected NCI source: " + name)
        changes.append((path, original))
    for path, original in changes:
        path.write_bytes(original)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--restore-from", type=Path)
    args = parser.parse_args()
    try:
        if args.restore_from:
            restore(args.source, args.restore_from)
        else:
            apply(args.source, Path(__file__).resolve().parents[1])
    except (OSError, ValueError) as error:
        parser.exit(1, f"NCI RF inhibit staging failed: {error}\n")


if __name__ == "__main__":
    main()
