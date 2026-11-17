// SPDX-License-Identifier: GPL-2.0-only
/* Generic local TCPM proposal: select a firmware-declared sink profile through
 * power_supply INPUT_VOLTAGE_LIMIT (uV), enforced on fixed and PPS requests.
 * No raw PDO or board register ABI.
 * The first firmware profile is the attach/reset default. All calls hold lock.
 */
static int tcpm_profile_fixed_mv(const struct pd_data *data)
{
	int i, mv = 0;

	/* Never change source capabilities through a sink voltage limit. */
	if (data->source_desc.pdo[0])
		return -EOPNOTSUPP;
	for (i = 0; i < PDO_MAX_OBJECTS && data->sink_desc.pdo[i]; i++) {
		u32 pdo = data->sink_desc.pdo[i];

		if (pdo_type(pdo) == PDO_TYPE_FIXED)
			mv = max(mv, (int)pdo_fixed_voltage(pdo));
		else if (pdo_type(pdo) != PDO_TYPE_APDO)
			return -EOPNOTSUPP;
	}
	return mv;
}

/* Type-C's setter only creates/removes a link; it cannot replace one. Keep
 * capabilities unchanged if publication fails, and restore the previous link.
 */
static int tcpm_psy_link_pd(struct tcpm_port *port, struct usb_power_delivery *pd)
{
	int ret, restore;

	ret = typec_port_set_usb_power_delivery(port->typec_port, NULL);
	if (ret)
		return ret;
	port->psy_pd_link_dirty = true;
	ret = typec_port_set_usb_power_delivery(port->typec_port, pd);
	if (ret) {
		restore = typec_port_set_usb_power_delivery(port->typec_port, port->selected_pd);
		port->psy_pd_link_dirty = !!restore;
		if (restore)
			tcpm_log(port, "PD profile link restore failed: %d", restore);
	} else {
		port->psy_pd_link_dirty = false;
	}
	return ret;
}

/* Serialize the standard Type-C profile selector with a power_supply
 * voltage-limit request. Changing capabilities behind that request would
 * make its readback stale and could remove the current PPS APDO. An identical
 * selection repairs a failed link; reset releases the power_supply selection.
 * Caller holds port->lock, before any capabilities or links are changed.
 */
static int tcpm_psy_check_pd(struct tcpm_port *port, struct usb_power_delivery *pd)
{
	if (port->psy_selected_profile && port->selected_pd != pd)
		return -EBUSY;
	if (port->psy_pd_link_dirty)
		return tcpm_psy_link_pd(port, port->selected_pd);
	return 0;
}

static void tcpm_psy_restore_profile(struct tcpm_port *port)
{
	struct pd_data *data;
	int i, ret;

	if (!port->psy_selected_profile && !port->psy_pd_link_dirty)
		return;
	data = port->pd_list[0];
	ret = tcpm_psy_link_pd(port, data->pd);
	if (ret)
		tcpm_log(port, "PD default profile link failed: %d", ret);
	for (i = 0; i < PDO_MAX_OBJECTS && data->sink_desc.pdo[i]; i++)
		port->snk_pdo[i] = data->sink_desc.pdo[i];
	port->nr_snk_pdo = i;
	port->operating_snk_mw = data->operating_snk_mw;
	port->port_sink_caps = data->sink_cap;
	port->selected_pd = data->pd;
	/* Always restore safe attach capabilities, even if sysfs publication
	 * failed. Keep a retry marker; getters must not report that as success.
	 */
	port->psy_selected_profile = !!ret;
	port->psy_pd_link_dirty = !!ret;
	port->psy_voltage_limit_mv = 0;
}

static int tcpm_psy_get_fixed_limit(struct tcpm_port *port,
				  union power_supply_propval *val)
{
	int i, ret = -ENODATA;

	mutex_lock(&port->lock);
	if (port->psy_pd_link_dirty ||
	    (port->psy_selected_profile && !port->psy_voltage_limit_mv)) {
		ret = -EIO;
		goto out;
	}
	if (port->state != SNK_READY || port->update_sink_caps) {
		ret = -EAGAIN;
		goto out;
	}
	if (port->psy_voltage_limit_mv) {
		val->intval = port->psy_voltage_limit_mv * 1000;
		ret = 0;
		goto out;
	}
	for (i = 0; i < port->pd_count; i++) {
		if (port->pd_list[i]->pd != port->selected_pd)
			continue;
		ret = tcpm_profile_fixed_mv(port->pd_list[i]);
		if (ret > 0) {
			val->intval = ret * 1000;
			ret = 0;
		}
		break;
	}
out:
	mutex_unlock(&port->lock);
	return ret;
}

static int tcpm_psy_set_fixed_limit(struct tcpm_port *port, int uv)
{
	struct pd_data *best = NULL;
	int i, mv, best_mv = 0, ret = 0;

	if (uv < 5000000 || uv % 1000)
		return -EINVAL;
	mutex_lock(&port->lock);
	if (port->pwr_role != TYPEC_SINK || !port->pd_capable) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (port->state != SNK_READY || port->update_sink_caps) {
		ret = -EAGAIN;
		goto out;
	}
	if (port->psy_pd_link_dirty) {
		ret = tcpm_psy_link_pd(port, port->selected_pd);
		if (ret)
			goto out;
	}
	if (port->pps_data.active) {
		/* Lower the live PPS contract before lowering its ceiling. */
		if (uv / 1000 < port->supply_voltage) {
			ret = -EINVAL;
			goto out;
		}
		port->psy_voltage_limit_mv = uv / 1000;
		port->psy_selected_profile = true;
		goto out;
	}
	for (i = 0; i < port->pd_count; i++) {
		mv = tcpm_profile_fixed_mv(port->pd_list[i]);
		if (mv > best_mv && mv <= uv / 1000) {
			best = port->pd_list[i];
			best_mv = mv;
		}
	}
	if (!best) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (best->pd == port->selected_pd) {
		if (port->psy_selected_profile && !port->psy_voltage_limit_mv) {
			ret = tcpm_psy_link_pd(port, best->pd);
			if (ret)
				goto out;
		}
		port->psy_voltage_limit_mv = uv / 1000;
		port->psy_selected_profile = true;
		goto out;
	}
	ret = tcpm_psy_link_pd(port, best->pd);
	if (ret)
		goto out;
	port->upcoming_state = SNK_NEGOTIATE_CAPABILITIES;
	ret = tcpm_ams_start(port, POWER_NEGOTIATION);
	if (ret) {
		port->upcoming_state = INVALID_STATE;
		if (tcpm_psy_link_pd(port, port->selected_pd))
			tcpm_log(port, "PD busy profile link rollback failed");
		goto out;
	}
	for (i = 0; i < PDO_MAX_OBJECTS && best->sink_desc.pdo[i]; i++)
		port->snk_pdo[i] = best->sink_desc.pdo[i];
	port->nr_snk_pdo = i;
	port->operating_snk_mw = best->operating_snk_mw;
	port->port_sink_caps = best->sink_cap;
	port->selected_pd = best->pd;
	port->psy_selected_profile = true;
	port->psy_voltage_limit_mv = uv / 1000;
	port->update_sink_caps = true;
	/* Enqueued is not negotiated: callers must wait for SNK_READY/readback. */
	ret = -EAGAIN;
out:
	mutex_unlock(&port->lock);
	return ret;
}
