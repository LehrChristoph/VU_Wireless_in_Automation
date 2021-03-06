/* echo-client.c - Networking echo client */

/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The echo-client application is acting as a client that is run in Zephyr OS,
 * and echo-server is run in the host acting as a server. The client will send
 * either unicast or multicast packets to the server which will reply the packet
 * back to the originator.
 *
 * In this sample application we create four threads that start to send data.
 * This might not be what you want to do in your app so caveat emptor.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(thermostat, LOG_LEVEL_INF);

#include <zephyr/zephyr.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_conn_mgr.h>


#ifndef TEMP_MIN
	#define TEMP_MIN 24.0
#endif

#ifndef TEMP_MAX
	#define TEMP_MAX 28.0
#endif

#ifndef TEMP_MIN_PRESENCE
	#define TEMP_MIN_PRESENCE 25.0
#endif

#ifndef TEMP_MAX_PRESENCE
	#define TEMP_MAX_PRESENCE 27.0
#endif

#ifndef HUMIDITY_MAX
	#define HUMIDITY_MAX 75.0
#endif

#ifndef AIQ_MAX
	#define AIQ_MAX 100
#endif

#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/app_memdomain.h>
K_APPMEM_PARTITION_DEFINE(app_partition);
struct k_mem_domain app_domain;
#endif

#include "common.h"
#define APP_BANNER "Thermostat"

#define INVALID_SOCK (-1)

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | \
		    NET_EVENT_L4_DISCONNECTED)


APP_DMEM struct configs conf = {
	.ipv6 = {
		.proto = "IPv6",
		.coap.sock = INVALID_SOCK,
	},
};

static APP_BMEM bool connected;
K_SEM_DEFINE(run_app, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;

// Join CoAP Multicast group
static bool join_coap_multicast_group(void)
{
	static struct in6_addr my_addr = MY_IP6ADDR;
	static struct sockaddr_in6 mcast_addr = {
		.sin6_family = AF_INET6,
		.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
		.sin6_port = htons(COAP_PORT) };
	struct net_if_addr *ifaddr;
	struct net_if *iface;

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("Could not get te default interface\n");
		return false;
	}

#if defined(CONFIG_NET_CONFIG_SETTINGS)
	if (net_addr_pton(AF_INET6,
			  CONFIG_NET_CONFIG_MY_IPV6_ADDR,
			  &my_addr) < 0) {
		LOG_ERR("Invalid IPv6 address %s",
			CONFIG_NET_CONFIG_MY_IPV6_ADDR);
	}
#endif

	ifaddr = net_if_ipv6_addr_add(iface, &my_addr, NET_ADDR_MANUAL, 0);
	if (!ifaddr) {
		LOG_ERR("Could not add unicast address to interface");
		return false;
	}

	ifaddr->addr_state = NET_ADDR_PREFERRED;

	struct net_if_mcast_addr *if_mcast_addr =	net_if_ipv6_maddr_add (iface, &mcast_addr.sin6_addr);
	
	if (if_mcast_addr == NULL) {
		LOG_ERR("Cannot join IPv6 multicast group");
		return false;
	}
	net_if_ipv6_maddr_join(if_mcast_addr);
	
	return true;
}

static void event_handler(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");

		connected = true;
		conf.ipv6.coap.mtu = net_if_get_mtu(iface);
		k_sem_give(&run_app);

		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_INF("Network disconnected");

		connected = false;
		k_sem_reset(&run_app);

		return;
	}
}

static void init_app(void)
{
	LOG_INF(APP_BANNER);

#if defined(CONFIG_USERSPACE)
	struct k_mem_partition *parts[] = {
		&app_partition
	};

	int ret = k_mem_domain_init(&app_domain, ARRAY_SIZE(parts), parts);

	__ASSERT(ret == 0, "k_mem_domain_init() failed %d", ret);
	ARG_UNUSED(ret);
#endif

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb,
					     event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);

		net_conn_mgr_resend_status();
	}
}


static int start_client(void)
{
	int ret;

	// Wait for the connection. 
	k_sem_take(&run_app, K_FOREVER);

	LOG_INF("Starting...");

	ret = start_coap();
	if (ret < 0) {
		return ret;
	}

	ret = coap_find_server();
	if (ret == 0) {

		ret = coap_register_observers();

		while (connected && (ret == 0)) {
			ret = coap_process();
		}
	
	}

	LOG_INF("Stopping...");

	stop_coap();

	return ret;
}

void main(void)
{
	init_app();

	if (!IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		/* If the config library has not been configured to start the
		 * app only after we have a connection, then we can start
		 * it right away.
		 */
		k_sem_give(&run_app);
	}

	join_coap_multicast_group();

	hvac_init(TEMP_MIN, TEMP_MAX, TEMP_MIN_PRESENCE, TEMP_MAX_PRESENCE, HUMIDITY_MAX, AIQ_MAX);
	init_display();

#if defined(CONFIG_USERSPACE)
	k_thread_access_grant(k_current_get(), &run_app);
	k_mem_domain_add_thread(&app_domain, k_current_get());
	k_thread_user_mode_enter((k_thread_entry_t)start_client, NULL, NULL,
				 NULL);
#else

	exit(start_client());
#endif
}
