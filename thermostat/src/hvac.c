/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/adc.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#include <stdio.h>
#include "common.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(hvac, LOG_LEVEL_DBG);

//--------------------------------------------------------
// Alias definitions 
//--------------------------------------------------------

#define HEATING_NODE	DT_ALIAS(led0)
#if !DT_NODE_HAS_STATUS(HEATING_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec heating_out = GPIO_DT_SPEC_GET_OR(HEATING_NODE, gpios, {0});

#define COOLING_NODE	DT_ALIAS(led1)
#if !DT_NODE_HAS_STATUS(COOLING_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec cooling_out = GPIO_DT_SPEC_GET_OR(COOLING_NODE, gpios, {0});

#define VENTING_NODE	DT_ALIAS(led2)
#if !DT_NODE_HAS_STATUS(VENTING_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec venting_out = GPIO_DT_SPEC_GET_OR(VENTING_NODE, gpios, {0});



//--------------------------------------------------------
// Static helper functions 
//--------------------------------------------------------

int outputs_init(void);
void hvac_thread(void);

//--------------------------------------------------------
// Runtime Variables 
//--------------------------------------------------------

static double temperature_min;
static double temperature_max;
static double temperature_min_presence;
static double temperature_max_presence;
static double humidity_max;
static int air_quality_max;

static int presence;
static double temperature;
static double humidity;
static int air_quality;


// Thread definitions to query the sensor data
K_THREAD_DEFINE(hvac_thread_id, STACK_SIZE,
		hvac_thread, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

int hvac_init(double temp_min, double temp_max, 
				double temp_min_presence, double temp_max_presence, 
				double hum_max, int air_qual_max)
{
	int ret = outputs_init();
	if(ret < 0)
	{
		return ret;
	}

	temperature_min = temp_min;
	temperature_max = temp_max;
	temperature_min_presence = temp_min_presence;
	temperature_max_presence = temp_max_presence;
	humidity_max = hum_max;
	air_quality_max = air_qual_max;

#if defined(CONFIG_USERSPACE)
		k_mem_domain_add_thread(&app_domain, hvac_thread_id);	
		LOG_DBG("starting Thread");
#endif

	k_thread_name_set(hvac_thread_id, "hvac-thread");
	k_thread_start(hvac_thread_id);

	return 0;
}

void hvac_thread(void)
{
	int heating_state = 0;
	int cooling_state = 0;
	int venting_state = 0;

	while(true)
	{
		if(presence == 0)
		{
			if(temperature > temperature_max)
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 0);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 1);
				if(cooling_state == 0)
				{
					LOG_INF("Enabling Cooling");
				}
				heating_state = 0;
				cooling_state = 1;
			}
			else if (temperature < temperature_min)
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 1);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 0);
				if(heating_state == 0)
				{
					LOG_INF("Enabling Heating");
				}
				heating_state = 1;
				cooling_state = 0;
			} 
			else
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 0);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 0);
				if(cooling_state == 1)
				{
					LOG_INF("Disabling Cooling");
				}
				else if(heating_state == 1)
				{
					LOG_INF("Disabling Heating");
				}

				heating_state = 0;
				cooling_state = 0;
			}
		}
		else
		{
			if(temperature > temperature_max_presence)
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 0);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 1);
				if(cooling_state == 0)
				{
					LOG_INF("Enabling Cooling");
				}

				heating_state = 0;
				cooling_state = 1;
			}
			else if (temperature < temperature_min_presence)
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 1);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 0);
				if(heating_state == 0)
				{
					LOG_INF("Enabling Heating");
				}
				heating_state = 1;
				cooling_state = 0;
			} 
			else
			{
				gpio_pin_set(heating_out.port, heating_out.pin, 0);
				gpio_pin_set(cooling_out.port, cooling_out.pin, 0);
				if(cooling_state == 1)
				{
					LOG_INF("Disabling Cooling");
				}
				else if(heating_state == 1)
				{
					LOG_INF("Disabling Heating");
				}
				heating_state = 0;
				cooling_state = 0;
			}
		}

		if(air_quality > air_quality_max || humidity > humidity_max)
		{
			gpio_pin_set(venting_out.port, venting_out.pin, 1);
			if(venting_state == 0)
			{
				LOG_INF("Enabling Venting");
			}
			venting_state =1;
		}
		else
		{
			gpio_pin_set(venting_out.port, venting_out.pin, 0);
			if(venting_state == 1)
			{
				LOG_INF("Disabling Venting");
			}
			venting_state =0;
		}
		

		gpio_pin_set(heating_out.port, heating_out.pin, heating_state);
		gpio_pin_set(cooling_out.port, cooling_out.pin, cooling_state);
		gpio_pin_set(venting_out.port, venting_out.pin, venting_state);

		LOG_DBG("Current State: heating_state %d cooling_state %d venting_state %d", heating_state, cooling_state, venting_state);
		LOG_DBG("temperature_min %lf, temperature %lf, temperature_max %lf", temperature_min, temperature, temperature_max);
		LOG_DBG("temperature_min_presence %lf, temperature %lf,  temperature_max_presence %lf", temperature_min_presence, temperature, temperature_max_presence);
		LOG_DBG("air_quality %d, air_quality_max %d, humidity %lf, humidity_max %lf", air_quality, air_quality_max, humidity, humidity_max);
		k_sleep(K_MSEC(5000));
	}
}

void hvac_update_temperatur(double temp)
{
    temperature = temp;
	LOG_DBG("New temperature value: %lf", temp);
}

void hvac_update_humidity(double hum)
{
    humidity = hum;
	LOG_DBG("New humidity value: %lf", hum);
}

void hvac_update_air_quality(int air_qual)
{
    air_quality = air_qual;
	LOG_DBG("New air quality value: %d", air_qual);
}

void hvac_update_pressence(int pres)
{
    presence = pres;
	LOG_DBG("New pressence value: %d", pres);
}


int outputs_init(void)
{
    
	int ret=device_is_ready(heating_out.port);

	if (!ret) {
		LOG_ERR("Error: heating_out device %s is not ready\n",
		       heating_out.port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(&heating_out, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, heating_out.port->name, heating_out.pin);
		return ret;
	}

	ret=device_is_ready(venting_out.port);

	if (!ret) {
		LOG_ERR("Error: venting_out device %s is not ready\n",
		       venting_out.port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(&venting_out, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, venting_out.port->name, venting_out.pin);
		return ret;
	}

	ret=device_is_ready(cooling_out.port);

	if (!ret) {
		LOG_ERR("Error: cooling_out device %s is not ready\n",
		       cooling_out.port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(&cooling_out, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, cooling_out.port->name, cooling_out.pin);
		return ret;
	}

	return 0;
}

