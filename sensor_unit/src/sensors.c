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
LOG_MODULE_DECLARE(net_echo_server_sample, LOG_LEVEL_DBG);

static uint8_t current_id=0;
static uint8_t last_id=0;
static sensor_data_t gathered_sensor_data[2];

int get_lux_value(uint8_t channel);
int pir_init(void);
int get_pir_value(uint8_t pin);
void bme680_get_sensor_data(sensor_data_t *sensor_data);

static void query_sensor_data(void);

K_THREAD_DEFINE(sensor_thread_id, STACK_SIZE,
		query_sensor_data, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

int sensors_init(void)
{
	int ret = pir_init();
	if(ret < 0)
	{
		return ret;
	}

	LOG_DBG("pir_init done");
#if defined(CONFIG_USERSPACE)
		k_mem_domain_add_thread(&app_domain, sensor_thread_id);	
		LOG_DBG("starting Thread");
#endif

	k_thread_name_set(sensor_thread_id, "sensors");
	k_thread_start(sensor_thread_id);

	return 0;
}


static void query_sensor_data(void)
{

	do
	{
		uint8_t temp_id=current_id;
		current_id = last_id;
		last_id=temp_id;

		gathered_sensor_data[current_id].luminance = get_lux_value(0);
		gathered_sensor_data[current_id].presence =  get_pir_value(0);
		bme680_get_sensor_data(&gathered_sensor_data[current_id]);
		
		LOG_DBG("lux:%i;pir:%i;T:%d.%.2i;P:%d.%.2i;H:%d.%.2i;G:%d.%.2i\n",
				gathered_sensor_data[current_id].luminance, gathered_sensor_data[current_id].presence,
				gathered_sensor_data[current_id].temp.val1, gathered_sensor_data[current_id].temp.val2, 
				gathered_sensor_data[current_id].press.val1, gathered_sensor_data[current_id].press.val2,
				gathered_sensor_data[current_id].humidity.val1, gathered_sensor_data[current_id].humidity.val2, 
				gathered_sensor_data[current_id].gas_res.val1, gathered_sensor_data[current_id].gas_res.val2);
		
		// TODO call notify
		
		k_sleep(K_MSEC(5000));
	} while (true);
	
}


void get_sensor_data(sensor_data_t *sensor_data)
{
	*sensor_data = gathered_sensor_data[current_id];
}


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


void bme680_get_sensor_data(sensor_data_t *sensor_data)
{
	const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, bosch_bme680)));
	LOG_DBG("Device %p name is %s\n", dev, dev->name);
	int ret = device_is_ready(dev);
	if(!ret )
	{
		LOG_ERR("Device %s is not ready \n",
		       dev->name);
		return;
	}
	
	ret = sensor_sample_fetch(dev);
	if(ret != 0)
	{
		LOG_ERR("Unable to fetch sensor sample of %s: %i \n",
		       dev->name, -ret);
		return;
	}
	
    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &sensor_data->temp);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_AMBIENT_TEMP, dev->name, -ret);
		return;
	}
	
	ret = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &sensor_data->press);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_PRESS, dev->name, -ret);
		return;
	}
	ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &sensor_data->humidity);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_HUMIDITY, dev->name, -ret);
		return;
	}
	ret = sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &sensor_data->gas_res);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data for channle %i of %s: %i \n",
		       SENSOR_CHAN_GAS_RES, dev->name, -ret);
		return;
	}

    LOG_DBG("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n",
				sensor_data->temp.val1, sensor_data->temp.val2, 
				sensor_data->press.val1, sensor_data->press.val2,
				sensor_data->humidity.val1, sensor_data->humidity.val2, 
				sensor_data->gas_res.val1, sensor_data->gas_res.val2);
	
}

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define ADC_NUM_CHANNELS	DT_PROP_LEN(DT_PATH(zephyr_user), io_channels)

#if ADC_NUM_CHANNELS > 2
#error "Currently only 1 or 2 channels supported"
#endif

#if ADC_NUM_CHANNELS == 2 && !DT_SAME_NODE( \
	DT_PHANDLE_BY_IDX(DT_PATH(zephyr_user), io_channels, 0), \
	DT_PHANDLE_BY_IDX(DT_PATH(zephyr_user), io_channels, 1))
#error "Channels have to use the same ADC."
#endif

#define ADC_NODE		DT_PHANDLE(DT_PATH(zephyr_user), io_channels)

/* Common settings supported by most ADCs */
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT

#ifdef CONFIG_ADC_NRFX_SAADC
#define ADC_INPUT_POS_OFFSET SAADC_CH_PSELP_PSELP_AnalogInput0
#else
#define ADC_INPUT_POS_OFFSET 0
#endif

/* Get the numbers of up to two channels */
static uint8_t channel_ids[ADC_NUM_CHANNELS] = {
	DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 0),
#if ADC_NUM_CHANNELS == 2
	DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 1)
#endif
};

static int16_t sample_buffer[ADC_NUM_CHANNELS];

struct adc_channel_cfg channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	/* channel ID will be overwritten below */
	.channel_id = 0,
	.differential = 0
};

struct adc_sequence sequence = {
	/* individual channels will be added below */
	.channels    = 0,
	.buffer      = sample_buffer,
	/* buffer size in bytes, not number of samples */
	.buffer_size = sizeof(sample_buffer),
	.resolution  = ADC_RESOLUTION,
};

int get_lux_value(uint8_t channel)
{
	int err;
	const struct device *dev_adc = DEVICE_DT_GET(ADC_NODE);

	if (!device_is_ready(dev_adc)) {
		LOG_ERR("ADC device not found");
		return -1;
	}

	if (channel > ADC_NUM_CHANNELS - 1) {
			LOG_ERR("Channel %d was not configured!", channel);
			return -1;
	}

	/*
	 * Configure channels individually prior to sampling
	 */
        channel_cfg.channel_id = channel_ids[channel];
#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
        channel_cfg.input_positive = ADC_INPUT_POS_OFFSET + channel_ids[channel];
#endif

	adc_channel_setup(dev_adc, &channel_cfg);

	sequence.channels |= BIT(channel_ids[channel]);

	int32_t adc_vref = adc_ref_internal(dev_adc);

	while (1) {
		/*
		 * Read sequence of channels (fails if not supported by MCU)
		 */
		err = adc_read(dev_adc, &sequence);
		if (err != 0) {
			LOG_ERR("ADC reading failed with error %d", err);
			return -1;
		}

		int32_t raw_value = sample_buffer[channel];
		int32_t mv_value = raw_value;

		LOG_DBG("ADC reading: %d", raw_value);
		// TODO do a value conversion/correction
		
		if (adc_vref > 0) {
		    /*
		     * Convert raw reading to millivolts if driver
		     * supports reading of ADC reference voltage
		     */
			
		    //adc_raw_to_millivolts(adc_vref, ADC_GAIN,
		    //        ADC_RESOLUTION, &mv_value);
			mv_value = raw_value * 350;
		    mv_value >>=12;
			LOG_DBG(" = %d mV  ", mv_value);
		    
			
			return (mv_value);
		}
		return raw_value;
	}
}
