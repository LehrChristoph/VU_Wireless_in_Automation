/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Value of 0 will cause the IP stack to select next free port */
#define MY_PORT 0

#define PEER_PORT 4242

#include <zephyr/drivers/sensor.h>

#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/app_memdomain.h>
extern struct k_mem_partition app_partition;
extern struct k_mem_domain app_domain;
#define APP_BMEM K_APP_BMEM(app_partition)
#define APP_DMEM K_APP_DMEM(app_partition)
#else
#define APP_BMEM
#define APP_DMEM
#endif

#if IS_ENABLED(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif

struct data {
	const char *proto;

	struct {
		int sock;
		/* Work controlling udp data sending */
		struct k_work_delayable recv;
		struct k_work_delayable transmit;
		uint32_t expecting;
		uint32_t counter;
		uint32_t mtu;
	} udp;
};

struct configs {
	struct data ipv6;
};

typedef struct 
{
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value gas_res;
} bme680_sensor_data_t;


#if !defined(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
#define CONFIG_NET_CONFIG_PEER_IPV4_ADDR ""
#endif

#if !defined(CONFIG_NET_CONFIG_PEER_IPV6_ADDR)
#define CONFIG_NET_CONFIG_PEER_IPV6_ADDR ""
#endif

extern char packet_buffer[];
extern int buffer_len;
extern struct configs conf;

int send_sensor_values(void);

int start_udp(void);
int process_udp(void);
void stop_udp(void);

int get_lux_value(uint8_t channel);

int pir_init(void);
int get_pir_value(uint8_t pin);

void bme680_get_sensor_data(bme680_sensor_data_t *sensor_data);