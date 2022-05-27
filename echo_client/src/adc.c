/*
 * Copyright (c) 2020 Libre Solar Technologies GmbH
 * Copyright (c) 2022 TU Wien
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/adc.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(net_echo_client_sample, LOG_LEVEL_DBG);

#include "common.h"

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
