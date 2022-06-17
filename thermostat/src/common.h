/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Value of 0 will cause the IP stack to select next free port */
#define MY_PORT 0
#define STACK_SIZE 2048

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
//4242

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


struct config {
	const char *proto;

	struct {
		int sock;
		/* Work controlling coap data sending */
		char recv_buffer[MAX_COAP_MSG_LEN];
		struct k_work_delayable recv;
		struct k_work_delayable transmit;
		uint32_t expecting;
		uint32_t counter;
		uint32_t mtu;
	} coap;
};

struct configs {
	struct config ipv6;
};

#define ALL_NODES_LOCAL_COAP_MCAST { { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfd } } }
#define MY_IP6ADDR { { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2 } } }


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

int start_coap(void);
int process_coap(void);
void stop_coap(void);