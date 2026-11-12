"""Validate compiled Umi wired topology and selected mainline DT schemas."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import json
import subprocess
from pathlib import Path

SCHEMAS = ("power/supply/qcom,pm8150b-fg.yaml",
           "power/supply/qcom,pm8150b-charger.yaml",
           "power/supply/xiaomi,umi-bq25970.yaml",
           "power/supply/battery.yaml", "thermal/thermal-zones.yaml",
           "thermal/thermal-sensor.yaml", "pinctrl/qcom,sm8250-pinctrl.yaml")


def check(dtb, schema):
    indexed = json.loads(schema.read_text())
    for name in SCHEMAS:
        if "http://devicetree.org/schemas/" + name + "#" not in indexed:
            raise ValueError("schema was not loaded: " + name)

    def prop(node, name, kind="x"):
        result = subprocess.run(["fdtget", "-t", kind, str(dtb), node, name],
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()

    nodes = {name: prop("/__symbols__", name, "s") for name in
             ("umi_battery", "umi_fg", "umi_charger", "umi_pps", "pm8150b_typec")}
    connector = nodes["pm8150b_typec"] + "/connector"
    if prop(connector, "op-sink-microwatt", "i") != "7000000":
        raise ValueError("PPS operating floor must permit the 1 A request region")
    pdos = [int(value) for value in prop(connector, "sink-pdos", "u").split()]
    if (len(pdos) != 2 or pdos[0] >> 30 != 0 or pdos[1] >> 30 != 3
            or ((pdos[0] >> 10) & 0x3ff) * 50 != 5000
            or (pdos[0] & 0x3ff) * 10 != 3000
            or (pdos[1] & 0x7f) * 50 != 2500):
        raise ValueError("wired sink must advertise fixed 5 V/3 A and PPS <=2.5 A")
    handles = {name: prop(node, "phandle") for name, node in nodes.items()}
    references = (("umi_fg", "monitored-battery", "umi_battery"),
                  ("umi_charger", "monitored-battery", "umi_battery"),
                  ("umi_charger", "qcom,fuel-gauge", "umi_fg"),
                  ("umi_pps", "xiaomi,fuel-gauge", "umi_fg"),
                  ("umi_pps", "xiaomi,switch-charger", "umi_charger"),
                  ("umi_pps", "power-supplies", "pm8150b_typec"))
    for owner, name, target in references:
        if prop(nodes[owner], name) != handles[target]:
            raise ValueError(f"{owner}:{name} must reference {target}")
    if prop(nodes["umi_battery"], "compatible", "s") != "simple-battery":
        raise ValueError("monitored-battery target is not simple-battery")
    if prop("/thermal-zones/wired-pps-thermal", "thermal-sensors") != handles["umi_pps"]:
        raise ValueError("wired thermal zone must reference the PPS sensor")
    zone = "/thermal-zones/wired-pd-thermal"
    if prop(zone, "thermal-sensors") != handles["umi_charger"]:
        raise ValueError("fixed PD thermal zone must reference SMB5 battery telemetry")
    trip = zone + "/trips/warm"
    if prop(trip, "temperature", "i") != "45000" or prop(trip, "hysteresis", "i") != "0":
        raise ValueError("fixed PD warm boundary must match the qualified battery gate")
    if prop(zone + "/cooling-maps/map-warm", "cooling-device") != handles["umi_charger"] + " 1 1":
        raise ValueError("fixed PD cooling must stop SMB5, separately from PPS")
    result = subprocess.run(["dt-validate", "-s", str(schema), "-l", ":".join(SCHEMAS), str(dtb)],
                            capture_output=True, text=True)
    # dt-validate/make can emit schema failures and still exit zero.
    if result.returncode or result.stderr.strip() or result.stdout.strip():
        raise ValueError("DT schema diagnostics:\n" + result.stdout + result.stderr)
    return nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    args = parser.parse_args()
    try:
        nodes = check(args.dtb, args.schema)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"wired DT validation failed: {exc}\n")
    print("PASS: wired DT schemas, battery/gauge/charger/TCPM references and thermal zone")
    print(json.dumps(nodes, sort_keys=True))


if __name__ == "__main__":
    main()
