// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6375 USB Type-C port controller (CMF Phone 1 / Tetris), on the TCPCI core.
 *
 * Mainline-style port of the Type-C part of the vendor
 * drivers/misc/mediatek/typec/tcpc/tcpc_mt6375.c, built like mainline's tcpci_mt6370.c:
 * the registers are standard TCPCI (bank 0 of the MT6375 regmap), the vendor driver's
 * init sequence goes in tcpci_data.init, and the port's alert comes as the core's
 * "pd_evt" interrupt. Water detection, floating ground and the vendor's own PD stack
 * are left out; TCPM does the protocol.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>
#include <linux/version.h>

#define MT6375_REG_PHYCTRL1	0x80
#define MT6375_REG_PHYCTRL2	0x81
#define MT6375_REG_PHYCTRL3	0x82
#define MT6375_REG_PHYCTRL7	0x86
#define MT6375_REG_VCONCTRL2	0x8B
#define MT6375_REG_VCONCTRL3	0x8C
#define MT6375_REG_SYSCTRL1	0x8F
#define MT6375_REG_MTMASK1	0x91	/* vendor interrupt masks 1..7 */
#define MT6375_REG_PHYCTRL9	0xAC
#define MT6375_REG_SYSCTRL3	0xB0
#define MT6375_REG_TCPCCTRL1	0xB1
#define MT6375_REG_TCPCCTRL2	0xB2
#define MT6375_REG_TCPCCTRL3	0xB3	/* 16 bit, little endian */
#define MT6375_REG_LPWRCTRL3	0xBB
#define MT6375_REG_WATCHDOGCTRL	0xBE
#define MT6375_REG_HILOCTRL9	0xC8
#define MT6375_REG_SHIELDCTRL1	0xCA

#define MT6375_SW_RESET		0x01
#define MT6375_VCON_RVPCPEN	BIT(1)
#define MT6375_VCON_RVPEN	BIT(3)
#define MT6375_VCON_OVCCEN	BIT(7)
#define MT6375_VCON_PROTEN	(MT6375_VCON_RVPEN | MT6375_VCON_OVCCEN | MT6375_VCON_RVPCPEN)
#define MT6375_VCON_OVPEN	BIT(4)
#define MT6375_VBUSDISCHG_OPT	BIT(6)
#define MT6375_OPEN40MS_EN	BIT(4)
#define MT6375_RP_DUTY_30	308	/* DRP duty (dcSRC + 1) / 1024 */

#define MT6375_VENDOR_ID	0x29CF
#define MT6375_PRODUCT_ID	0x6375

struct mt6375_tcpc {
	struct device *dev;
	struct regulator *vbus;
	struct tcpci *tcpci;
	struct tcpci_data tcpci_data;
};

/* The vendor mt6375_tcpc_init() without water detection and floating ground */
static const struct reg_sequence mt6375_reg_init[] = {
	REG_SEQ(MT6375_REG_TCPCCTRL1, 0x0A, 0),		/* tTCPCFilter 250 us */
	REG_SEQ(MT6375_REG_TCPCCTRL2, 0x00, 0),		/* DRP toggle cycle 51.2 ms */
	REG_SEQ(MT6375_REG_TCPCCTRL3, MT6375_RP_DUTY_30 & 0xff, 0),
	REG_SEQ(MT6375_REG_TCPCCTRL3 + 1, MT6375_RP_DUTY_30 >> 8, 0),
	REG_SEQ(MT6375_REG_PHYCTRL1, 0x74, 0),		/* toggle count 7, OSC_FREQ_CFG 1 */
	REG_SEQ(MT6375_REG_PHYCTRL2, 0x3A, 0),		/* PHY CDR threshold */
	REG_SEQ(MT6375_REG_PHYCTRL3, 0x82, 0),		/* transition window 43.29 us */
	REG_SEQ(MT6375_REG_PHYCTRL7, 0x36, 0),		/* BMC decoder idle 17.982 us */
	REG_SEQ(MT6375_REG_PHYCTRL9, 0x3C, 0),		/* retry period 26.208 us */
	REG_SEQ(MT6375_REG_VCONCTRL3, 0x11, 0),		/* Vconn current limit, OCP 100 mA, OVP */
	REG_SEQ(MT6375_REG_HILOCTRL9, 0xAA, 0),		/* HILOCCFILTER 250 us */
	REG_SEQ(MT6375_REG_LPWRCTRL3, 0xD8, 0),		/* low power LDO 2 V */
	REG_SEQ(MT6375_REG_SYSCTRL1, 0xB8, 1000),	/* shipping off, autoidle, timeout 6.4 ms */
};

static int mt6375_tcpc_init(struct tcpci *tcpci, struct tcpci_data *data)
{
	struct regmap *regmap = data->regmap;
	u8 mask[7] = { };
	int ret;

	ret = regmap_write(regmap, MT6375_REG_SYSCTRL3, MT6375_SW_RESET);
	if (ret)
		return ret;
	usleep_range(1000, 2000);

	ret = regmap_multi_reg_write(regmap, mt6375_reg_init, ARRAY_SIZE(mt6375_reg_init));
	if (ret)
		return ret;

	/* the vendor interrupts (water, OTP, VBUS levels) are not used: all masked */
	ret = regmap_bulk_write(regmap, MT6375_REG_MTMASK1, mask, sizeof(mask));
	if (ret)
		return ret;

	/* CC open 40 ms when the PMIC's SYS goes under voltage */
	ret = regmap_set_bits(regmap, MT6375_REG_SHIELDCTRL1, MT6375_OPEN40MS_EN);
	if (ret)
		return ret;
	/* alert when CCStatus.Looking4Connection changes */
	ret = regmap_set_bits(regmap, TCPC_TCPC_CTRL, TCPC_TCPC_CTRL_EN_LK4CONN_ALRT);
	if (ret)
		return ret;
	/* auto discharge timer (about 12 mA less), no bleed discharge (about 2 mA less) */
	ret = regmap_set_bits(regmap, MT6375_REG_WATCHDOGCTRL, MT6375_VBUSDISCHG_OPT);
	if (ret)
		return ret;
	return regmap_clear_bits(regmap, TCPC_POWER_CTRL, TCPC_POWER_CTRL_BLEED_DISCHARGE);
}

/* Vconn protection on before the TCPCI core switches Vconn on, off after */
static int mt6375_tcpc_set_vconn(struct tcpci *tcpci, struct tcpci_data *data, bool enable)
{
	struct regmap *regmap = data->regmap;
	int ret;

	if (!enable)
		return regmap_clear_bits(regmap, MT6375_REG_VCONCTRL2, MT6375_VCON_PROTEN);

	ret = regmap_set_bits(regmap, MT6375_REG_VCONCTRL3, MT6375_VCON_OVPEN);
	if (ret)
		return ret;
	return regmap_set_bits(regmap, MT6375_REG_VCONCTRL2, MT6375_VCON_PROTEN);
}

/* VBUS as source comes from the charger's OTG boost, a regulator */
static int mt6375_tcpc_set_vbus(struct tcpci *tcpci, struct tcpci_data *data,
				bool source, bool sink)
{
	struct mt6375_tcpc *priv = container_of(data, struct mt6375_tcpc, tcpci_data);
	int ret;

	ret = regulator_is_enabled(priv->vbus);
	if (ret < 0)
		return ret;
	if (ret && !source)
		return regulator_disable(priv->vbus);
	if (!ret && source)
		return regulator_enable(priv->vbus);
	return 0;
}

static irqreturn_t mt6375_tcpc_irq(int irq, void *dev_id)
{
	struct mt6375_tcpc *priv = dev_id;

	return tcpci_irq(priv->tcpci);
}

static int mt6375_tcpc_check_ids(struct mt6375_tcpc *priv)
{
	struct regmap *regmap = priv->tcpci_data.regmap;
	u16 vid, pid, did;
	int ret;

	ret = regmap_raw_read(regmap, TCPC_VENDOR_ID, &vid, sizeof(vid));
	if (!ret)
		ret = regmap_raw_read(regmap, TCPC_PRODUCT_ID, &pid, sizeof(pid));
	if (!ret)
		ret = regmap_raw_read(regmap, TCPC_BCD_DEV, &did, sizeof(did));
	if (ret)
		return dev_err_probe(priv->dev, ret, "cannot read the TCPC ids\n");
	vid = le16_to_cpu((__force __le16)vid);
	pid = le16_to_cpu((__force __le16)pid);
	did = le16_to_cpu((__force __le16)did);
	if (vid != MT6375_VENDOR_ID || pid != MT6375_PRODUCT_ID)
		return dev_err_probe(priv->dev, -ENODEV, "not an MT6375 TCPC (%04x:%04x)\n",
				     vid, pid);
	dev_info(priv->dev, "MT6375 TCPC, device id 0x%04x\n", did);
	return 0;
}

static void mt6375_tcpc_unregister(void *tcpci)
{
	tcpci_unregister_port(tcpci);
}

static int mt6375_tcpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6375_tcpc *priv;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	priv->tcpci_data.regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->tcpci_data.regmap)
		return dev_err_probe(dev, -ENODEV, "no regmap from the MT6375 core\n");

	ret = mt6375_tcpc_check_ids(priv);
	if (ret)
		return ret;

	irq = platform_get_irq_byname_optional(pdev, "pd_evt");
	if (irq < 0)
		irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->tcpci_data.auto_discharge_disconnect = 1;
	priv->tcpci_data.init = mt6375_tcpc_init;
	priv->tcpci_data.set_vconn = mt6375_tcpc_set_vconn;

	priv->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(priv->vbus)) {
		if (PTR_ERR(priv->vbus) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		priv->tcpci_data.set_vbus = mt6375_tcpc_set_vbus;
	}

	priv->tcpci = tcpci_register_port(dev, &priv->tcpci_data);
	if (IS_ERR(priv->tcpci))
		return dev_err_probe(dev, PTR_ERR(priv->tcpci), "cannot register the TCPCI port\n");

	ret = devm_add_action_or_reset(dev, mt6375_tcpc_unregister, priv->tcpci);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, irq, NULL, mt6375_tcpc_irq, IRQF_ONESHOT,
					dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request the interrupt\n");

	device_init_wakeup(dev, true);
	dev_pm_set_wake_irq(dev, irq);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void mt6375_tcpc_remove(struct platform_device *pdev)
#else
static int mt6375_tcpc_remove(struct platform_device *pdev)
#endif
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id mt6375_tcpc_of_match[] = {
	{ .compatible = "mediatek,mt6375-tcpc" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6375_tcpc_of_match);

static struct platform_driver mt6375_tcpc_driver = {
	.driver = {
		.name = "mt6375-tcpc",
		.of_match_table = mt6375_tcpc_of_match,
	},
	.probe = mt6375_tcpc_probe,
	.remove = mt6375_tcpc_remove,
};
module_platform_driver(mt6375_tcpc_driver);

MODULE_DESCRIPTION("MediaTek MT6375 USB Type-C port controller (CMF Phone 1)");
MODULE_LICENSE("GPL");
