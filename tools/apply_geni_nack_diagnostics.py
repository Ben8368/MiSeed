"""Stage minimal GENI I2C NACK phase diagnostics in the pinned Linux tree."""

# SPDX-License-Identifier: AGPL-3.0-only

from pathlib import Path


PATH = "drivers/i2c/busses/i2c-qcom-geni.c"
OLD = b'''\t\tif (m_stat & M_GP_IRQ_1_EN)
\t\t\tgeni_i2c_err(gi2c, NACK);'''
# Adapt the phase classification from upstream Linux 1275cbed80a7
# (Praveen Talari and Naresh Maramaina, GPL-2.0); keep the pinned recovery path.
NEW = b'''\t\tif (m_stat & M_GP_IRQ_1_EN) {
\t\t\tbool ambiguous = dma || dm_rx_st & DM_I2C_CB_ERR ||
\t\t\t\t(m_stat & ~(M_CMD_DONE_EN | M_CMD_FAILURE_EN | M_GP_IRQ_1_EN));

\t\t\tif (cur) {
\t\t\t\tu32 gp_length = readl_relaxed(base + SE_GENI_M_GP_LENGTH);
\t\t\t\tbool rd = cur->flags & I2C_M_RD;
\t\t\t\tconst char *phase = !gp_length ? "address" : rd ? "unknown" : "data";

\t\t\t\t/* Reserve write -ENXIO for an isolated FIFO address NACK. */
\t\t\t\tif (!rd && (gp_length || ambiguous) && !gi2c->err)
\t\t\t\t\tgi2c->err = -EIO;
\t\t\t\tdev_warn_ratelimited(gi2c->se.dev,
\t\t\t\t\t"I2C NACK bus=%d addr=0x%02x dir=%s phase=%s gp_length=%u irq=0x%x "
\t\t\t\t\t"clk_sel=0x%x m_clk_cfg=0x%x scl_counters=0x%x\\n",
\t\t\t\t\tgi2c->adap.nr, cur->addr, rd ? "read" : "write",
\t\t\t\t\tphase, gp_length, m_stat,
\t\t\t\t\treadl_relaxed(base + SE_GENI_CLK_SEL),
\t\t\t\t\treadl_relaxed(base + GENI_SER_M_CLK_CFG),
\t\t\t\t\treadl_relaxed(base + SE_I2C_SCL_COUNTERS));
\t\t\t} else if (!gi2c->err) {
\t\t\t\tgi2c->err = -EIO;
\t\t\t}
\t\t\tgeni_i2c_err(gi2c, NACK);
\t\t}'''


def transform(original: bytes) -> bytes:
    if original.count(NEW) == 1 and OLD not in original:
        return original
    if original.count(OLD) != 1:
        raise ValueError("pinned GENI I2C NACK handler changed; refusing patch")
    return original.replace(OLD, NEW, 1)


def apply(source: Path) -> None:
    path = source / PATH
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"invalid GENI source path: {path}")
    original = path.read_bytes()
    updated = transform(original)
    if original != updated:
        path.write_bytes(updated)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        apply(args.source)
    except (OSError, ValueError) as error:
        parser.exit(1, f"GENI NACK diagnostics staging failed: {error}\n")
    print("Staged payload-free GENI I2C NACK phase diagnostics")
