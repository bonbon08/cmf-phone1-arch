// SPDX-License-Identifier: GPL-2.0-only
/*
 * FocalTech FT3519 touch controller (CMF Phone 1 / Tetris), basic multitouch.
 *
 * Mainline-style port of the basic read path of the vendor driver
 * drivers/input/touchscreen/FT3519 (focaltech_core.c, focaltech_i2c.c): the
 * touch report at register 0x01 (two header bytes, then six bytes per point),
 * reported as Linux multitouch slots. Left out on purpose: gestures, pen, proximity,
 * firmware flashing and the factory tests of the vendor driver.
 *
 * Like the RT6010 port, it claims the chip at boot: the initramfs loads it right
 * before the vendor focaltech_tp.ko, so the vendor driver finds the device taken.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#define FT_NAME			"focaltech-ft3519"
#define FT_MAX_POINTS		10
#define FT_REG_TOUCH		0x01
#define FT_REG_CHIP_ID		0xA3
#define FT_TOUCH_LEN		(2 + 6 * FT_MAX_POINTS)
#define FT_EVENT_DOWN		0
#define FT_EVENT_UP		1
#define FT_EVENT_CONTACT	2

struct ft_point {
	u8 id;
	u8 event;
	u16 x;
	u16 y;
};

struct ft3519 {
	struct i2c_client *client;
	struct device *dev;
	struct input_dev *input;
	struct regulator *vdd;
	struct gpio_desc *reset;
	struct gpio_desc *dvdd;
	u32 max_x;
	u32 max_y;
	struct mutex lock;	/* serialises I2C reads from the IRQ thread and probe */
};

/* Reads the touch report; the chip answers a read after register 0x01 */
static int ft_read_report(struct ft3519 *ft, u8 *buf)
{
	u8 reg = FT_REG_TOUCH;
	struct i2c_msg msgs[2] = {
		{ .addr = ft->client->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = ft->client->addr, .flags = I2C_M_RD, .len = FT_TOUCH_LEN, .buf = buf },
	};
	int ret;

	ret = i2c_transfer(ft->client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret == ARRAY_SIZE(msgs) ? 0 : (ret < 0 ? ret : -EIO);
}

/* Decodes the report exactly like focaltech_core.c (TOUCH_DEFAULT protocol) */
static int ft_parse_report(struct ft3519 *ft, const u8 *buf, struct ft_point *pts)
{
	unsigned int n = buf[1] & 0x0F;
	unsigned int i, count = 0;

	if (n > FT_MAX_POINTS)
		return -EINVAL;

	for (i = 0; i < FT_MAX_POINTS && count < n; i++) {
		const u8 *p = buf + 2 + 6 * i;

		pts[count].event = p[0] >> 6;
		pts[count].x = ((p[0] & 0x0F) << 8) | p[1];
		pts[count].id = p[2] >> 4;
		pts[count].y = ((p[2] & 0x0F) << 8) | p[3];
		if (pts[count].id >= FT_MAX_POINTS)
			continue;
		count++;
	}
	return count;
}

static irqreturn_t ft_irq_thread(int irq, void *data)
{
	struct ft3519 *ft = data;
	struct ft_point pts[FT_MAX_POINTS];
	u8 buf[FT_TOUCH_LEN];
	int i, count, ret;

	mutex_lock(&ft->lock);
	ret = ft_read_report(ft, buf);
	if (ret) {
		mutex_unlock(&ft->lock);
		return IRQ_HANDLED;
	}
	count = ft_parse_report(ft, buf, pts);
	mutex_unlock(&ft->lock);
	if (count < 0)
		return IRQ_HANDLED;

	for (i = 0; i < count; i++) {
		struct ft_point *p = &pts[i];
		bool down = p->event != FT_EVENT_UP;

		input_mt_slot(ft->input, p->id);
		input_mt_report_slot_state(ft->input, MT_TOOL_FINGER, down);
		if (down) {
			input_report_abs(ft->input, ABS_MT_POSITION_X, p->x);
			input_report_abs(ft->input, ABS_MT_POSITION_Y, p->y);
		}
	}
	input_mt_sync_frame(ft->input);
	input_sync(ft->input);
	return IRQ_HANDLED;
}

/*
 * The vendor device tree names the pins "focaltech,reset-gpio" and "focaltech,dvdd-gpio".
 * Kernels before 6.3 can resolve those names directly; newer ones only take the -gpios names.
 * The reset pin is active low in the device tree: a logical 1 asserts reset.
 */
static struct gpio_desc *ft_gpio(struct device *dev, const char *con, const char *legacy,
				 enum gpiod_flags flags)
{
	struct gpio_desc *d;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
	d = devm_gpiod_get_from_of_node(dev, dev->of_node, legacy, 0, flags, con);
	if (!IS_ERR(d) || PTR_ERR(d) != -ENOENT)
		return d;
#else
	(void)legacy;
#endif
	d = devm_gpiod_get_optional(dev, con, flags);
	return d;
}

static void ft_power_on(struct ft3519 *ft)
{
	if (ft->dvdd) {
		gpiod_set_value_cansleep(ft->dvdd, 1);
		usleep_range(1000, 2000);
	}
	gpiod_set_value_cansleep(ft->reset, 1);	/* assert reset */
	usleep_range(10000, 15000);
	gpiod_set_value_cansleep(ft->reset, 0);	/* release */
	msleep(200);	/* the chip needs this long before the first report */
}

static int ft_read_chip_id(struct ft3519 *ft, u8 *id)
{
	u8 reg = FT_REG_CHIP_ID;
	struct i2c_msg msgs[2] = {
		{ .addr = ft->client->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = ft->client->addr, .flags = I2C_M_RD, .len = 1, .buf = id },
	};

	return i2c_transfer(ft->client->adapter, msgs, ARRAY_SIZE(msgs)) == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

static void ft_regulator_off(void *data)
{
	regulator_disable(data);
}

static int ft_init_input(struct ft3519 *ft)
{
	struct input_dev *input;
	int ret;

	input = devm_input_allocate_device(ft->dev);
	if (!input)
		return -ENOMEM;

	input->name = "focaltech-ft3519";
	input->id.bustype = BUS_I2C;
	input->phys = "i2c/focaltech-ft3519";
	ret = input_mt_init_slots(input, FT_MAX_POINTS,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED | INPUT_MT_TRACK);
	if (ret)
		return ret;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ft->max_x - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ft->max_y - 1, 0, 0);
	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input_set_drvdata(input, ft);

	ret = input_register_device(input);
	if (ret)
		return ret;
	ft->input = input;
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int ft_probe(struct i2c_client *client)
#else
static int ft_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct ft3519 *ft;
	u32 coords[4] = { 0, 0, 1080, 2400 };
	u8 chip_id = 0;
	u8 buf[FT_TOUCH_LEN];
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ft = devm_kzalloc(dev, sizeof(*ft), GFP_KERNEL);
	if (!ft)
		return -ENOMEM;
	ft->client = client;
	ft->dev = dev;
	mutex_init(&ft->lock);
	i2c_set_clientdata(client, ft);

	/* EPROBE_DEFER here leaves the device to the vendor driver, which is the fallback */
	ft->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ft->vdd))
		return dev_err_probe(dev, PTR_ERR(ft->vdd), "no vdd regulator\n");
	ret = regulator_enable(ft->vdd);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable vdd\n");
	ret = devm_add_action_or_reset(dev, ft_regulator_off, ft->vdd);
	if (ret)
		return ret;

	ft->reset = ft_gpio(dev, "reset", "focaltech,reset-gpio", GPIOD_OUT_HIGH);
	if (IS_ERR(ft->reset))
		return dev_err_probe(dev, PTR_ERR(ft->reset), "no reset GPIO\n");
	if (!ft->reset)
		return dev_err_probe(dev, -EINVAL, "reset GPIO missing\n");
	ft->dvdd = ft_gpio(dev, "dvdd", "focaltech,dvdd-gpio", GPIOD_OUT_HIGH);
	if (IS_ERR(ft->dvdd))
		return dev_err_probe(dev, PTR_ERR(ft->dvdd), "no dvdd GPIO\n");

	of_property_read_u32_array(dev->of_node, "focaltech,display-coords", coords, 4);
	ft->max_x = coords[2];
	ft->max_y = coords[3];

	ft_power_on(ft);

	ret = ft_read_report(ft, buf);
	if (ret)
		return dev_err_probe(dev, ret, "no answer from the touch controller\n");
	if (ft_read_chip_id(ft, &chip_id) == 0)
		dev_info(dev, "chip id 0x%02x\n", chip_id);

	ret = ft_init_input(ft);
	if (ret)
		return dev_err_probe(dev, ret, "input device failed\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL, ft_irq_thread,
					IRQF_ONESHOT, FT_NAME, ft);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request the interrupt\n");

	dev_info(dev, "touch ready, %ux%u\n", ft->max_x, ft->max_y);
	return 0;
}

static const struct of_device_id ft_of_match[] = {
	{ .compatible = "focaltech,fts" },	/* the vendor device tree of the phone */
	{ .compatible = "focaltech,ft3519" },
	{ }
};
MODULE_DEVICE_TABLE(of, ft_of_match);

static const struct i2c_device_id ft_id[] = {
	{ "focaltech-ft3519" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ft_id);

static struct i2c_driver ft_driver = {
	.driver = {
		.name = FT_NAME,
		.of_match_table = ft_of_match,
	},
	.probe = ft_probe,
	.id_table = ft_id,
};
module_i2c_driver(ft_driver);

MODULE_DESCRIPTION("FocalTech FT3519 basic multitouch driver (CMF Phone 1)");
MODULE_LICENSE("GPL");
