"""Report DPU command-mode PP TX completions/s from a bounded tracefs sample."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import json
from pathlib import Path
import re

WINDOW = re.compile(r"(\d+\.\d+): [^:\n]+: miseed_fps_(begin|end)$")
TX_DONE = re.compile(r"(\d+\.\d+): dpu_enc_phys_cmd_pp_tx_done: "
                     r"id=(\d+), pp=(\d+), new_count=-?\d+, event=(\d+)$")
COUNTS = re.compile(r"^# entries-in-buffer/entries-written:\s*(\d+)/(\d+)", re.M)


def report_fps(trace, encoder, pp):
    counts = COUNTS.findall(trace)
    if len(counts) != 1 or counts[0][0] != counts[0][1] or "[LOST " in trace:
        raise ValueError("missing tracefs counts or lost trace events")
    marks = []
    completions = []
    for line in trace.splitlines():
        if match := WINDOW.search(line):
            marks.append((float(match[1]), match[2]))
        if match := TX_DONE.search(line):
            timestamp, drm_id, index, event = match.groups()
            if (int(drm_id), int(index), int(event)) == (encoder, pp, 1):
                completions.append(float(timestamp))
    if [kind for _, kind in marks] != ["begin", "end"]:
        raise ValueError("expected one complete begin/end marker pair")
    start, end = (stamp for stamp, _ in marks)
    span = end - start
    if not 1 <= span <= 30:
        raise ValueError("expected a monotonic 1–30 second trace window")
    completed = sum(start < stamp < end for stamp in completions)
    return {"encoder_id": encoder, "pingpong_index": pp, "span_s": round(span, 6),
            "tx_completed": completed, "dpu_tx_fps": round(completed / span, 3),
            "metric": "DPU transfer completions during workload; not physical panel scan Hz"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--encoder", type=int, required=True)
    parser.add_argument("--pp", type=int, required=True)
    args = parser.parse_args()
    try:
        trace = args.trace.read_text(encoding="utf-8")
        print(json.dumps(report_fps(trace, args.encoder, args.pp), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"DPU FPS unavailable: {exc}\n")


if __name__ == "__main__":
    main()
