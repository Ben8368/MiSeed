// SPDX-License-Identifier: GPL-2.0-only
/* Fixed Umi CSOT DRM panel. Board data attribution: NOTICE.md.
 * Rails remain firmware-owned pending qualification; no vendor UAPI.
 */
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include "umi_csot_commands.h"

struct umi_csot {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset;
	struct mutex lock;
	bool prepared;
};
static struct umi_csot *to_csot(struct drm_panel *p)
{
	return container_of(p, struct umi_csot, panel);
}
static const struct drm_display_mode umi_csot_mode = {
	.clock = 235655,
	.hdisplay = 1080, .hsync_start = 1096, .hsync_end = 1104, .htotal = 1112,
	.vdisplay = 2340, .vsync_start = 2940, .vsync_end = 2972, .vtotal = 3532,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};
static int umi_csot_prepare(struct drm_panel *panel)
{
	struct umi_csot *c = to_csot(panel);
	struct drm_dsc_picture_parameter_set pps;
	int ret;
	unsigned int i;

	mutex_lock(&c->lock);
	/* ACTIVE_LOW: physical high 1ms, low 1ms, high 10ms. */
	ret = gpiod_direction_output(c->reset, 0);
	if (ret)
		goto out;
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(c->reset, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(c->reset, 0);
	msleep(10);
	ret = mipi_dsi_compression_mode(c->dsi, true);
	if (ret < 0)
		goto reset;
	/* MSM DSI computes RC parameters before panel prepare. */
	drm_dsc_pps_payload_pack(&pps, &c->dsc);
	ret = mipi_dsi_picture_parameter_set(c->dsi, &pps);
	if (ret < 0)
		goto reset;
	for (i = 0; i < ARRAY_SIZE(umi_csot_init); i++) {
		const struct umi_csot_cmd *cmd = &umi_csot_init[i];
		ret = mipi_dsi_dcs_write_buffer(c->dsi, cmd->data, cmd->len);
		/* MSM returns the aligned DMA packet size, including its header. */
		if (ret < cmd->len) {
			ret = ret < 0 ? ret : -EIO;
			goto reset;
		}
		if (cmd->delay_ms)
			msleep(cmd->delay_ms);
	}
	c->prepared = true;
	dev_info(&c->dsi->dev, "CSOT prepared: DSC and %zu DCS commands complete\n",
		 ARRAY_SIZE(umi_csot_init));
	ret = 0;
	goto out;
reset:
	gpiod_set_value_cansleep(c->reset, 1);
	dev_err(&c->dsi->dev, "CSOT prepare failed: %d\n", ret);
out:
	mutex_unlock(&c->lock);
	return ret;
}
static int umi_csot_unprepare(struct drm_panel *panel)
{
	struct umi_csot *c = to_csot(panel);
	int ret = 0, sleep_ret;

	mutex_lock(&c->lock);
	if (c->prepared) {
		ret = mipi_dsi_dcs_set_display_off(c->dsi);
		sleep_ret = mipi_dsi_dcs_enter_sleep_mode(c->dsi);
		if (!ret)
			ret = sleep_ret;
		msleep(120);
		gpiod_set_value_cansleep(c->reset, 1);
		c->prepared = false;
		if (ret)
			dev_err(&c->dsi->dev, "CSOT shutdown command failed: %d\n", ret);
	}
	mutex_unlock(&c->lock);
	/* Reset completed: let DRM clear its prepared flag and permit reprepare. */
	return 0;
}
static int umi_csot_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode = drm_mode_duplicate(connector->dev, &umi_csot_mode);

	if (!mode)
		return -ENOMEM;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	/* Physical-dimension units in reference data remain unqualified. */
	return 1;
}
static const struct drm_panel_funcs umi_csot_funcs = {
	.prepare = umi_csot_prepare, .unprepare = umi_csot_unprepare,
	.get_modes = umi_csot_modes,
};
static int umi_csot_brightness(struct backlight_device *bl)
{
	struct umi_csot *c = bl_get_data(bl);
	int ret = 0;

	mutex_lock(&c->lock);
	if (c->prepared) {
		ret = mipi_dsi_dcs_set_display_brightness(c->dsi, backlight_get_brightness(bl));
		dev_info(&c->dsi->dev, "CSOT brightness=%u transfer_rc=%d\n",
			 backlight_get_brightness(bl), ret);
	} else
		dev_info(&c->dsi->dev, "CSOT brightness deferred: panel not prepared\n");
	mutex_unlock(&c->lock);
	return ret < 0 ? ret : 0;
}
static const struct backlight_ops umi_csot_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME, .update_status = umi_csot_brightness,
};
static int umi_csot_probe(struct mipi_dsi_device *dsi)
{
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW, .max_brightness = 2047, .brightness = 1,
	};
	struct device *dev = &dsi->dev;
	struct umi_csot *c;
	int ret;

	c = devm_drm_panel_alloc(dev, struct umi_csot, panel,
				&umi_csot_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(c))
		return PTR_ERR(c);
	c->dsi = dsi;
	mutex_init(&c->lock);
	c->reset = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(c->reset))
		return dev_err_probe(dev, PTR_ERR(c->reset), "reset GPIO unavailable\n");
	mipi_dsi_set_drvdata(dsi, c);
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	dsi->hs_rate = 682000000;
	dsi->dsc = &c->dsc;
	c->dsc.dsc_version_major = 1;
	c->dsc.dsc_version_minor = 1;
	c->dsc.slice_height = 12;
	c->dsc.slice_width = 540;
	c->dsc.slice_count = 2;
	c->dsc.bits_per_component = 8;
	c->dsc.bits_per_pixel = 8 << 4;
	c->dsc.block_pred_enable = true;
	/* Power the upstream DSI bridge before sending panel commands. */
	c->panel.prepare_prev_first = true;
	c->panel.backlight = devm_backlight_device_register(dev, dev_name(dev),
			dev, c, &umi_csot_bl_ops, &props);
	if (IS_ERR(c->panel.backlight))
		return PTR_ERR(c->panel.backlight);
	drm_panel_add(&c->panel);
	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&c->panel);
	return ret;
}
static void umi_csot_shutdown(struct mipi_dsi_device *dsi)
{
	struct umi_csot *c = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&c->panel);
	drm_panel_unprepare(&c->panel);
}
static void umi_csot_remove(struct mipi_dsi_device *dsi)
{
	struct umi_csot *c = mipi_dsi_get_drvdata(dsi);

	umi_csot_shutdown(dsi);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&c->panel);
}
static const struct of_device_id umi_csot_match[] = {
	{ .compatible = "xiaomi,umi-csot" }, { }
};
MODULE_DEVICE_TABLE(of, umi_csot_match);
static struct mipi_dsi_driver umi_csot_driver = {
	.probe = umi_csot_probe, .remove = umi_csot_remove, .shutdown = umi_csot_shutdown,
	.driver = { .name = "panel-xiaomi-umi-csot", .of_match_table = umi_csot_match },
};
module_mipi_dsi_driver(umi_csot_driver);
MODULE_DESCRIPTION("Xiaomi Umi CSOT j2_mp_42_02_0b DRM panel");
MODULE_LICENSE("GPL");
