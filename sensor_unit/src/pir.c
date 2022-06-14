/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#include "common.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(net_echo_client_sample, LOG_LEVEL_DBG);

/*
 * Get pir_sensor configuration from the devicetree sw0 alias. This is mandatory.
 */
#define PIR0_NODE	DT_ALIAS(pir0)
#if !DT_NODE_HAS_STATUS(PIR0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec pir_sensor = GPIO_DT_SPEC_GET_OR(PIR0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;

void pir_changed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
    int value = gpio_pin_get(pir_sensor.port, pir_sensor.pin);
	LOG_DBG("Intr:  PIR value: %i, Dev: %s, Pin %i\n", value,pir_sensor.port->name, pir_sensor.pin);
	(void) send_sensor_values();
}


int get_pir_value(uint8_t pin)
{
    int value = gpio_pin_get(pir_sensor.port, pir_sensor.pin);
	LOG_DBG("PIR value: %i, Dev: %s, Pin %i\n", value,pir_sensor.port->name, pir_sensor.pin);
    return value;
}

int pir_init(void)
{
	int ret=device_is_ready(pir_sensor.port);

	if (!ret) {
		LOG_ERR("Error: pir_sensor device %s is not ready\n",
		       pir_sensor.port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(&pir_sensor, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, pir_sensor.port->name, pir_sensor.pin);
		return ret;
	}
	/*
	ret = gpio_pin_interrupt_configure_dt(&pir_sensor,
					      GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, pir_sensor.port->name, pir_sensor.pin);
		return ret;
	}

	gpio_init_callback(&button_cb_data, pir_changed, BIT(pir_sensor.pin));
	gpio_add_callback(pir_sensor.port, &button_cb_data);
	*/
	LOG_INF("Set up pir_sensor at %s pin %d\n", pir_sensor.port->name, pir_sensor.pin);

	return 0;
}
