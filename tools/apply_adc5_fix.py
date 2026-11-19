"""Apply the pinned PMIC5 ADC configuration sequence without vendor frameworks."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = "drivers/iio/adc/qcom-spmi-adc5.c"
PATCHED = "b40343cba96af4f3712813cd49372e470718055d6d10c9bef5531a96e18ed369"
ORIGINAL = "f70143e7f5cee6fe67cfed927a32d4a6b1a83979885658dc9380ab21632bff9c"


def transform(original):
    if hashlib.sha256(original).hexdigest() != ORIGINAL:
        raise ValueError("ADC5 source differs from the pinned Linux baseline")
    text = original.decode()
    start = text.index("static int adc5_configure(")
    end = text.index("\nstatic int adc7_configure(", start)
    text = text[:start] + (ROOT / "kernel/power/adc5_configure.inc").read_text() + text[end:]
    anchor = "\tret = adc5_read_voltage_data(adc, data_volt);\nunlock:"
    if text.count(anchor) != 1:
        raise ValueError("ADC5 conversion anchor changed")
    # The IRQ is a wakeup; STATUS1 and the selected channel qualify the result.
    text = text.replace(anchor, """
	ret = adc5_validate_result(adc, prop);
	if (!ret)
		ret = adc5_read_voltage_data(adc, data_volt);
unlock:""", 1)
    result = text.encode()
    if hashlib.sha256(result).hexdigest() != PATCHED:
        raise ValueError("ADC5 patch output lock changed")
    return result


def stage(source):
    source = Path(source)
    if source.is_symlink() or not source.is_dir():
        raise ValueError("ADC5 source must be a regular directory")
    current = source
    for part in Path(PATH).parts:
        current = current / part
        if current.is_symlink():
            raise ValueError("refusing ADC5 source symlink")
    data = current.read_bytes()
    if hashlib.sha256(data).hexdigest() == ORIGINAL:
        current.write_bytes(transform(data))
        return
    if hashlib.sha256(data).hexdigest() == PATCHED:
        return
    raise ValueError("ADC5 source is not pristine; use a verified clean tree")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    marker = args.source / ".miseed-source-revision"
    if marker.is_symlink() or marker.read_text().strip() != "f45ed76d6791cf72ff1265ee93511175bbbc2f18":
        parser.error("ADC5 source revision mismatch")
    stage(args.source)
    print("Staged sequential PMIC5 ADC configuration and result qualification")


if __name__ == "__main__":
    main()
