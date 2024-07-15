/*
 * Copyright 2024 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_pf1550_charger

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/linear_range.h>

#include "zephyr/logging/log.h"
LOG_MODULE_REGISTER(pf1550_charger);


#define INT_ENABLE_DELAY K_MSEC(500)

enum {
  /* Charger Register Addresses */
  CHARGER_CHG_INT           = 0x80 + 0x00,
  CHARGER_CHG_INT_MASK      = 0x80 + 0x02,
  CHARGER_CHG_INT_OK        = 0x80 + 0x04,
  CHARGER_VBUS_SNS          = 0x80 + 0x06,
  CHARGER_CHG_SNS           = 0x80 + 0x07,
  CHARGER_BATT_SNS          = 0x80 + 0x08,
  CHARGER_CHG_OPER          = 0x80 + 0x09,
  CHARGER_CHG_TMR           = 0x80 + 0x0A,
  CHARGER_CHG_EOC_CNFG      = 0x80 + 0x0D,
  CHARGER_CHG_CURR_CFG      = 0x80 + 0x0E,
  CHARGER_BATT_REG          = 0x80 + 0x0F,
  CHARGER_BATFET_CNFG       = 0x80 + 0x11,
  CHARGER_THM_REG_CNFG      = 0x80 + 0x12,
  CHARGER_VBUS_INLIM_CNFG   = 0x80 + 0x14,
  CHARGER_VBUS_LIN_DPM      = 0x80 + 0x15,
  CHARGER_USB_PHY_LDO_CNFG  = 0x80 + 0x16,
  CHARGER_DBNC_DELAY_TIME   = 0x80 + 0x18,
  CHARGER_CHG_INT_CNFG      = 0x80 + 0x19,
  CHARGER_THM_ADJ_SETTING   = 0x80 + 0x1A,
  CHARGER_VBUS2SYS_CNFG     = 0x80 + 0x1B,
  CHARGER_LED_PWM           = 0x80 + 0x1C,
  CHARGER_FAULT_BATFET_CNFG = 0x80 + 0x1D,
  CHARGER_LED_CNFG          = 0x80 + 0x1E,
  CHARGER_CHGR_KEY2         = 0x80 + 0x1F,
};

#define PF1550_BAT_IRQ			BIT(2)
#define PF1550_CHG_IRQ			BIT(3)
#define PF1550_VBUS_IRQ			BIT(5)
#define PF1550_VBUS_DPM_IRQ		BIT(5)

enum charger_pf1550_therm_mode {
	PF1550_THERM_MODE_DISABLED,
	PF1550_THERM_MODE_THERMISTOR,
	PF1550_THERM_MODE_JEITA_1,
	PF1550_THERM_MODE_JEITA_2,
	PF1550_THERM_MODE_UNKNOWN,
};

enum pf1550_led_behaviour {
	ON_CHARGING_FLASH_FAULT_OFF_DONE,
	FLASH_CHARGING_ON_FAUTL_OFF_DONE
};

struct charger_pf1550_led_config {
	bool enabled;
	bool manual;
	uint8_t frequency;
	uint8_t duty;
	enum pf1550_led_behaviour behaviour;
};

struct charger_pf1550_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec int_gpio;
	char *therm_mon_mode;
	uint32_t charge_current_ua;
	uint32_t vbus_ilim_ua;
	uint32_t battery_charge_termination_uv;
	uint32_t vsys_min_uv;
};

struct charger_pf1550_data {
	const struct device *dev;
	struct gpio_callback gpio_cb;
	struct k_work int_routine_work;
	struct k_work_delayable int_enable_work;
	enum charger_status charger_status;
	enum charger_online charger_online;
	charger_status_notifier_t charger_status_notifier;
	charger_online_notifier_t charger_online_notifier;
	bool charger_enabled;
	uint32_t charge_current_ua;
	uint32_t vbus_ilim_ua;
	/* TODO: move me to config as soon as dt is ready */
	struct charger_pf1550_led_config* led_config;
};

static const struct linear_range charger_vbus_ilim_range[] = {
	LINEAR_RANGE_INIT(10000, 5000, 0, 8),
	LINEAR_RANGE_INIT(100000, 50000, 9, 10),
	LINEAR_RANGE_INIT(200000, 100000, 11, 19),
	LINEAR_RANGE_INIT(1500000, 0, 20, 20),
};

static const struct linear_range charger_fast_charge_ua_range[] = {
	LINEAR_RANGE_INIT(100000, 50000, 0, 18),
};

static const struct linear_range charger_battery_termination_uv_range[] = {
	LINEAR_RANGE_INIT(3500000, 20000, 8, 55),
};

static const struct linear_range charger_vsysmin_uv[] = {
	LINEAR_RANGE_INIT(3500000, 0, 0, 0),
	LINEAR_RANGE_INIT(3700000, 0, 1, 1),
	LINEAR_RANGE_INIT(4300000, 0, 2, 2),
};

static int pf1550_get_charger_status(const struct device *dev, enum charger_status *status)
{
	enum chg_sns {
		PF1550_CHARGER_PRECHARGE,
		PF1550_FAST_CHARGE_CONSTANT_CURRENT,
		PF1550_FAST_CHARGE_CONSTANT_VOLTAGE,
		PF1550_END_OF_CHARGE,
		PF1550_CHARGE_DONE,
		PF1550_TIMER_FAULT = 6,
		PF1550_THERMISTOR_SUSPEND,
		PF1550_CHARGER_OFF_INVALID_INPUT,
		PF1550_BATTERY_OVERVOLTAGE,
		PF1550_BATTERY_OVERTEMPERATURE,
		PF1550_CHARGER_OFF_LINEAR_MODE = 12,
	};

	const struct charger_pf1550_config *const config = dev->config;
	uint8_t val;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->bus, CHARGER_CHG_SNS, &val);
	if (ret) {
		return ret;
	}

	val = FIELD_GET(GENMASK(3,0), val);

	if (val == PF1550_CHARGE_DONE) {
		*status = CHARGER_STATUS_FULL;
	} else if (val < PF1550_CHARGE_DONE) {
		*status = CHARGER_STATUS_CHARGING;
	} else {
		*status = CHARGER_STATUS_NOT_CHARGING;
	}

	return 0;
}

static int pf1550_get_charger_online(const struct device *dev, enum charger_online *online)
{
	enum {
		PF1550_CHARGER_OFF_LINEAR_OFF,
		PF1550_CHARGER_OFF_LINEAR_ON,
		PF1550_CHARGER_ON_LINEAR_ON,
	};
	const struct charger_pf1550_config *const config = dev->config;
	uint8_t val;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->bus, CHARGER_CHG_OPER, &val);
	if (ret) {
		return ret;
	}

	val = FIELD_GET(GENMASK(1,0), val);

	switch (val) {
	case PF1550_CHARGER_ON_LINEAR_ON:
		*online = CHARGER_ONLINE_FIXED;
		break;
	default:
		*online = CHARGER_ONLINE_OFFLINE;
		break;
	};

	return 0;
}

static int pf1550_set_constant_charge_current(const struct device *dev,
						uint32_t current_ua)
{
	const struct charger_pf1550_config *const config = dev->config;
	uint16_t idx;
	uint8_t val;
	int ret;

	ret = linear_range_group_get_index(charger_fast_charge_ua_range, ARRAY_SIZE(charger_fast_charge_ua_range), current_ua, &idx);
	if (ret < 0) {
		return ret;
	}

	val = FIELD_PREP(CHARGER_CHG_CURR_CFG, idx);

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_CHG_CURR_CFG,
				      GENMASK(4,0),
				      val);
}

static int pf1550_set_vbus_ilim(const struct device *dev, uint32_t current_ua)
{
	const struct charger_pf1550_config *const config = dev->config;
	uint16_t idx;
	uint8_t val;
	int ret;

	ret = linear_range_group_get_index(charger_vbus_ilim_range, ARRAY_SIZE(charger_vbus_ilim_range), current_ua, &idx);
	if (ret < 0) {
		return ret;
	}

	val = FIELD_PREP(GENMASK(7,3), idx);

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_VBUS_INLIM_CNFG,
				      GENMASK(7,3),
				      val);
}

static int pf1550_set_vsys_min(const struct device *dev, uint32_t voltage_uv)
{
	const struct charger_pf1550_config *const config = dev->config;
	uint16_t idx;
	uint8_t val;
	int ret;

	ret = linear_range_group_get_index(charger_vsysmin_uv, ARRAY_SIZE(charger_vsysmin_uv), voltage_uv, &idx);
	if (ret < 0) {
		return ret;
	}

	val = FIELD_PREP(GENMASK(7,6), idx);

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_BATT_REG,
				      GENMASK(7,6),
				      val);
}

static int pf1550_set_charge_termination_uv(const struct device *dev, uint32_t voltage_uv)
{
	const struct charger_pf1550_config *const config = dev->config;
	uint16_t idx;
	uint8_t val;
	int ret;

	ret = linear_range_group_get_index(charger_battery_termination_uv_range, ARRAY_SIZE(charger_battery_termination_uv_range), voltage_uv, &idx);
	if (ret < 0) {
		return ret;
	}

	val = FIELD_PREP(GENMASK(5,0), idx);

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_BATT_REG,
				      GENMASK(5,0),
				      val);
}

static int pf1550_set_thermistor_mode(const struct device *dev,
					enum charger_pf1550_therm_mode mode)
{
	const struct charger_pf1550_config *const config = dev->config;
	uint8_t val = mode;
	val = FIELD_PREP(GENMASK(1, 0), val);

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_THM_REG_CNFG,
				      GENMASK(1, 0),
				      val);
}

static int pf1550_set_enabled(const struct device *dev, bool enable)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *const config = dev->config;

	data->charger_enabled = enable;

	return i2c_reg_update_byte_dt(&config->bus,
				      CHARGER_CHG_OPER,
				      GENMASK(1,0),
				      enable ? 2 : 0);
}

static int pf1550_get_interrupt_source(const struct device *dev, uint8_t *int_a)
{
	const struct charger_pf1550_config *config = dev->config;
	uint8_t dummy;
	uint8_t *int_src;

	int_src = (int_a != NULL) ? int_a : &dummy;
	return i2c_reg_read_byte_dt(&config->bus, CHARGER_CHG_INT, int_src);
}

static int pf1550_enable_interrupts(const struct device *dev)
{
	enum {MASKA_VAL_ENABLE = 0xFF};
	const struct charger_pf1550_config *config = dev->config;
	int ret;

	ret = pf1550_get_interrupt_source(dev, NULL);
	if (ret < 0) {
		LOG_WRN("Failed to clear pending interrupts: %d", ret);
		return ret;
	}

	return i2c_reg_write_byte_dt(&config->bus, CHARGER_CHG_INT_MASK, MASKA_VAL_ENABLE);
}

static int pf1550_led_config(const struct device *dev)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *config = dev->config;
	int ret;
	uint8_t val;

	struct charger_pf1550_led_config* cfg = data->led_config;

	val = (cfg->enabled ? BIT(7) : 0) | (cfg->duty / 3);

	ret = i2c_reg_write_byte_dt(&config->bus, CHARGER_LED_PWM, val);
	if (ret < 0) {
		return ret;
	}

	val = (cfg->manual ? BIT(5) : 0) | (cfg->behaviour ? BIT(4) : 0) | cfg->frequency;

	return i2c_reg_write_byte_dt(&config->bus, CHARGER_LED_CNFG, val);
}

#define LED_FREQUENCY_1_HZ			0
#define LED_FREQUENCY_0_5_HZ		1
#define LED_FREQUENCY_256_HZ		2
#define LED_FREQUENCY_8_HZ			3

static int pf1550_init_properties(const struct device *dev)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *config = dev->config;
	int ret;

	data->charger_enabled = true;
	data->charge_current_ua = config->charge_current_ua;
	data->vbus_ilim_ua = config->vbus_ilim_ua;

	static struct charger_pf1550_led_config led_config = {
		.enabled = true,
		.manual = false,
		.duty = 10,
		.frequency = LED_FREQUENCY_1_HZ,
		.behaviour = ON_CHARGING_FLASH_FAULT_OFF_DONE,
	};

	data->led_config = &led_config;

	ret = pf1550_get_charger_status(dev, &data->charger_status);
	if (ret < 0) {
		LOG_ERR("Failed to read charger status: %d", ret);
		return ret;
	}

	ret = pf1550_get_charger_online(dev, &data->charger_online);
	if (ret < 0) {
		LOG_ERR("Failed to read charger online: %d", ret);
		return ret;
	}

	return 0;
}

enum charger_pf1550_therm_mode pf1550_string_to_therm_mode(const char *mode_string)
{
	static const char * const modes[] = {
		[PF1550_THERM_MODE_DISABLED] = "disabled",
		[PF1550_THERM_MODE_THERMISTOR] = "thermistor",
		[PF1550_THERM_MODE_JEITA_1] = "JEITA-1",
		[PF1550_THERM_MODE_JEITA_2] = "JEITA-2",
	};
	enum charger_pf1550_therm_mode i;

	for (i = PF1550_THERM_MODE_DISABLED; i < ARRAY_SIZE(modes); i++) {
		if (strncmp(mode_string, modes[i], strlen(modes[i])) == 0) {
			return i;
		}
	}

	return PF1550_THERM_MODE_UNKNOWN;
}

static int pf1550_update_properties(const struct device *dev)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *config = dev->config;
	enum charger_pf1550_therm_mode therm_mode;
	int ret;

	ret = pf1550_set_vbus_ilim(dev, config->vbus_ilim_ua);
	if (ret < 0) {
		LOG_ERR("Failed to set vbus current limit: %d", ret);
		return ret;
	}

	ret = pf1550_set_vsys_min(dev, config->vsys_min_uv);
	if (ret < 0) {
		LOG_ERR("Failed to set minimum system voltage threshold: %d", ret);
		return ret;
	}

	ret = pf1550_set_charge_termination_uv(dev, config->battery_charge_termination_uv);
	if (ret < 0) {
		LOG_ERR("Failed to set recharge threshold: %d", ret);
		return ret;
	}

	therm_mode = pf1550_string_to_therm_mode(config->therm_mon_mode);
	ret = pf1550_set_thermistor_mode(dev, therm_mode);
	if (ret < 0) {
		LOG_ERR("Failed to set thermistor mode: %d", ret);
		return ret;
	}

	ret = pf1550_set_constant_charge_current(dev, data->charge_current_ua);
	if (ret < 0) {
		LOG_ERR("Failed to set charge voltage: %d", ret);
		return ret;
	}

	ret = pf1550_set_enabled(dev, data->charger_enabled);
	if (ret < 0) {
		LOG_ERR("Failed to set enabled: %d", ret);
		return ret;
	}

	ret = pf1550_led_config(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure led: %d", ret);
		return ret;
	}

	return 0;
}

static int pf1550_get_prop(const struct device *dev, charger_prop_t prop,
			     union charger_propval *val)
{
	struct charger_pf1550_data *data = dev->data;

	switch (prop) {
	case CHARGER_PROP_ONLINE:
		val->online = data->charger_online;
		return 0;
	case CHARGER_PROP_STATUS:
		val->status = data->charger_status;
		return 0;
	case CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA:
		val->const_charge_current_ua = data->charge_current_ua;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int pf1550_set_prop(const struct device *dev, charger_prop_t prop,
			     const union charger_propval *val)
{
	struct charger_pf1550_data *data = dev->data;
	int ret;

	switch (prop) {
	case CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA:
		ret = pf1550_set_constant_charge_current(dev, val->const_charge_current_ua);
		if (ret == 0) {
			data->charge_current_ua = val->const_charge_current_ua;
		}
		return ret;
	case CHARGER_PROP_INPUT_REGULATION_CURRENT_UA:
		ret = pf1550_set_vbus_ilim(dev, val->input_current_regulation_current_ua);
		if (ret == 0) {
			data->vbus_ilim_ua = val->input_current_regulation_current_ua;
		}
		return ret;
	case CHARGER_PROP_STATUS_NOTIFICATION:
		data->charger_status_notifier = val->status_notification;
		return 0;
	case CHARGER_PROP_ONLINE_NOTIFICATION:
		data->charger_online_notifier = val->online_notification;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int pf1550_enable_interrupt_pin(const struct device *dev, bool enabled)
{
	const struct charger_pf1550_config *const config = dev->config;
	gpio_flags_t flags;
	int ret;

	flags = enabled ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE;

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, flags);
	if (ret < 0) {
		LOG_ERR("Could not %s interrupt GPIO callback: %d", enabled ? "enable" : "disable",
			ret);
	}

	return ret;
}

static void pf1550_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				   uint32_t pins)
{
	struct charger_pf1550_data *data = CONTAINER_OF(cb, struct charger_pf1550_data,
							  gpio_cb);
	int ret;

	(void) pf1550_enable_interrupt_pin(data->dev, false);

	ret = k_work_submit(&data->int_routine_work);
	if (ret < 0) {
		LOG_WRN("Could not submit int work: %d", ret);
	}
}

static void pf1550_int_routine_work_handler(struct k_work *work)
{
	struct charger_pf1550_data *data = CONTAINER_OF(work, struct charger_pf1550_data,
							  int_routine_work);
	uint8_t int_src;
	int ret;

	ret = pf1550_get_interrupt_source(data->dev, &int_src);
	if (ret < 0) {
		LOG_WRN("Failed to read interrupt source");
		return;
	}

	LOG_DBG("Interrupt received: %x",int_src);

	ret = pf1550_get_charger_status(data->dev, &data->charger_status);
	if (ret < 0) {
		LOG_WRN("Failed to read charger status: %d", ret);
	} else {
		if (data->charger_status_notifier != NULL) {
			data->charger_status_notifier(data->charger_status);
		}
	}

	ret = pf1550_get_charger_online(data->dev, &data->charger_online);
	if (ret < 0) {
		LOG_WRN("Failed to read charger online %d", ret);
	} else {
		if (data->charger_online_notifier != NULL) {
			data->charger_online_notifier(data->charger_online);
		}
	}

	if (data->charger_online != CHARGER_ONLINE_OFFLINE) {
		(void) pf1550_update_properties(data->dev);
	}

	ret = k_work_reschedule(&data->int_enable_work, INT_ENABLE_DELAY);
	if (ret < 0) {
		LOG_WRN("Could not reschedule int_enable_work: %d", ret);
	}
}

static void pf1550_int_enable_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct charger_pf1550_data *data = CONTAINER_OF(dwork, struct charger_pf1550_data,
							  int_enable_work);

	(void) pf1550_enable_interrupt_pin(data->dev, true);
}

static int pf1550_configure_interrupt_pin(const struct device *dev)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *config = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&config->int_gpio)) {
		LOG_ERR("Interrupt GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Could not configure interrupt GPIO");
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, pf1550_gpio_callback, BIT(config->int_gpio.pin));
	ret = gpio_add_callback_dt(&config->int_gpio, &data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("Could not add interrupt GPIO callback");
		return ret;
	}

	return 0;
}

static int pf1550_init(const struct device *dev)
{
	struct charger_pf1550_data *data = dev->data;
	const struct charger_pf1550_config *config = dev->config;
	int ret;

	if (!i2c_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	data->dev = dev;

	ret = pf1550_init_properties(dev);
	if (ret < 0) {
		return ret;
	}

	k_work_init(&data->int_routine_work, pf1550_int_routine_work_handler);
	k_work_init_delayable(&data->int_enable_work, pf1550_int_enable_work_handler);

	ret = pf1550_configure_interrupt_pin(dev);
	if (ret < 0) {
		return ret;
	}

	ret = pf1550_enable_interrupt_pin(dev, true);
	if (ret < 0) {
		return ret;
	}

	ret = pf1550_enable_interrupts(dev);
	if (ret < 0) {
		LOG_ERR("Failed to enable interrupts");
		return ret;
	}

	ret = pf1550_update_properties(dev);
	if (ret < 0) {
		LOG_ERR("Failed to setup charger");
		return ret;
	}

	return 0;
}

static const struct charger_driver_api pf1550_driver_api = {
	.get_property = pf1550_get_prop,
	.set_property = pf1550_set_prop,
	.charge_enable = pf1550_set_enabled,
};

/*
#define PF1550_LED_DEFINE(inst)								\
	static struct charger_pf1550_led_config_##inst = {				\
		.enabled = DT_INST_PROP(inst, constant_charge_current_max_microamp),	\
	}

	TODO: add led_config field to dt
*/

#define PF1550_DEFINE(inst)									\
	static struct charger_pf1550_data charger_pf1550_data_##inst;			\
	static const struct charger_pf1550_config charger_pf1550_config_##inst = {		\
		.bus = I2C_DT_SPEC_GET(DT_INST_PARENT(inst)),					\
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),				\
		.charge_current_ua = DT_INST_PROP(inst, constant_charge_current_max_microamp),	\
		.vbus_ilim_ua = DT_INST_PROP(inst, vbus_current_limit_microamp),\
		.vsys_min_uv = DT_INST_PROP(inst, system_voltage_min_threshold_microvolt),	\
		.therm_mon_mode = DT_INST_PROP(inst, thermistor_monitoring_mode),		\
		.battery_charge_termination_uv = DT_INST_PROP(inst, battery_termination_microvolt),		\
	};											\
												\
	DEVICE_DT_INST_DEFINE(inst, &pf1550_init, NULL, &charger_pf1550_data_##inst,	\
			      &charger_pf1550_config_##inst,					\
			      POST_KERNEL, CONFIG_MFD_INIT_PRIORITY,				\
			      &pf1550_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PF1550_DEFINE)
