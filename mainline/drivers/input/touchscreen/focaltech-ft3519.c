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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#define FT_NAME			"focaltech-ft3519"
#define FT_MAX_POINTS		10
#define FT_REG_TOUCH		0x01
#define FT_REG_CHIP_ID		0xA3
#define FT_REG_FOD_EN		0xCF	/* fingerprint mode: 0x03 on, 0 off (vendor focaltech_common.h) */
#define FT_REG_FOD_VALUE	0x03
#define FT_FOD_DISABLE		0
#define FT_FOD_ENABLE		1
#define FT_FOD_UNLOCK		2
#define FT_REG_FOD_INFO		0xE1	/* finger on the sensor spot, vendor fts_read_fod_info() */
#define FT_FOD_INFO_LEN		9
#define FT_FOD_INFO_ID		0x26
#define FT_KEY_GESTURE_FOD	0xF9	/* the key code the vendor driver reports */

/*
 * The Goodix fingerprint driver learns about a finger on the spot through the vendor
 * notifier module touchpanel_event_notify (FINGER_DOWN/UP to its HAL). It is taken at
 * run time, so the driver has no hard dependency on it and builds without it.
 */
enum { FT_TP_FPEVENT_UP, FT_TP_FPEVENT_DOWN };
struct ft_tp_coordinate {
	int x;
	int y;
};
int touchpanel_event_call_notifier(unsigned long val, void *data);
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
	struct mutex lock;	/* serialises I2C access and the FOD state (IRQ thread, procfs) */
	int fod_state;		/* what /proc/touchpanel/fod_mode last set */
	struct proc_dir_entry *proc_dir;
	bool fp_down;		/* the chip's FOD info: finger on the spot */
	bool fp_down_report;	/* what was last reported */
	u16 fp_x, fp_y;
	int (*tp_notify)(unsigned long val, void *data);
};

/* The chip answers a read after writing the register address */
static int ft_read(struct ft3519 *ft, u8 reg, u8 *buf, u16 len)
{
	struct i2c_msg msgs[2] = {
		{ .addr = ft->client->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = ft->client->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	int ret;

	ret = i2c_transfer(ft->client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret == ARRAY_SIZE(msgs) ? 0 : (ret < 0 ? ret : -EIO);
}

static int ft_read_report(struct ft3519 *ft, u8 *buf)
{
	return ft_read(ft, FT_REG_TOUCH, buf, FT_TOUCH_LEN);
}

/* Like the vendor driver: with the mode on, read the FOD info after every report */
static void ft_read_fod(struct ft3519 *ft)
{
	u8 en, val[FT_FOD_INFO_LEN];

	if (ft->fod_state == FT_FOD_DISABLE)
		return;
	if (ft_read(ft, FT_REG_FOD_EN, &en, 1) || en != FT_REG_FOD_VALUE)
		return;
	if (ft_read(ft, FT_REG_FOD_INFO, val, sizeof(val)) || val[1] != FT_FOD_INFO_ID)
		return;
	if (val[8] == 0)
		ft->fp_down = true;
	else if (val[8] == 1)
		ft->fp_down = false;
	ft->fp_x = (val[4] << 8) | val[5];
	ft->fp_y = (val[6] << 8) | val[7];
}

/* Reports a change of the finger on the spot: key event and the fingerprint notifier */
static void ft_report_fod(struct ft3519 *ft)
{
	struct ft_tp_coordinate c = { .x = ft->fp_x, .y = ft->fp_y };

	if (ft->fp_down == ft->fp_down_report)
		return;
	ft->fp_down_report = ft->fp_down;
	input_report_key(ft->input, FT_KEY_GESTURE_FOD, ft->fp_down);
	input_sync(ft->input);
	if (!ft->tp_notify)
		ft->tp_notify = symbol_get(touchpanel_event_call_notifier);
	if (ft->tp_notify)
		ft->tp_notify(ft->fp_down ? FT_TP_FPEVENT_DOWN : FT_TP_FPEVENT_UP, &c);
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
	ft_read_fod(ft);
	ft_report_fod(ft);
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

static int ft_write_reg(struct ft3519 *ft, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = ft->client->addr, .flags = 0, .len = 2, .buf = buf };

	return i2c_transfer(ft->client->adapter, &msg, 1) == 1 ? 0 : -EIO;
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

static void ft_put_notify(void *data)
{
	struct ft3519 *ft = data;

	if (ft->tp_notify)
		symbol_put(touchpanel_event_call_notifier);
}

static void ft_remove_proc(void *data)
{
	struct ft3519 *ft = data;

	if (ft->proc_dir)
		remove_proc_subtree("touchpanel", NULL);
}

static void ft_regulator_off(void *data)
{
	regulator_disable(data);
}

/*
 * /proc/touchpanel/fod_mode, the interface tetris-fp uses (the same as the vendor driver):
 * 1 turns the fingerprint mode on (register 0xCF), 0 turns it off, 2 only unlocks.
 */
static int ft_fod_show(struct seq_file *s, void *unused)
{
	struct ft3519 *ft = s->private;

	seq_printf(s, "Fod Mode:%d\n", ft->fod_state);
	return 0;
}

static int ft_fod_open(struct inode *inode, struct file *file)
{
	return single_open(file, ft_fod_show, pde_data(inode));
}

static ssize_t ft_fod_write(struct file *file, const char __user *ubuf, size_t count,
			    loff_t *ppos)
{
	struct ft3519 *ft = pde_data(file_inode(file));
	char buf[20] = "";
	u32 mode;
	int ret = 0;

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;
	if (kstrtouint(buf, 0, &mode))
		return -EINVAL;

	mutex_lock(&ft->lock);
	switch (mode) {
	case FT_FOD_ENABLE:
		ret = ft_write_reg(ft, FT_REG_FOD_EN, FT_REG_FOD_VALUE);
		break;
	case FT_FOD_DISABLE:
		ret = ft_write_reg(ft, FT_REG_FOD_EN, 0);
		ft->fp_down = false;
		break;
	case FT_FOD_UNLOCK:
		break;
	default:
		ret = -EINVAL;
	}
	if (!ret)
		ft->fod_state = mode;
	if (mode == FT_FOD_DISABLE)
		ft_report_fod(ft);	/* a finger still reported as down goes up */
	mutex_unlock(&ft->lock);

	return ret ? ret : count;
}

static const struct proc_ops ft_fod_fops = {
	.proc_open = ft_fod_open,
	.proc_write = ft_fod_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int ft_init_proc(struct ft3519 *ft)
{
	ft->proc_dir = proc_mkdir("touchpanel", NULL);
	if (!ft->proc_dir)
		return -ENOMEM;
	if (!proc_create_data("fod_mode", 0664, ft->proc_dir, &ft_fod_fops, ft)) {
		remove_proc_subtree("touchpanel", NULL);
		return -ENOMEM;
	}
	return 0;
}

static int ft_init_input(struct ft3519 *ft)
{
	struct input_dev *input;
	int ret;

	input = devm_input_allocate_device(ft->dev);
	if (!input)
		return -ENOMEM;

	/* the name tetris-fp looks for (open_touchscreen), same as the vendor driver */
	input->name = "fts_ts";
	input->id.bustype = BUS_I2C;
	input->phys = "i2c/focaltech-ft3519";
	ret = input_mt_init_slots(input, FT_MAX_POINTS,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED | INPUT_MT_TRACK);
	if (ret)
		return ret;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ft->max_x - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ft->max_y - 1, 0, 0);
	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input_set_capability(input, EV_KEY, FT_KEY_GESTURE_FOD);
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

	ret = ft_init_proc(ft);
	if (ret)
		return dev_err_probe(dev, ret, "cannot create /proc/touchpanel\n");
	devm_add_action_or_reset(dev, ft_remove_proc, ft);
	devm_add_action_or_reset(dev, ft_put_notify, ft);

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
