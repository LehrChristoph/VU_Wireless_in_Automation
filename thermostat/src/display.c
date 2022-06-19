/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/zephyr.h>
#include <zephyr/drivers/sensor.h>

#include "common.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(display, LOG_LEVEL_DBG);


void display_thread(void);

// Thread definitions to query the sensor data
K_THREAD_DEFINE(display_thread_id, STACK_SIZE,
		display_thread, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

static int aiq;
static double temperature, humidty;

void init_display(void)
{	
	const struct device *display_dev;
	lv_obj_t *text_label;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return;
	}

	if (IS_ENABLED(CONFIG_LV_Z_POINTER_KSCAN)) {
		lv_obj_t *hello_world_button;

		hello_world_button = lv_btn_create(lv_scr_act());
		lv_obj_align(hello_world_button, LV_ALIGN_CENTER, 0, 0);
		text_label = lv_label_create(hello_world_button);
	} else {
		text_label = lv_label_create(lv_scr_act());
	}

	lv_label_set_text(text_label, "Thermostat\nT:\nH:\nAIQ:");
	lv_obj_align(text_label, LV_ALIGN_TOP_LEFT, 0, 0);

	lv_task_handler();
	display_blanking_off(display_dev);

    #if defined(CONFIG_USERSPACE)
		k_mem_domain_add_thread(&app_domain, display_thread_id);	
		LOG_DBG("starting Thread");
    #endif

        k_thread_name_set(display_thread_id, "display-thread");
        k_thread_start(display_thread_id);
	
}


void display_thread(void)
{
    char data_str[50] = {0};

    struct sensor_value temp, hum;

	lv_obj_t *data_label = lv_label_create(lv_scr_act());
	lv_obj_align(data_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    


    while (1) {
        
        sensor_value_from_double(&temp, temperature);
        sensor_value_from_double(&hum, humidty);
        
        snprintf(data_str, 50, "\n%d.%01d\n%d.%01d\n%d", temp.val1, temp.val2/10000 , hum.val1, hum.val2/10000, aiq);
        LOG_DBG("%s", data_str);
        lv_label_set_text(data_label, data_str);
		
		lv_task_handler();
		k_sleep(K_MSEC(10000));
	}
}

void display_update_temperatur(double temp)
{
    temperature = temp;
    k_wakeup(display_thread_id);
}

void display_update_humidity(double hum)
{
    humidty = hum;
    k_wakeup(display_thread_id);
}

void display_update_air_quality(int air_qual)
{
    aiq = air_qual;
    k_wakeup(display_thread_id);
}