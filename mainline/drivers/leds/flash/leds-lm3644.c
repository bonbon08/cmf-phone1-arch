// SPDX-License-Identifier: GPL-2.0-only
/*
 * LM3644 dual flash LED driver (CMF Phone 1 / Tetris), as an LED flash class device.
 *
 * Mainline-style port of the vendor V4L2 driver
 * drivers/misc/mediatek/flashlight/v4l2/lm3644.c. Both LED outputs drive the same
 * flash module, so they are one LED: brightness is the torch, the flash class
 * attributes (flash_brightness, flash_timeout, flash_strobe, flash_fault) the flash.
 *
 * The current steps are the ones the vendor driver programs on this phone: 8-bit codes,
 * flash 2.94 mA + 5.87 mA per step (up to 1.5 A), torch 0.75 mA + 1.51 mA per step
 * (up to 386 mA), for each output.
 *
 * Like the other ports it claims the chip at boot: the initramfs loads it right before
 * the vendor lm3644.ko, so the vendor driver finds the device taken.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/version.h>

#define LM3644_REG_ENABLE		0x01
#define LM3644_REG_LED0_FLASH		0x03
#define LM3644_REG_LED1_FLASH		0x04
#define LM3644_REG_LED0_TORCH		0x05
#define LM3644_REG_LED1_TORCH		0x06
#define LM3644_REG_TIMING		0x08
#define LM3644_REG_FLAGS1		0x0A
#define LM3644_REG_FLAGS2		0x0B
#define LM3644_REG_DEVICE_ID		0x0C

#define LM3644_ENABLE_LED0		BIT(0)
#define LM3644_ENABLE_LED1		BIT(1)
#define LM3644_MODE_MASK		GENMASK(3, 2)
#define LM3644_MODE_OFF			0x00
#define LM3644_MODE_TORCH		0x08
#define LM3644_MODE_FLASH		0x0C
#define LM3644_STROBE_MASK		0x2C	/* mode plus the strobe pin enable, as the vendor clears it */
#define LM3644_TIMEOUT_MASK		GENMASK(3, 0)

#define LM3644_FLAG_TIMEOUT		BIT(0)
#define LM3644_FLAG_THERMAL		BIT(2)
#define LM3644_FLAG_LED1_SHORT		BIT(4)
#define LM3644_FLAG_LED0_SHORT		BIT(5)

#define LM3644_FLASH_MIN_UA		2940
#define LM3644_FLASH_STEP_UA		5870
#define LM3644_FLASH_MAX_UA		1500000
#define LM3644_TORCH_MIN_UA		750
#define LM3644_TORCH_STEP_UA		1510
#define LM3644_TORCH_MAX_UA		386000
#define LM3644_TORCH_DEFAULT_UA		200000	/* the torch stays on: keep it cooler than the maximum */
#define LM3644_TIMEOUT_MIN_US		200000
#define LM3644_TIMEOUT_STEP_US		200000
#define LM3644_TIMEOUT_MAX_US		1600000
#define LM3644_TIMEOUT_DEFAULT_US	400000

struct lm3644 {
	struct device *dev;
	struct regmap *regmap;
	struct led_classdev_flash fled;
	struct mutex lock;	/* serialises the mode changes of torch and flash */
	u8 outputs;		/* LM3644_ENABLE_LED0/1 of the outputs in use */
	struct gpio_descs *enable;
	struct pinctrl *pinctrl;
	struct pinctrl_state *hwen_on[2], *hwen_off[2];
};

static struct lm3644 *to_lm3644(struct led_classdev_flash *fled)
{
	return container_of(fled, struct lm3644, fled);
}

/*
 * The enable pins: enable-gpios in a mainline device tree; the vendor device tree only
 * has pinctrl states for them (hwen-high/-low and hwen-high1/-low1, GPIO 155 and 151).
 */
static void lm3644_hwen(struct lm3644 *chip, bool on)
{
	unsigned long bits = on ? ~0UL : 0;
	int i;

	if (chip->enable) {
		gpiod_set_array_value_cansleep(chip->enable->ndescs, chip->enable->desc,
					       chip->enable->info, &bits);
		return;
	}
	for (i = 0; i < 2; i++) {
		struct pinctrl_state *st = on ? chip->hwen_on[i] : chip->hwen_off[i];

		if (st)
			pinctrl_select_state(chip->pinctrl, st);
	}
}

static int lm3644_get_hwen(struct lm3644 *chip)
{
	static const char * const on[] = { "hwen-high", "hwen-high1" };
	static const char * const off[] = { "hwen-low", "hwen-low1" };
	struct pinctrl_state *st;
	int i;

	chip->enable = devm_gpiod_get_array_optional(chip->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->enable))
		return dev_err_probe(chip->dev, PTR_ERR(chip->enable), "enable GPIOs\n");
	if (chip->enable)
		return 0;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR(chip->pinctrl)) {
		chip->pinctrl = NULL;
		return 0;	/* enable pins strapped high */
	}
	for (i = 0; i < 2; i++) {
		st = pinctrl_lookup_state(chip->pinctrl, on[i]);
		chip->hwen_on[i] = IS_ERR(st) ? NULL : st;
		st = pinctrl_lookup_state(chip->pinctrl, off[i]);
		chip->hwen_off[i] = IS_ERR(st) ? NULL : st;
	}
	lm3644_hwen(chip, true);
	return 0;
}

/* Writes the same code to both outputs (bit 7 of the LED1 registers is not a current bit) */
static int lm3644_write_both(struct lm3644 *chip, u8 reg0, u8 reg1, u8 code)
{
	int ret = 0;

	if (chip->outputs & LM3644_ENABLE_LED0)
		ret = regmap_write(chip->regmap, reg0, code);
	if (!ret && (chip->outputs & LM3644_ENABLE_LED1))
		ret = regmap_update_bits(chip->regmap, reg1, 0x7f, code & 0x7f);
	return ret;
}

static int lm3644_set_mode(struct lm3644 *chip, u8 mode)
{
	return regmap_update_bits(chip->regmap, LM3644_REG_ENABLE,
				  LM3644_STROBE_MASK | LM3644_ENABLE_LED0 | LM3644_ENABLE_LED1,
				  mode == LM3644_MODE_OFF ? 0 : mode | chip->outputs);
}

/* Torch: the LED class brightness, 1..max_brightness are torch codes 0.. */
static int lm3644_brightness_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct led_classdev_flash *fled = lcdev_to_flcdev(cdev);
	struct lm3644 *chip = to_lm3644(fled);
	int ret;

	mutex_lock(&chip->lock);
	if (brightness == LED_OFF) {
		ret = lm3644_set_mode(chip, LM3644_MODE_OFF);
	} else {
		ret = lm3644_write_both(chip, LM3644_REG_LED0_TORCH, LM3644_REG_LED1_TORCH,
					brightness - 1);
		if (!ret)
			ret = lm3644_set_mode(chip, LM3644_MODE_TORCH);
	}
	mutex_unlock(&chip->lock);
	return ret;
}

static int lm3644_flash_brightness_set(struct led_classdev_flash *fled, u32 brightness)
{
	struct lm3644 *chip = to_lm3644(fled);
	u8 code = (brightness - LM3644_FLASH_MIN_UA) / LM3644_FLASH_STEP_UA;
	int ret;

	mutex_lock(&chip->lock);
	ret = lm3644_write_both(chip, LM3644_REG_LED0_FLASH, LM3644_REG_LED1_FLASH, code);
	mutex_unlock(&chip->lock);
	return ret;
}

/* The vendor timing codes: 0x04 for 200 ms, else 7 + t / 200 ms (400 ms -> 0x09) */
static int lm3644_timeout_set(struct led_classdev_flash *fled, u32 timeout)
{
	struct lm3644 *chip = to_lm3644(fled);
	u8 code = timeout <= LM3644_TIMEOUT_MIN_US ? 0x04 : 7 + timeout / LM3644_TIMEOUT_STEP_US;
	int ret;

	mutex_lock(&chip->lock);
	ret = regmap_update_bits(chip->regmap, LM3644_REG_TIMING, LM3644_TIMEOUT_MASK, code);
	mutex_unlock(&chip->lock);
	return ret;
}

static int lm3644_strobe_set(struct led_classdev_flash *fled, bool state)
{
	struct lm3644 *chip = to_lm3644(fled);
	unsigned int flags;
	int ret;

	mutex_lock(&chip->lock);
	regmap_read(chip->regmap, LM3644_REG_FLAGS1, &flags);	/* reading clears the flags */
	ret = lm3644_set_mode(chip, state ? LM3644_MODE_FLASH : LM3644_MODE_OFF);
	mutex_unlock(&chip->lock);
	return ret;
}

/* The chip clears the mode bits itself when the flash timeout ends */
static int lm3644_strobe_get(struct led_classdev_flash *fled, bool *state)
{
	struct lm3644 *chip = to_lm3644(fled);
	unsigned int val;
	int ret;

	mutex_lock(&chip->lock);
	ret = regmap_read(chip->regmap, LM3644_REG_ENABLE, &val);
	mutex_unlock(&chip->lock);
	if (ret)
		return ret;
	*state = (val & LM3644_MODE_MASK) == LM3644_MODE_FLASH;
	return 0;
}

static int lm3644_fault_get(struct led_classdev_flash *fled, u32 *fault)
{
	struct lm3644 *chip = to_lm3644(fled);
	unsigned int val;
	int ret;

	mutex_lock(&chip->lock);
	ret = regmap_read(chip->regmap, LM3644_REG_FLAGS1, &val);
	mutex_unlock(&chip->lock);
	if (ret)
		return ret;
	*fault = 0;
	if (val & (LM3644_FLAG_LED0_SHORT | LM3644_FLAG_LED1_SHORT))
		*fault |= LED_FAULT_SHORT_CIRCUIT;
	if (val & LM3644_FLAG_THERMAL)
		*fault |= LED_FAULT_OVER_TEMPERATURE;
	if (val & LM3644_FLAG_TIMEOUT)
		*fault |= LED_FAULT_TIMEOUT;
	return 0;
}

static const struct led_flash_ops lm3644_flash_ops = {
	.flash_brightness_set	= lm3644_flash_brightness_set,
	.strobe_set		= lm3644_strobe_set,
	.strobe_get		= lm3644_strobe_get,
	.timeout_set		= lm3644_timeout_set,
	.fault_get		= lm3644_fault_get,
};

static const struct regmap_config lm3644_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LM3644_REG_DEVICE_ID,
};

static void lm3644_off(void *data)
{
	struct lm3644 *chip = data;

	lm3644_set_mode(chip, LM3644_MODE_OFF);
	lm3644_hwen(chip, false);
}

/*
 * The outputs in use are the child nodes (reg 0 and 1); the first one carries the
 * LED properties. Without child nodes both outputs are used.
 */
static struct fwnode_handle *lm3644_parse_outputs(struct lm3644 *chip)
{
	struct fwnode_handle *child, *first = NULL;
	u32 reg;

	device_for_each_child_node(chip->dev, child) {
		if (fwnode_property_read_u32(child, "reg", &reg) || reg > 1)
			continue;
		chip->outputs |= reg ? LM3644_ENABLE_LED1 : LM3644_ENABLE_LED0;
		if (!first)
			first = fwnode_handle_get(child);
	}
	if (!chip->outputs)
		chip->outputs = LM3644_ENABLE_LED0 | LM3644_ENABLE_LED1;
	return first;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int lm3644_probe(struct i2c_client *client)
#else
static int lm3644_probe(struct i2c_client *client, const struct i2c_device_id *i2c_id)
#endif
{
	struct device *dev = &client->dev;
	struct led_init_data init_data = {};
	struct fwnode_handle *fwnode;
	struct led_classdev *cdev;
	struct led_flash_setting *s;
	struct lm3644 *chip;
	u32 torch_ua = LM3644_TORCH_DEFAULT_UA, flash_ua = LM3644_FLASH_MAX_UA;
	u32 timeout_us = LM3644_TIMEOUT_DEFAULT_US;
	unsigned int id, flags;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = dev;
	mutex_init(&chip->lock);

	chip->regmap = devm_regmap_init_i2c(client, &lm3644_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap), "regmap\n");

	ret = lm3644_get_hwen(chip);
	if (ret)
		return ret;

	ret = regmap_read(chip->regmap, LM3644_REG_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "no answer from the flash LED driver\n");
	dev_info(dev, "device id 0x%02x\n", id);

	fwnode = lm3644_parse_outputs(chip);
	if (fwnode) {
		fwnode_property_read_u32(fwnode, "led-max-microamp", &torch_ua);
		fwnode_property_read_u32(fwnode, "flash-max-microamp", &flash_ua);
		fwnode_property_read_u32(fwnode, "flash-max-timeout-us", &timeout_us);
	}
	torch_ua = clamp(torch_ua, LM3644_TORCH_MIN_UA, LM3644_TORCH_MAX_UA);
	flash_ua = clamp(flash_ua, LM3644_FLASH_MIN_UA, LM3644_FLASH_MAX_UA);
	timeout_us = clamp(timeout_us, LM3644_TIMEOUT_MIN_US, LM3644_TIMEOUT_MAX_US);

	/* everything off, faults cleared, like the vendor lm3644_init() */
	ret = lm3644_set_mode(chip, LM3644_MODE_OFF);
	if (!ret)
		ret = regmap_read(chip->regmap, LM3644_REG_FLAGS1, &flags);
	if (ret) {
		fwnode_handle_put(fwnode);
		return dev_err_probe(dev, ret, "cannot reset the outputs\n");
	}

	chip->fled.ops = &lm3644_flash_ops;
	s = &chip->fled.brightness;
	s->min = LM3644_FLASH_MIN_UA;
	s->step = LM3644_FLASH_STEP_UA;
	s->max = LM3644_FLASH_MIN_UA + (flash_ua - LM3644_FLASH_MIN_UA) / s->step * s->step;
	s->val = s->max;
	s = &chip->fled.timeout;
	s->min = LM3644_TIMEOUT_MIN_US;
	s->step = LM3644_TIMEOUT_STEP_US;
	s->max = timeout_us - timeout_us % s->step;
	s->val = min_t(u32, s->max, LM3644_TIMEOUT_DEFAULT_US);

	cdev = &chip->fled.led_cdev;
	/* brightness n is torch code n - 1; LED_FULL caps it one code under 386 mA */
	cdev->max_brightness = min_t(u32, LED_FULL,
				     (torch_ua - LM3644_TORCH_MIN_UA) / LM3644_TORCH_STEP_UA + 1);
	cdev->brightness_set_blocking = lm3644_brightness_set;
	cdev->flags |= LED_DEV_CAP_FLASH;

	ret = devm_add_action_or_reset(dev, lm3644_off, chip);
	if (ret) {
		fwnode_handle_put(fwnode);
		return ret;
	}

	/* the vendor device tree has neither label nor function: the name Phosh looks for */
	if (fwnode && (fwnode_property_present(fwnode, "label") ||
		       fwnode_property_present(fwnode, "function") ||
		       fwnode_property_present(fwnode, "color"))) {
		init_data.fwnode = fwnode;
		ret = devm_led_classdev_flash_register_ext(dev, &chip->fled, &init_data);
	} else {
		cdev->name = "white:torch";
		ret = devm_led_classdev_flash_register(dev, &chip->fled);
	}
	fwnode_handle_put(fwnode);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register the LED\n");

	/* the first flash setting goes to the chip; the core only calls the ops on changes */
	lm3644_flash_brightness_set(&chip->fled, chip->fled.brightness.val);
	lm3644_timeout_set(&chip->fled, chip->fled.timeout.val);

	dev_info(dev, "%s: torch up to %u uA, flash up to %u uA, outputs 0x%x\n",
		 cdev->dev ? dev_name(cdev->dev) : cdev->name, torch_ua, flash_ua, chip->outputs);
	return 0;
}

static const struct of_device_id lm3644_of_match[] = {
	{ .compatible = "ti,lm3644" },
	{ .compatible = "mediatek,lm3644" },	/* the vendor device tree of the phone */
	{ }
};
MODULE_DEVICE_TABLE(of, lm3644_of_match);

static const struct i2c_device_id lm3644_id[] = {
	{ "lm3644" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3644_id);

static struct i2c_driver lm3644_driver = {
	.driver = {
		.name = "leds-lm3644",
		.of_match_table = lm3644_of_match,
	},
	.probe = lm3644_probe,
	.id_table = lm3644_id,
};
module_i2c_driver(lm3644_driver);

MODULE_DESCRIPTION("LM3644 dual flash LED driver (CMF Phone 1)");
MODULE_LICENSE("GPL");
