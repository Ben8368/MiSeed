// SPDX-License-Identifier: AGPL-3.0-only
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include "umi_csot_commands.h"
struct drm_panel { bool prepared; };
struct mipi_dsi_device { int dev; };
struct drm_dsc_config { int unused; };
struct drm_dsc_picture_parameter_set { int unused; };
struct gpio_desc { int value; };
struct mutex { int unused; };
struct backlight_device { void *data; int brightness; };
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define mutex_lock(x) ((void)(x))
#define mutex_unlock(x) ((void)(x))
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
static int command, fail_command = -1, fail_compression, fail_pps, brightness;
static int short_write, msm_packet_count;
static int fail_off, fail_sleep, off_calls, sleep_calls;
static void msleep(unsigned int n) { (void)n; }
static void usleep_range(unsigned int a, unsigned int b) { (void)a; (void)b; }
static int gpiod_direction_output(struct gpio_desc *g, int value) { g->value = value; return 0; }
static void gpiod_set_value_cansleep(struct gpio_desc *g, int value) { g->value = value; }
static int mipi_dsi_compression_mode(struct mipi_dsi_device *d, bool enable) { return fail_compression ? -EIO : 0; }
static void drm_dsc_pps_payload_pack(void *a, void *b) { }
static int mipi_dsi_picture_parameter_set(struct mipi_dsi_device *d, void *pps) { return fail_pps ? -EIO : 0; }
static int mipi_dsi_dcs_write_buffer(struct mipi_dsi_device *d, const void *p, size_t n)
{
    int index = command++;
    assert(n == umi_csot_init[index].len);
    assert(n > 0 && n <= 32);
    if (index == fail_command) return -EIO;
    /* Mainline MSM counts the 4-byte header and aligns the packet to 4. */
    if (msm_packet_count) return n <= 2 ? 4 : ((int)n + 7) & ~3;
    return (int)n - short_write;
}
static int mipi_dsi_dcs_set_display_off(struct mipi_dsi_device *d) { off_calls++; return fail_off ? -EIO : 0; }
static int mipi_dsi_dcs_enter_sleep_mode(struct mipi_dsi_device *d) { sleep_calls++; return fail_sleep ? -ETIMEDOUT : 0; }
static int mipi_dsi_dcs_set_display_brightness(struct mipi_dsi_device *d, int v) { brightness = v; return 0; }
static void *bl_get_data(struct backlight_device *bl) { return bl->data; }
static int backlight_get_brightness(struct backlight_device *bl) { return bl->brightness; }
/* PANEL_UNDER_TEST */
/* Pinned DRM core keeps prepared=true on a negative unprepare result. */
static void core_prepare(struct drm_panel *panel) {
    if (!panel->prepared && !umi_csot_prepare(panel)) panel->prepared = true;
}
static void core_unprepare(struct drm_panel *panel) {
    if (panel->prepared && !umi_csot_unprepare(panel)) panel->prepared = false;
}
int main(void)
{
    struct mipi_dsi_device d = {0};
    struct gpio_desc reset = {0};
    struct umi_csot c = {.dsi = &d, .reset = &reset};
    struct backlight_device bl = {.data = &c, .brightness = 0};
    for (int i = 0; i < 59; i++) {
        command = 0; fail_command = i;
        assert(umi_csot_prepare(&c.panel) == -EIO);
        assert(reset.value == 1 && !c.prepared && command == i + 1);
    }
    fail_command = -1; command = 0; fail_compression = 1;
    assert(umi_csot_prepare(&c.panel) == -EIO && command == 0);
    fail_compression = 0; fail_pps = 1;
    assert(umi_csot_prepare(&c.panel) == -EIO && command == 0);
    fail_pps = 0; short_write = 1;
    assert(umi_csot_prepare(&c.panel) == -EIO && !c.prepared);
    short_write = 0; command = 0;
    assert(umi_csot_prepare(&c.panel) == 0 && c.prepared && command == 59);
    assert(umi_csot_unprepare(&c.panel) == 0 && !c.prepared);
    msm_packet_count = 1; command = 0;
    assert(umi_csot_prepare(&c.panel) == 0 && c.prepared && command == 59);
    brightness = 42;
    assert(umi_csot_brightness(&bl) == 0 && brightness == 0);
    assert(umi_csot_unprepare(&c.panel) == 0 && !c.prepared && reset.value == 1);
    brightness = 42;
    assert(umi_csot_brightness(&bl) == 0 && brightness == 42);
    for (int failure = 1; failure <= 3; failure++) {
        fail_off = failure & 1; fail_sleep = failure & 2;
        command = 0; core_prepare(&c.panel);
        assert(c.prepared && c.panel.prepared && command == 59);
        int before_off = off_calls, before_sleep = sleep_calls;
        core_unprepare(&c.panel);
        assert(!c.prepared && !c.panel.prepared && reset.value == 1);
        assert(off_calls == before_off + 1 && sleep_calls == before_sleep + 1);
        brightness = 42;
        assert(umi_csot_brightness(&bl) == 0 && brightness == 42);
        command = 0; core_prepare(&c.panel);
        assert(c.prepared && c.panel.prepared && command == 59);
        fail_off = fail_sleep = 0;
        core_unprepare(&c.panel);
    }
    return 0;
}
