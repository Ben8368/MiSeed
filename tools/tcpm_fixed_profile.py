"""Wire the local generic fixed-PD profile proposal into pinned TCPM."""
# SPDX-License-Identifier: AGPL-3.0-only
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "kernel/power/tcpm_fixed_profile.inc"


def transform(text, replace):
    text = replace(text, "\tstruct usb_power_delivery *selected_pd;",
                   "\tstruct usb_power_delivery *selected_pd;\n\tbool psy_selected_profile;\n"
                   "\tunsigned int psy_voltage_limit_mv;")
    # Apply the voltage ceiling to both fixed selection and PPS output.
    check = ("\n\t\tif (port->psy_voltage_limit_mv && "
             "max_src_mv > port->psy_voltage_limit_mv)\n\t\t\tcontinue;\n")
    start = text.index("static int tcpm_pd_select_pdo(")
    end = text.index("static unsigned int tcpm_pd_select_pps_apdo(", start)
    section = text[start:end]
    anchor = "\n\t\tswitch (type) {\n\t\tcase PDO_TYPE_FIXED:\n\t\tcase PDO_TYPE_VAR:"
    section = replace(section, anchor, check + anchor)
    text = text[:start] + section + text[end:]
    anchor = "\tu32 pdo, src;\n\n\tfor (i = 1; i < port->nr_source_caps; ++i) {"
    text = replace(text, anchor, "\tu32 pdo, src;\n\n"
                   "\tif (port->psy_voltage_limit_mv && port->pps_data.req_out_volt >\n"
                   "\t    port->psy_voltage_limit_mv)\n\t\treturn 0;\n\n"
                   "\tfor (i = 1; i < port->nr_source_caps; ++i) {")
    anchor = "\ttarget_mw = (port->current_limit * req_out_volt) / 1000;"
    text = replace(text, anchor, "\tif (port->psy_voltage_limit_mv && req_out_volt > port->psy_voltage_limit_mv) {\n"
                   "\t\tret = -EINVAL;\n\t\tgoto port_unlock;\n\t}\n\n" + anchor)
    anchor = "static void tcpm_reset_port(struct tcpm_port *port)\n{"
    text = replace(text, anchor, PROFILE.read_text(encoding="utf-8") + "\n" + anchor +
                   "\n\ttcpm_psy_restore_profile(port);")
    text = replace(text, "static enum power_supply_property tcpm_psy_props[] = {",
                   "static enum power_supply_property tcpm_psy_props[] = {\n"
                   "\tPOWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,")
    anchor = "\tcase POWER_SUPPLY_PROP_USB_TYPE:\n\t\tval->intval = port->usb_type;"
    text = replace(text, anchor, "\tcase POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:\n"
                   "\t\tret = tcpm_psy_get_fixed_limit(port, val);\n\t\tbreak;\n" + anchor)
    anchor = "\tcase POWER_SUPPLY_PROP_ONLINE:\n\t\tret = tcpm_psy_set_online(port, val);"
    text = replace(text, anchor, "\tcase POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:\n"
                   "\t\tret = tcpm_psy_set_fixed_limit(port, val->intval);\n\t\tbreak;\n" + anchor)
    anchor = "\tcase POWER_SUPPLY_PROP_CURRENT_NOW:\n\t\treturn 1;"
    return replace(text, anchor, "\tcase POWER_SUPPLY_PROP_CURRENT_NOW:\n"
                   "\tcase POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:\n\t\treturn 1;")
