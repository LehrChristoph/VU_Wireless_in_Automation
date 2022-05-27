/*
 * Copyright (c) 2017-2019 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#define MY_PORT 4242
#define STACK_SIZE 2048

#define THREAD_PRIORITY K_PRIO_PREEMPT(8)

#define RECV_BUFFER_SIZE 1280
#define STATS_TIMER 60 /* How often to print statistics (in seconds) */

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

struct data {
	const char *proto;

	struct {
		int sock;
		char recv_buffer[RECV_BUFFER_SIZE];
		uint32_t counter;
		atomic_t bytes_received;
		struct k_work_delayable stats_print;
	} udp;
};

struct configs {
	struct data ipv4;
	struct data ipv6;
};

extern struct configs conf;

void start_udp(void);
void stop_udp(void);

void quit(void);
