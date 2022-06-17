/*
 * Copyright (c) 2017-2019 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr/drivers/sensor.h>


#define COAP_PORT 5683
#define STACK_SIZE 2048

#define MAX_COAP_MSG_LEN 256
#define MAX_RETRANSMIT_COUNT 2

#define THREAD_PRIORITY K_PRIO_PREEMPT(8)

#define STATS_TIMER 60 /* How often to print statistics (in seconds) */

#define ALL_NODES_LOCAL_COAP_MCAST \
	{ { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfd } } }

#define MY_IP6ADDR \
	{ { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1 } } }


#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/app_memdomain.h>
extern struct k_mem_partition apption;
extern struct k_mem_domain app_domain;
#define APP_BMEM K_APP_BMEM(app_partition)
#define APP_DMEM K_APP_DMEM(app_partition)
#else
#define APP_BMEM
#define APP_DMEM
#endif

struct config {
	const char *proto;

	struct {
		int sock;
		char recv_buffer[MAX_COAP_MSG_LEN];
		uint32_t counter;
		atomic_t bytes_received;
	} coap;
};

struct configs {
	struct config ipv6;
};

extern struct configs conf;

typedef struct 
{
	int presence;
	int luminance;
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value gas_res;
} sensor_data_t;

void start_coap(void);
void stop_coap(void);

void get_sensor_data(sensor_data_t *sensor_data);
int sensors_init(void);

void quit(void);
