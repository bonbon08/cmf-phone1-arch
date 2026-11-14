// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6375 SubPMIC core (CMF Phone 1 / Tetris): charger, Type-C port controller,
 * fuel gauge and ADCs behind one I2C bus.
 *
 * Mainline-style port of the vendor drivers/mfd/mt6375.c, built like mainline's mt6370.c:
 * one regmap with 16-bit addresses whose high byte selects the I2C client (bank), and
 * regmap-irq for the 16 event registers. The Type-C port controller has a second register
 * bank, 0xF2xx, which it addresses with both bytes on its own client.
 *
 *   bank 0 0x4E TCPC   bank 1 0x34 PMU (this client)   bank 2 0x1A BM (gauge)
 *   bank 3 0x4A HK1    bank 4 0x64 HK2                 bank 5 0x3F TM
 *   0xF2xx: TCPC "RT2" registers
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/version.h>

#define MT6375_REG_DEV_INFO	0x100
#define MT6375_REG_CHG_IRQ0	0x1D0	/* 16 event registers, write 1 to clear */
#define MT6375_REG_CHG_MSK0	0x1F0	/* 16 mask registers, 1 masks */
#define MT6375_NUM_IRQ_REGS	16
#define MT6375_REG_HK2_END	0x4FF
#define MT6375_BANK_RT2		0xF2
#define MT6375_REG_RT2_START	0xF200
#define MT6375_REG_RT2_END	0xF2FF

#define MT6375_VENID_MASK	GENMASK(7, 4)
#define MT6375_REV_MASK		GENMASK(3, 0)
#define MT6375_VENID		0x7

#define MT6375_ADDR_LEN		2

enum {
	MT6375_BANK_TCPC,
	MT6375_BANK_PMU,
	MT6375_BANK_BM,
	MT6375_BANK_HK1,
	MT6375_BANK_HK2,
	MT6375_BANK_TM,
	MT6375_NUM_BANKS,
};

static const u8 mt6375_bank_addr[MT6375_NUM_BANKS] = {
	[MT6375_BANK_TCPC]	= 0x4E,
	[MT6375_BANK_PMU]	= 0x34,
	[MT6375_BANK_BM]	= 0x1A,
	[MT6375_BANK_HK1]	= 0x4A,
	[MT6375_BANK_HK2]	= 0x64,
	[MT6375_BANK_TM]	= 0x3F,
};

struct mt6375_data {
	struct device *dev;
	struct i2c_client *i2c[MT6375_NUM_BANKS];
	struct regmap_irq_chip_data *irq_data;
};

static struct i2c_client *mt6375_bank_client(struct mt6375_data *data, u8 bank)
{
	if (bank == MT6375_BANK_RT2)
		return data->i2c[MT6375_BANK_TCPC];
	return bank < MT6375_NUM_BANKS ? data->i2c[bank] : NULL;
}

static int mt6375_regmap_read(void *context, const void *reg_buf, size_t reg_size,
			      void *val_buf, size_t val_size)
{
	struct mt6375_data *data = context;
	const u8 *reg = reg_buf;
	struct i2c_client *i2c = mt6375_bank_client(data, reg[0]);
	int ret;

	if (!i2c)
		return -EINVAL;

	if (reg[0] == MT6375_BANK_RT2) {
		/* the RT2 bank takes both address bytes */
		struct i2c_msg msgs[2] = {
			{ .addr = i2c->addr, .len = MT6375_ADDR_LEN, .buf = (u8 *)reg },
			{ .addr = i2c->addr, .flags = I2C_M_RD, .len = val_size, .buf = val_buf },
		};

		ret = i2c_transfer(i2c->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret < 0)
			return ret;
		return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
	}

	ret = i2c_smbus_read_i2c_block_data(i2c, reg[1], val_size, val_buf);
	if (ret < 0)
		return ret;
	return ret == val_size ? 0 : -EIO;
}

static int mt6375_regmap_write(void *context, const void *buf, size_t count)
{
	struct mt6375_data *data = context;
	const u8 *b = buf;
	struct i2c_client *i2c = mt6375_bank_client(data, b[0]);
	int ret;

	if (!i2c)
		return -EINVAL;

	if (b[0] == MT6375_BANK_RT2) {
		ret = i2c_master_send(i2c, buf, count);
		if (ret < 0)
			return ret;
		return ret == count ? 0 : -EIO;
	}

	return i2c_smbus_write_i2c_block_data(i2c, b[1], count - MT6375_ADDR_LEN,
					      b + MT6375_ADDR_LEN);
}

static const struct regmap_bus mt6375_regmap_bus = {
	.read	= mt6375_regmap_read,
	.write	= mt6375_regmap_write,
};

static bool mt6375_accessible_reg(struct device *dev, unsigned int reg)
{
	return reg <= MT6375_REG_HK2_END ||
	       (reg >= MT6375_REG_RT2_START && reg <= MT6375_REG_RT2_END);
}

static const struct regmap_config mt6375_regmap_config = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.reg_format_endian	= REGMAP_ENDIAN_BIG,
	.max_register		= MT6375_REG_RT2_END,
	.readable_reg		= mt6375_accessible_reg,
	.writeable_reg		= mt6375_accessible_reg,
};

/*
 * All 128 event bits are interrupt lines; the numbers are those of the vendor binding
 * (dt-bindings/mfd/mt6375.h), e.g. 0 fl_pwr_rdy, 44 adc_donei, 104 gm30_evt, 120 pd_evt.
 */
#define MT6375_IRQ(n)	REGMAP_IRQ_REG_LINE(n, 8)
#define MT6375_IRQ8(n)	MT6375_IRQ(n), MT6375_IRQ(n + 1), MT6375_IRQ(n + 2), MT6375_IRQ(n + 3), \
			MT6375_IRQ(n + 4), MT6375_IRQ(n + 5), MT6375_IRQ(n + 6), MT6375_IRQ(n + 7)

static const struct regmap_irq mt6375_irqs[] = {
	MT6375_IRQ8(0), MT6375_IRQ8(8), MT6375_IRQ8(16), MT6375_IRQ8(24),
	MT6375_IRQ8(32), MT6375_IRQ8(40), MT6375_IRQ8(48), MT6375_IRQ8(56),
	MT6375_IRQ8(64), MT6375_IRQ8(72), MT6375_IRQ8(80), MT6375_IRQ8(88),
	MT6375_IRQ8(96), MT6375_IRQ8(104), MT6375_IRQ8(112), MT6375_IRQ8(120),
};

static const struct regmap_irq_chip mt6375_irq_chip = {
	.name		= "mt6375-irqs",
	.status_base	= MT6375_REG_CHG_IRQ0,
	.ack_base	= MT6375_REG_CHG_IRQ0,
	.mask_base	= MT6375_REG_CHG_MSK0,
	.num_regs	= MT6375_NUM_IRQ_REGS,
	.irqs		= mt6375_irqs,
	.num_irqs	= ARRAY_SIZE(mt6375_irqs),
};

/* The vendor device tree has these children; drivers exist for the TCPC so far */
static const struct mfd_cell mt6375_devices[] = {
	MFD_CELL_OF("mt6375-tcpc", NULL, NULL, 0, 0, "mediatek,mt6375-tcpc"),
	MFD_CELL_OF("mt6375-charger", NULL, NULL, 0, 0, "mediatek,mt6375-chg"),
	MFD_CELL_OF("mt6375-adc", NULL, NULL, 0, 0, "mediatek,mt6375-adc"),
	MFD_CELL_OF("mt6375-gauge", NULL, NULL, 0, 0, "mediatek,mt6375-gauge"),
};

static int mt6375_check_vendor_info(struct mt6375_data *data, struct regmap *regmap)
{
	unsigned int info;
	int ret;

	ret = regmap_read(regmap, MT6375_REG_DEV_INFO, &info);
	if (ret)
		return dev_err_probe(data->dev, ret, "no answer from the MT6375\n");
	if (FIELD_GET(MT6375_VENID_MASK, info) != MT6375_VENID)
		return dev_err_probe(data->dev, -ENODEV, "unknown vendor id 0x%02x\n", info);
	dev_info(data->dev, "MT6375 revision %lu\n", FIELD_GET(MT6375_REV_MASK, info));
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int mt6375_probe(struct i2c_client *i2c)
#else
static int mt6375_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
#endif
{
	struct device *dev = &i2c->dev;
	struct mt6375_data *data;
	struct regmap *regmap;
	int i, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;

	for (i = 0; i < MT6375_NUM_BANKS; i++) {
		if (i == MT6375_BANK_PMU) {
			data->i2c[i] = i2c;
			continue;
		}
		data->i2c[i] = devm_i2c_new_dummy_device(dev, i2c->adapter, mt6375_bank_addr[i]);
		if (IS_ERR(data->i2c[i]))
			return dev_err_probe(dev, PTR_ERR(data->i2c[i]),
					     "cannot add I2C client 0x%02x\n", mt6375_bank_addr[i]);
	}

	regmap = devm_regmap_init(dev, &mt6375_regmap_bus, data, &mt6375_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "cannot init the regmap\n");

	ret = mt6375_check_vendor_info(data, regmap);
	if (ret)
		return ret;

	ret = devm_regmap_add_irq_chip(dev, regmap, i2c->irq, IRQF_ONESHOT, -1,
				       &mt6375_irq_chip, &data->irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "cannot add the IRQ chip\n");

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, mt6375_devices,
				    ARRAY_SIZE(mt6375_devices), NULL, 0,
				    regmap_irq_get_domain(data->irq_data));
}

static const struct of_device_id mt6375_of_match[] = {
	{ .compatible = "mediatek,mt6375" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6375_of_match);

static struct i2c_driver mt6375_driver = {
	.driver = {
		.name = "mt6375",
		.of_match_table = mt6375_of_match,
	},
	.probe = mt6375_probe,
};
module_i2c_driver(mt6375_driver);

MODULE_DESCRIPTION("MediaTek MT6375 SubPMIC core (CMF Phone 1)");
MODULE_LICENSE("GPL");
