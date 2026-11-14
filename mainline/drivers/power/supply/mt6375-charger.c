// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6375 switching charger (CMF Phone 1 / Tetris).
 *
 * Mainline-style port of the vendor drivers/power/nt_supply/mt6375-charger.c, built like
 * mainline's mt6370-charger.c: a cell of the MT6375 core (mainline/drivers/mfd/mt6375.c)
 * on its regmap, a power supply with the charge and input limits, BC1.2 detection on the
 * core's interrupts, and the OTG boost as a regulator ("usb-otg-vbus") that the Type-C
 * port controller takes as vbus-supply.
 *
 * Left out: the vendor charger framework (PE/PE2/PD fast charging through the charge
 * pump, AICC, water and USB killer checks, HVDCP) and the ADC readings. The vendor also
 * switches D+/D- from the USB PHY to the detection during BC1.2 (PHY_MODE_BC11_SET of its
 * T-PHY); mainline's T-PHY has no such mode, so detection runs with the PHY attached.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#define MT6375_REG_TM_PAS_CODE1	0x107	/* 0x69 opens the hidden registers */
#define MT6375_REG_CHG_BATPRO	0x117
#define MT6375_REG_CHG_TOP1	0x120
#define MT6375_REG_CHG_TOP2	0x121
#define MT6375_REG_CHG_AICR	0x122
#define MT6375_REG_CHG_MIVR	0x123
#define MT6375_REG_CHG_VCHG	0x125
#define MT6375_REG_CHG_ICHG	0x126
#define MT6375_REG_CHG_TMR	0x127
#define MT6375_REG_CHG_EOC	0x128
#define MT6375_REG_CHG_VSYS	0x129
#define MT6375_REG_CHG_WDT	0x12A
#define MT6375_REG_CHG_AICC2	0x12D
#define MT6375_REG_OTG_LBP	0x130
#define MT6375_REG_OTG_V	0x131
#define MT6375_REG_OTG_C	0x132
#define MT6375_REG_BAT_COMP	0x133
#define MT6375_REG_CHG_STAT	0x134
#define MT6375_REG_CHG_DUMY0	0x135
#define MT6375_REG_CHG_HD_BUCK5	0x140
#define MT6375_REG_BC12_FUNC	0x150
#define MT6375_REG_BC12_STAT	0x151
#define MT6375_REG_CHG_STAT0	0x1E0

#define MT6375_OTG_EN		BIT(2)	/* in CHG_TOP1 */
#define MT6375_OTG_CV_MASK	GENMASK(5, 0)
#define MT6375_OTG_CC_MASK	GENMASK(2, 0)

enum mt6375_chg_field {
	F_BATPROTECT_EN, F_CHG_EN, F_HZ, F_VBUS_OV, F_IAICR, F_ILIM_EN, F_VMIVR, F_CV, F_VREC,
	F_CC, F_CHG_TMR, F_CHG_TMR_EN, F_TE, F_IEOC, F_WDT_EN, F_AICC_ONESHOT, F_OTG_LBP,
	F_IRCMP_V, F_IRCMP_R, F_IC_STAT, F_BC12_VBUS_EN_OPT, F_BC12_EN, F_PORT_STAT,
	F_ST_PWR_RDY, F_MAX,
};

static const struct reg_field mt6375_chg_fields[F_MAX] = {
	[F_BATPROTECT_EN]	= REG_FIELD(MT6375_REG_CHG_BATPRO, 7, 7),
	[F_CHG_EN]		= REG_FIELD(MT6375_REG_CHG_TOP1, 0, 0),
	[F_HZ]			= REG_FIELD(MT6375_REG_CHG_TOP1, 3, 3),
	[F_VBUS_OV]		= REG_FIELD(MT6375_REG_CHG_TOP2, 0, 1),
	[F_IAICR]		= REG_FIELD(MT6375_REG_CHG_AICR, 0, 6),
	[F_ILIM_EN]		= REG_FIELD(MT6375_REG_CHG_AICR, 7, 7),
	[F_VMIVR]		= REG_FIELD(MT6375_REG_CHG_MIVR, 0, 6),
	[F_CV]			= REG_FIELD(MT6375_REG_CHG_VCHG, 0, 6),
	[F_VREC]		= REG_FIELD(MT6375_REG_CHG_VCHG, 7, 7),
	[F_CC]			= REG_FIELD(MT6375_REG_CHG_ICHG, 0, 5),
	[F_CHG_TMR]		= REG_FIELD(MT6375_REG_CHG_TMR, 4, 5),
	[F_CHG_TMR_EN]		= REG_FIELD(MT6375_REG_CHG_TMR, 7, 7),
	[F_TE]			= REG_FIELD(MT6375_REG_CHG_EOC, 1, 1),
	[F_IEOC]		= REG_FIELD(MT6375_REG_CHG_EOC, 4, 7),
	[F_WDT_EN]		= REG_FIELD(MT6375_REG_CHG_WDT, 3, 3),
	[F_AICC_ONESHOT]	= REG_FIELD(MT6375_REG_CHG_AICC2, 7, 7),
	[F_OTG_LBP]		= REG_FIELD(MT6375_REG_OTG_LBP, 0, 3),
	[F_IRCMP_V]		= REG_FIELD(MT6375_REG_BAT_COMP, 0, 2),
	[F_IRCMP_R]		= REG_FIELD(MT6375_REG_BAT_COMP, 4, 6),
	[F_IC_STAT]		= REG_FIELD(MT6375_REG_CHG_STAT, 0, 3),
	[F_BC12_VBUS_EN_OPT]	= REG_FIELD(MT6375_REG_BC12_FUNC, 2, 2),
	[F_BC12_EN]		= REG_FIELD(MT6375_REG_BC12_FUNC, 7, 7),
	[F_PORT_STAT]		= REG_FIELD(MT6375_REG_BC12_STAT, 0, 3),
	[F_ST_PWR_RDY]		= REG_FIELD(MT6375_REG_CHG_STAT0, 0, 0),
};

/* Value ranges of the vendor table (min, selector offset, step, max), in uA / uV */
enum { R_AICR, R_MIVR, R_CV, R_CC, R_IEOC, R_MAX };
static const struct linear_range mt6375_chg_ranges[R_MAX] = {
	[R_AICR]	= LINEAR_RANGE(100000, 2, 0x7F, 25000),		/* up to 3.225 A */
	[R_MIVR]	= LINEAR_RANGE(3900000, 0, 0x5F, 100000),	/* up to 13.4 V */
	[R_CV]		= LINEAR_RANGE(3900000, 0, 0x51, 10000),	/* up to 4.71 V */
	[R_CC]		= LINEAR_RANGE(300000, 6, 0x3F, 50000),		/* up to 3.15 A */
	[R_IEOC]	= LINEAR_RANGE(100000, 1, 0x0F, 50000),		/* up to 800 mA */
};
static const unsigned int mt6375_range_fields[R_MAX] = {
	[R_AICR] = F_IAICR, [R_MIVR] = F_VMIVR, [R_CV] = F_CV, [R_CC] = F_CC, [R_IEOC] = F_IEOC,
};

enum {
	CHG_STAT_SLEEP, CHG_STAT_VBUS_RDY, CHG_STAT_TRICKLE, CHG_STAT_PRE, CHG_STAT_FAST,
	CHG_STAT_EOC, CHG_STAT_BKGND, CHG_STAT_DONE, CHG_STAT_FAULT, CHG_STAT_OTG = 15,
};

enum {
	PORT_STAT_NOINFO, PORT_STAT_APPLE_10W = 8, PORT_STAT_SAMSUNG, PORT_STAT_APPLE_5W,
	PORT_STAT_APPLE_12W, PORT_STAT_UNKNOWN_TA, PORT_STAT_SDP, PORT_STAT_CDP, PORT_STAT_DCP,
};

static const u32 mt6375_otg_cc_ua[] = {
	500000, 700000, 1100000, 1300000, 1800000, 2100000, 2400000,
};

struct mt6375_chg {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX];
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	struct work_struct bc12_work;
	struct mutex lock;		/* attach state and BC1.2 */
	struct mutex hm_lock;		/* hidden register access count */
	int hm_count;
	bool online;
	enum power_supply_usb_type usb_type;
	u32 ichg_max, cv_max;
};

static int mt6375_get_range(struct mt6375_chg *chg, int r, u32 *val)
{
	unsigned int sel;
	int ret;

	ret = regmap_field_read(chg->fields[mt6375_range_fields[r]], &sel);
	if (ret)
		return ret;
	if (sel < mt6375_chg_ranges[r].min_sel)
		sel = mt6375_chg_ranges[r].min_sel;
	return linear_range_get_value(&mt6375_chg_ranges[r], min_t(unsigned int, sel,
				      mt6375_chg_ranges[r].max_sel), val);
}

static int mt6375_set_range(struct mt6375_chg *chg, int r, u32 val)
{
	unsigned int sel;

	linear_range_get_selector_within(&mt6375_chg_ranges[r], val, &sel);
	return regmap_field_write(chg->fields[mt6375_range_fields[r]], sel);
}

/* The OTG boost changes four hidden buck settings, like the vendor's set_boost_param() */
static int mt6375_hidden_mode(struct mt6375_chg *chg, bool on)
{
	int ret = 0;

	mutex_lock(&chg->hm_lock);
	if (on && chg->hm_count++ == 0)
		ret = regmap_write(chg->regmap, MT6375_REG_TM_PAS_CODE1, 0x69);
	else if (!on && chg->hm_count > 0 && --chg->hm_count == 0)
		ret = regmap_write(chg->regmap, MT6375_REG_TM_PAS_CODE1, 0x00);
	mutex_unlock(&chg->hm_lock);
	return ret;
}

static int mt6375_boost_param(struct mt6375_chg *chg, bool boost)
{
	static const struct { u16 reg; u8 mask, buck, boost; } p[] = {
		{ MT6375_REG_CHG_TOP2,		GENMASK(7, 6), 0x01, 0x00 },	/* clock frequency */
		{ MT6375_REG_CHG_DUMY0,		GENMASK(1, 0), 0x00, 0x03 },	/* comp clamp */
		{ MT6375_REG_CHG_HD_BUCK5,	GENMASK(7, 6), 0x01, 0x03 },	/* ramp offset */
		{ MT6375_REG_CHG_VSYS,		BIT(7),	       0x01, 0x00 },	/* bleed discharge */
	};
	int i, ret;

	ret = mt6375_hidden_mode(chg, true);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(p) && !ret; i++)
		ret = regmap_update_bits(chg->regmap, p[i].reg, p[i].mask,
					 (boost ? p[i].boost : p[i].buck) << __ffs(p[i].mask));
	mt6375_hidden_mode(chg, false);
	return ret;
}

static int mt6375_otg_enable(struct regulator_dev *rdev)
{
	struct mt6375_chg *chg = rdev_get_drvdata(rdev);
	int ret;

	ret = mt6375_boost_param(chg, true);
	if (ret)
		return ret;
	ret = regulator_enable_regmap(rdev);
	if (ret)
		mt6375_boost_param(chg, false);
	return ret;
}

static int mt6375_otg_disable(struct regulator_dev *rdev)
{
	struct mt6375_chg *chg = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_disable_regmap(rdev);
	if (ret)
		return ret;
	return mt6375_boost_param(chg, false);
}

static const struct regulator_ops mt6375_otg_ops = {
	.enable			= mt6375_otg_enable,
	.disable		= mt6375_otg_disable,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
};

static const struct regulator_desc mt6375_otg_desc = {
	.name			= "mt6375-otg-vbus",
	.of_match		= "usb-otg-vbus",
	.ops			= &mt6375_otg_ops,
	.owner			= THIS_MODULE,
	.type			= REGULATOR_VOLTAGE,
	.min_uV			= 4850000,
	.uV_step		= 25000,
	.n_voltages		= 47,
	.linear_min_sel		= 20,
	.vsel_reg		= MT6375_REG_OTG_V,
	.vsel_mask		= MT6375_OTG_CV_MASK,
	.curr_table		= mt6375_otg_cc_ua,
	.n_current_limits	= ARRAY_SIZE(mt6375_otg_cc_ua),
	.csel_reg		= MT6375_REG_OTG_C,
	.csel_mask		= MT6375_OTG_CC_MASK,
	.enable_reg		= MT6375_REG_CHG_TOP1,
	.enable_mask		= MT6375_OTG_EN,
};

static int mt6375_bc12(struct mt6375_chg *chg, bool en)
{
	return regmap_field_write(chg->fields[F_BC12_EN], en);
}

/* Input current for the detected port, as the vendor charger framework sets it */
static void mt6375_apply_port(struct mt6375_chg *chg, unsigned int port)
{
	u32 aicr;

	switch (port) {
	case PORT_STAT_SDP:
		chg->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		aicr = 500000;
		break;
	case PORT_STAT_CDP:
		chg->usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		aicr = 1500000;
		break;
	case PORT_STAT_DCP:
	case PORT_STAT_APPLE_5W:
	case PORT_STAT_APPLE_10W:
	case PORT_STAT_APPLE_12W:
	case PORT_STAT_SAMSUNG:
		chg->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		aicr = 2000000;
		break;
	case PORT_STAT_UNKNOWN_TA:
		chg->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		aicr = 1000000;
		break;
	default:
		chg->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return;
	}
	mt6375_set_range(chg, R_AICR, aicr);
	dev_info(chg->dev, "port %u: input limit %u mA\n", port, aicr / 1000);
}

/* Attach (power ready), detach and BC1.2 done all end up here */
static void mt6375_bc12_work(struct work_struct *work)
{
	struct mt6375_chg *chg = container_of(work, struct mt6375_chg, bc12_work);
	unsigned int rdy = 0, port = PORT_STAT_NOINFO, bc12_en = 0;

	mutex_lock(&chg->lock);
	regmap_field_read(chg->fields[F_ST_PWR_RDY], &rdy);
	if (!rdy) {
		chg->online = false;
		chg->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		mt6375_bc12(chg, false);
		goto out;
	}
	chg->online = true;
	regmap_field_read(chg->fields[F_BC12_EN], &bc12_en);
	regmap_field_read(chg->fields[F_PORT_STAT], &port);
	if (port == PORT_STAT_NOINFO) {
		if (!bc12_en)
			mt6375_bc12(chg, true);	/* start; fl_bc12_dn comes back here */
		goto out;
	}
	mt6375_apply_port(chg, port);
	/* a DCP keeps D+/D- shorted only while detection is on; a USB host needs the PHY */
	if (port == PORT_STAT_SDP || port == PORT_STAT_CDP)
		mt6375_bc12(chg, false);
out:
	mutex_unlock(&chg->lock);
	power_supply_changed(chg->psy);
}

static irqreturn_t mt6375_attach_irq(int irq, void *data)
{
	struct mt6375_chg *chg = data;

	schedule_work(&chg->bc12_work);
	return IRQ_HANDLED;
}

static irqreturn_t mt6375_event_irq(int irq, void *data)
{
	struct mt6375_chg *chg = data;

	power_supply_changed(chg->psy);
	return IRQ_HANDLED;
}

static irqreturn_t mt6375_fault_irq(int irq, void *data)
{
	struct mt6375_chg *chg = data;

	dev_warn(chg->dev, "charger fault (irq %d)\n", irq);
	power_supply_changed(chg->psy);
	return IRQ_HANDLED;
}

static int mt6375_get_status(struct mt6375_chg *chg, int *status)
{
	unsigned int stat, en;
	int ret;

	if (!chg->online) {
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}
	ret = regmap_field_read(chg->fields[F_IC_STAT], &stat);
	if (!ret)
		ret = regmap_field_read(chg->fields[F_CHG_EN], &en);
	if (ret)
		return ret;
	switch (stat) {
	case CHG_STAT_OTG:
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case CHG_STAT_TRICKLE:
	case CHG_STAT_PRE:
	case CHG_STAT_FAST:
	case CHG_STAT_EOC:
	case CHG_STAT_BKGND:
		*status = en ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHG_STAT_DONE:
		*status = POWER_SUPPLY_STATUS_FULL;
		break;
	default:
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}
	return 0;
}

static int mt6375_get_charge_type(struct mt6375_chg *chg, int *type)
{
	unsigned int stat;
	int ret;

	ret = regmap_field_read(chg->fields[F_IC_STAT], &stat);
	if (ret)
		return ret;
	switch (stat) {
	case CHG_STAT_TRICKLE:
	case CHG_STAT_PRE:
		*type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case CHG_STAT_FAST:
		*type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case CHG_STAT_EOC:
	case CHG_STAT_BKGND:
		*type = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	default:
		*type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}
	return 0;
}

static int mt6375_psy_get(struct power_supply *psy, enum power_supply_property psp,
			  union power_supply_propval *val)
{
	struct mt6375_chg *chg = power_supply_get_drvdata(psy);
	u32 v = 0;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chg->online;
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		return mt6375_get_status(chg, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return mt6375_get_charge_type(chg, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = mt6375_get_range(chg, R_CC, &v);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chg->ichg_max;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = mt6375_get_range(chg, R_CV, &v);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = chg->cv_max;
		return 0;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = mt6375_get_range(chg, R_AICR, &v);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = mt6375_get_range(chg, R_MIVR, &v);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = mt6375_get_range(chg, R_IEOC, &v);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = chg->usb_type;
		return 0;
	default:
		return -EINVAL;
	}
	val->intval = v;
	return ret;
}

static int mt6375_psy_set(struct power_supply *psy, enum power_supply_property psp,
			  const union power_supply_propval *val)
{
	struct mt6375_chg *chg = power_supply_get_drvdata(psy);

	if (val->intval < 0)
		return -EINVAL;
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return mt6375_set_range(chg, R_CC, min_t(u32, val->intval, chg->ichg_max));
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return mt6375_set_range(chg, R_CV, min_t(u32, val->intval, chg->cv_max));
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return mt6375_set_range(chg, R_AICR, val->intval);
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return mt6375_set_range(chg, R_MIVR, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return mt6375_set_range(chg, R_IEOC, val->intval);
	default:
		return -EINVAL;
	}
}

static int mt6375_psy_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static const enum power_supply_property mt6375_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
static const enum power_supply_usb_type mt6375_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN, POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_CDP, POWER_SUPPLY_USB_TYPE_DCP,
};
#endif

/*
 * Charge limits from the monitored battery (constant-charge-voltage-max-microvolt,
 * constant-charge-current-max-microamp, charge-term-current-microamp), else the vendor
 * defaults; the rest as the vendor device tree of the phone sets it.
 */
static int mt6375_init_settings(struct mt6375_chg *chg)
{
	struct power_supply_battery_info *bat;
	u32 ichg = 2000000, cv = 4200000, ieoc = 500000;
	int ret;

	if (!power_supply_get_battery_info(chg->psy, &bat)) {
		if (bat->constant_charge_current_max_ua > 0)
			ichg = bat->constant_charge_current_max_ua;
		if (bat->constant_charge_voltage_max_uv > 0)
			cv = bat->constant_charge_voltage_max_uv;
		if (bat->charge_term_current_ua > 0)
			ieoc = bat->charge_term_current_ua;
	}
	chg->ichg_max = ichg;
	chg->cv_max = cv;

	ret = regmap_field_write(chg->fields[F_AICC_ONESHOT], 1);
	ret = ret ?: regmap_field_write(chg->fields[F_BC12_EN], 0);
	ret = ret ?: regmap_field_write(chg->fields[F_BC12_VBUS_EN_OPT], 1);
	ret = ret ?: regmap_field_write(chg->fields[F_ILIM_EN], 0);
	ret = ret ?: regmap_field_write(chg->fields[F_WDT_EN], 0);	/* saves 1 mA */
	ret = ret ?: mt6375_set_range(chg, R_CC, ichg);
	ret = ret ?: mt6375_set_range(chg, R_CV, cv);
	ret = ret ?: mt6375_set_range(chg, R_IEOC, ieoc);
	ret = ret ?: mt6375_set_range(chg, R_MIVR, 4400000);
	ret = ret ?: regmap_field_write(chg->fields[F_VREC], 0);		/* recharge 100 mV under CV */
	ret = ret ?: regmap_field_write(chg->fields[F_VBUS_OV], 3);	/* 14.5 V */
	ret = ret ?: regmap_field_write(chg->fields[F_TE], 1);		/* end of charge */
	ret = ret ?: regmap_field_write(chg->fields[F_CHG_TMR], 1);	/* 10 h */
	ret = ret ?: regmap_field_write(chg->fields[F_CHG_TMR_EN], 1);
	ret = ret ?: regmap_field_write(chg->fields[F_IRCMP_R], 1);	/* 16.7 mOhm */
	ret = ret ?: regmap_field_write(chg->fields[F_IRCMP_V], 1);	/* 32 mV */
	ret = ret ?: regmap_field_write(chg->fields[F_OTG_LBP], 5);	/* 2.8 V */
	ret = ret ?: regmap_field_write(chg->fields[F_HZ], 0);
	ret = ret ?: regmap_field_write(chg->fields[F_CHG_EN], 1);
	return ret;
}

static int mt6375_request_irqs(struct mt6375_chg *chg, struct platform_device *pdev)
{
	static const struct {
		const char *name;
		irq_handler_t handler;
	} irqs[] = {
		{ "fl_pwr_rdy", mt6375_attach_irq },
		{ "fl_detach", mt6375_attach_irq },
		{ "fl_bc12_dn", mt6375_attach_irq },
		{ "fl_vbus_ov", mt6375_fault_irq },
		{ "fl_chg_tout", mt6375_fault_irq },
		{ "fl_wdt", mt6375_event_irq },
	};
	int i, irq, ret;

	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		irq = platform_get_irq_byname(pdev, irqs[i].name);
		if (irq < 0)
			return irq;
		ret = devm_request_threaded_irq(chg->dev, irq, NULL, irqs[i].handler,
						IRQF_ONESHOT, irqs[i].name, chg);
		if (ret)
			return dev_err_probe(chg->dev, ret, "cannot request %s\n", irqs[i].name);
	}
	return 0;
}

static int mt6375_chg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_supply_config psy_cfg = {};
	struct regulator_config reg_cfg = {};
	struct regulator_dev *rdev;
	struct mt6375_chg *chg;
	int i, ret;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;
	chg->dev = dev;
	mutex_init(&chg->lock);
	mutex_init(&chg->hm_lock);
	INIT_WORK(&chg->bc12_work, mt6375_bc12_work);
	platform_set_drvdata(pdev, chg);

	chg->regmap = dev_get_regmap(dev->parent, NULL);
	if (!chg->regmap)
		return dev_err_probe(dev, -ENODEV, "no regmap from the MT6375 core\n");
	for (i = 0; i < F_MAX; i++) {
		chg->fields[i] = devm_regmap_field_alloc(dev, chg->regmap, mt6375_chg_fields[i]);
		if (IS_ERR(chg->fields[i]))
			return dev_err_probe(dev, PTR_ERR(chg->fields[i]), "regmap field %d\n", i);
	}

	chg->psy_desc.name = "mt6375-charger";
	chg->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	chg->psy_desc.properties = mt6375_psy_props;
	chg->psy_desc.num_properties = ARRAY_SIZE(mt6375_psy_props);
	chg->psy_desc.get_property = mt6375_psy_get;
	chg->psy_desc.set_property = mt6375_psy_set;
	chg->psy_desc.property_is_writeable = mt6375_psy_writeable;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	chg->psy_desc.usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) | BIT(POWER_SUPPLY_USB_TYPE_SDP) |
				  BIT(POWER_SUPPLY_USB_TYPE_CDP) | BIT(POWER_SUPPLY_USB_TYPE_DCP);
#else
	chg->psy_desc.usb_types = mt6375_usb_types;
	chg->psy_desc.num_usb_types = ARRAY_SIZE(mt6375_usb_types);
#endif
	psy_cfg.drv_data = chg;
	psy_cfg.fwnode = dev_fwnode(dev);
	chg->psy = devm_power_supply_register(dev, &chg->psy_desc, &psy_cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(dev, PTR_ERR(chg->psy), "cannot register the power supply\n");

	ret = mt6375_init_settings(chg);
	if (ret)
		return dev_err_probe(dev, ret, "cannot apply the charger settings\n");

	reg_cfg.dev = dev;
	reg_cfg.regmap = chg->regmap;
	reg_cfg.driver_data = chg;
	rdev = devm_regulator_register(dev, &mt6375_otg_desc, &reg_cfg);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev), "cannot register the OTG regulator\n");

	ret = mt6375_request_irqs(chg, pdev);
	if (ret)
		return ret;

	schedule_work(&chg->bc12_work);	/* a cable that is already in */
	dev_info(dev, "charge up to %u mA / %u mV\n", chg->ichg_max / 1000, chg->cv_max / 1000);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void mt6375_chg_remove(struct platform_device *pdev)
#else
static int mt6375_chg_remove(struct platform_device *pdev)
#endif
{
	struct mt6375_chg *chg = platform_get_drvdata(pdev);

	cancel_work_sync(&chg->bc12_work);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id mt6375_chg_of_match[] = {
	{ .compatible = "mediatek,mt6375-chg" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6375_chg_of_match);

static struct platform_driver mt6375_chg_driver = {
	.driver = {
		.name = "mt6375-charger",
		.of_match_table = mt6375_chg_of_match,
	},
	.probe = mt6375_chg_probe,
	.remove = mt6375_chg_remove,
};
module_platform_driver(mt6375_chg_driver);

MODULE_DESCRIPTION("MediaTek MT6375 charger (CMF Phone 1)");
MODULE_LICENSE("GPL");
