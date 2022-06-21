/* udp.c - UDP specific code for echo server */

/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coap, LOG_LEVEL_INF);

#include <zephyr/zephyr.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/udp.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_link_format.h>
#include <zephyr/net/tls_credentials.h>

#include "common.h"
#include "net_private.h"
#include "ipv6.h"

#define NUM_OBSERVERS 10
#define NUM_PENDINGS 10
static struct coap_pending pendings[NUM_PENDINGS];
static struct coap_observer observers[NUM_OBSERVERS];

static struct k_work_delayable retransmit_work;

static void retransmit_request(struct k_work *work);
static void schedule_next_retransmission(void);
static int well_known_core_get(struct coap_resource *resource,
			       struct coap_packet *request,
			       struct sockaddr *addr, socklen_t addr_len);

static int echo_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static int temperature_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void temperature_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static int humidity_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void humidity_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static int  air_quality_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void  air_quality_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static int air_pressure_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void air_pressure_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static int presence_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void presence_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static int luminance_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len);

static void luminance_notify(struct coap_resource *resource,
		       struct coap_observer *observer);

static const char * const temperature_path[] = {"sensors", "temperature", NULL };
static const char * const humidity_path[] = {"sensors",  "humidity", NULL };
static const char * const air_quality_path[] = {"sensors",  "air_quality", NULL };
static const char * const air_pressure_path[] = {"sensors",  "air_pressure", NULL };
static const char * const presence_path[] = {"sensors",  "presence", NULL };
static const char * const luminance_path[] = {"sensors",  "luminance", NULL };
 
static const char * const echo_path[] = { "echo", NULL };

static struct coap_resource resources[] = {
	{ .get = well_known_core_get,
	  .path = COAP_WELL_KNOWN_CORE_PATH,
	},
	{
		.put = echo_put,
		.path = echo_path,
	}, 
	{
		.path = temperature_path,
		.get = temperature_get,
		.notify = temperature_notify,
	},
	{
		.path = humidity_path,
		.get = humidity_get,
		.notify = humidity_notify,
	},
	{
		.path = air_quality_path,
		.get = air_quality_get,
		.notify = air_quality_notify,
	},
	{
		.path = air_pressure_path,
		.get = air_pressure_get,
		.notify = air_pressure_notify,
	},
	{
		.path = presence_path,
		.get = presence_get,
		.notify = presence_notify,
	},
	{
		.path = luminance_path,
		.get = luminance_get,
		.notify = luminance_notify,
	},
	{ }
};

static void coap_server_process_received_packet(uint8_t *data, uint16_t data_len,
				 struct sockaddr *client_addr,
				 socklen_t client_addr_len);

static void coap_server_thread(void);

K_THREAD_DEFINE(coap_thread_id, STACK_SIZE,
		coap_server_thread, NULL, NULL, NULL,
		THREAD_PRIORITY,
		IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, -1);

//--------------------------------------------------------
// Setup and Destroy functions
//--------------------------------------------------------

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

static int init_coap_proto(struct config *cfg, struct sockaddr *bind_addr,
			   socklen_t bind_addrlen)
{
	int ret;

	cfg->coap.sock = socket(bind_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);

	if (cfg->coap.sock < 0) {
		NET_ERR("Failed to create UDP socket (%s): %d", cfg->proto,
			errno);
		return -errno;
	}

	ret = bind(cfg->coap.sock, bind_addr, bind_addrlen);
	if (ret < 0) {
		NET_ERR("Failed to bind UDP socket (%s): %d", cfg->proto,
			errno);
		ret = -errno;
	}

	return ret;
}

void start_coap(void)
{
	join_coap_multicast_group();
	k_work_init_delayable(&retransmit_work, retransmit_request);

#if defined(CONFIG_USERSPACE)
		k_mem_domain_add_thread(&app_domain, coap_thread_id);
#endif

		k_thread_name_set(coap_thread_id, "coap");
		k_thread_start(coap_thread_id);
}

void stop_coap(void)
{
	/* Not very graceful way to close a thread, but as we may be blocked
	 * in recvfrom call it seems to be necessary
	 */
	if (IS_ENABLED(CONFIG_NET_IPV6)) {
		k_thread_abort(coap_thread_id);
		if (conf.ipv6.coap.sock >= 0) {
			(void)close(conf.ipv6.coap.sock);
		}
	}
}

//--------------------------------------------------------
// Main Thread Loop
//--------------------------------------------------------

static void coap_server_thread(void)
{
	int ret = 0;
	int received;
	struct sockaddr client_addr;
	socklen_t client_addr_len;
	client_addr_len = sizeof(client_addr);

	struct sockaddr_in6 addr6;

	(void)memset(&addr6, 0, sizeof(addr6));
	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = htons(COAP_PORT);

	ret = init_coap_proto(&conf.ipv6, (struct sockaddr *)&addr6, sizeof(addr6));
	if (ret < 0) {
		quit();
		return;
	}

	while (ret == 0) {
		
		received = recvfrom(conf.ipv6.coap.sock, conf.ipv6.coap.recv_buffer,
				    sizeof(conf.ipv6.coap.recv_buffer), 0,
				    &client_addr, &client_addr_len);

		if (received < 0) {
			LOG_ERR("Connection error %d", errno);
			quit();
			return;
		}
		LOG_DBG("Received CoAP Packet");
		coap_server_process_received_packet(conf.ipv6.coap.recv_buffer, received, &client_addr,
				     client_addr_len);
		
	}
}

//--------------------------------------------------------
// Send and receive Packets
//--------------------------------------------------------

static struct coap_resource *find_resource_by_observer(
		struct coap_resource *resources, struct coap_observer *o)
{
	struct coap_resource *r;

	for (r = resources; r && r->path; r++) {
		sys_snode_t *node;

		SYS_SLIST_FOR_EACH_NODE(&r->observers, node) {
			if (&o->list == node) {
				return r;
			}
		}
	}

	return NULL;
}

static void remove_observer(struct sockaddr *addr)
{
	struct coap_resource *r;
	struct coap_observer *o;

	o = coap_find_observer_by_addr(observers, NUM_OBSERVERS, addr);
	if (!o) {
		return;
	}

	r = find_resource_by_observer(resources, o);
	if (!r) {
		LOG_ERR("Observer found but Resource not found\n");
		return;
	}

	LOG_INF("Removing observer %p", o);

	coap_remove_observer(r, o);
	memset(o, 0, sizeof(struct coap_observer));
}

static void retransmit_request(struct k_work *work)
{
	struct coap_pending *pending;
	int r;

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return;
	}

	if (!coap_pending_cycle(pending)) {
		LOG_ERR("Pending Retransmission timed out");
		remove_observer(&pending->addr);
		k_free(pending->data);
		coap_pending_clear(pending);
	} else {
		net_hexdump("Retransmit", pending->data, pending->len);

		r = sendto(conf.ipv6.coap.sock, pending->data, pending->len, 0,
			   &pending->addr, sizeof(struct sockaddr_in6));
		if (r < 0) {
			LOG_ERR("Failed to send %d", errno);
		}
	}

	schedule_next_retransmission();
}

static void schedule_next_retransmission(void)
{
	struct coap_pending *pending;
	int32_t remaining;
	uint32_t now = k_uptime_get_32();

	/* Get the first pending retransmission to expire after cycling. */
	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return;
	}

	remaining = pending->t0 + pending->timeout - now;
	if (remaining < 0) {
		LOG_ERR("Retransmission timed out");
		remaining = 0;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(remaining));
}


static int create_pending_request(struct coap_packet *response,
				  const struct sockaddr *addr)
{
	struct coap_pending *pending;
	int r;

	pending = coap_pending_next_unused(pendings, NUM_PENDINGS);
	if (!pending) {
		return -ENOMEM;
	}

	r = coap_pending_init(pending, response, addr, MAX_RETRANSMIT_COUNT);
	if (r < 0) {
		return -EINVAL;
	}

	coap_pending_cycle(pending);

	schedule_next_retransmission();

	return 0;
}

static void coap_server_process_received_packet(uint8_t *data, uint16_t data_len,
				 struct sockaddr *client_addr,
				 socklen_t client_addr_len)
{
	struct coap_packet request;
	struct coap_pending *pending;
	struct coap_option options[16] = { 0 };
	uint8_t opt_num = 16U;
	uint8_t type;
	int r;

	r = coap_packet_parse(&request, data, data_len, options, opt_num);
	if (r < 0) {
		LOG_ERR("Invalid data received (%d)\n", r);
		return;
	}

	type = coap_header_get_type(&request);

	pending = coap_pending_received(&request, pendings, NUM_PENDINGS);
	if (!pending) {
		r = coap_handle_request(&request, resources, options, opt_num,
					client_addr, client_addr_len);
		if (r < 0) {
			LOG_WRN("No handler for such request (%d)\n", r);
		}
	}
	/* Clear CoAP pending request */
	else if (type == COAP_TYPE_ACK || type == COAP_TYPE_RESET) {
		k_free(pending->data);
		coap_pending_clear(pending);

		if (type == COAP_TYPE_RESET) {
			remove_observer(client_addr);
		}
	}
}

static int send_coap_reply(struct coap_packet *cpkt,
			   const struct sockaddr *addr,
			   socklen_t addr_len)
{
	int r;

	net_hexdump("Reply", cpkt->data, cpkt->offset);

	r = sendto(conf.ipv6.coap.sock, cpkt->data, cpkt->offset, 0, addr, addr_len);
	if (r < 0) {
		LOG_ERR("Failed to send %d", errno);
		r = -errno;
	}

	return r;
}

static int send_notification_packet(const struct sockaddr *addr,
				    socklen_t addr_len,
				    uint16_t age, uint16_t id,
				    const uint8_t *token, uint8_t tkl,
				    bool is_response, void* payload, uint8_t payload_length )
{
	struct coap_packet response;
	uint8_t *data;
	uint8_t type;
	int r;

	if (is_response) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_CON;
	}

	if (!is_response) {
		id = coap_next_id();
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	
	if (r == 0 && age >= 2U) {
		r = coap_append_option_int(&response, COAP_OPTION_OBSERVE, age);
	}

	if(r == 0)
	{	
		r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
					COAP_CONTENT_FORMAT_TEXT_PLAIN);
	}

	if(r == 0)
	{
		r = coap_packet_append_payload_marker(&response);
	}

	if(r == 0)
	{
		r = coap_packet_append_payload(&response, (uint8_t *)payload, 
				       payload_length);
	}

	if (r == 0 && type == COAP_TYPE_CON) {
		r = create_pending_request(&response, addr);
	}

	if(r == 0)
	{
		r = send_coap_reply(&response, addr, addr_len);

		/* On successful creation of pending request, do not free memory */
		if (type == COAP_TYPE_CON) {
			return r;
		}
	}
	
	k_free(data);

	return r;
}

//--------------------------------------------------------
// Resources
//--------------------------------------------------------

static int well_known_core_get(struct coap_resource *resource,
			       struct coap_packet *request,
			       struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_well_known_core_get(resource, request, &response,
				     data, MAX_COAP_MSG_LEN);
	if (r == 0) {
		r = send_coap_reply(&response, addr, addr_len);
	}

	k_free(data);

	return r;
}

static int echo_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		net_hexdump("PUT Payload", payload, payload_len);
	}

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r == 0) {

		r = coap_packet_append_payload_marker(&response);
		if (r == 0) {
			r = coap_packet_append_payload(&response, payload, payload_len);
			if (r == 0) {
				r = send_coap_reply(&response, addr, addr_len);
			}

		}

	}

	k_free(data);

	return r;
}

static int temperature_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char temp[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(temp, "%d.%.2i",sensor_data.temp.val1,sensor_data.temp.val2);
	
	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &temp, strlen(temp));
}

static void temperature_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	char temp[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(temp, "%d.%.2i",sensor_data.temp.val1,sensor_data.temp.val2);
	
	LOG_INF("Sending Temperature Resource Notification: %s", temp);

	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &temp, strlen(temp));
}

static int humidity_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char humidity[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(humidity, "%d.%.2i",sensor_data.humidity.val1,sensor_data.humidity.val2);
	
	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &humidity, strlen(humidity));
}

static void humidity_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	char humidity[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(humidity, "%d.%.2i",sensor_data.humidity.val1,sensor_data.humidity.val2);
	
	LOG_INF("Sending Humidty Resource Notification: %s", humidity);

	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &humidity, strlen(humidity));
}

static int  air_quality_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char air_quality[10];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(air_quality, "%d",sensor_data.air_quality_index);

	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &air_quality, strlen(air_quality));
}

static void  air_quality_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	char air_quality[10];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(air_quality, "%d",sensor_data.air_quality_index);

	LOG_INF("Sending Air Quality Resource Notification: %s", air_quality);
	
	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &air_quality, strlen(air_quality));
}

static int air_pressure_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char air_pressure[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(air_pressure, "%d.%.2i",sensor_data.press.val1,sensor_data.press.val2);
	
	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &air_pressure, strlen(air_pressure));
}

static void air_pressure_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	char air_pressure[20];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(air_pressure, "%d.%.2i",sensor_data.press.val1,sensor_data.press.val2);

	LOG_INF("Sending Air Pressure Resource Notification: %s", air_pressure);
	
	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &air_pressure, strlen(air_pressure));
}

static int presence_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char presence[5];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(presence, "%d",sensor_data.presence);
	
	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &presence, strlen(presence));
}

static void presence_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	char presence[5];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(presence, "%d",sensor_data.presence);

	LOG_INF("Sending Presence Resource Notification: %s", presence);
	
	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &presence, strlen(presence));
}

static int luminance_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;

	if (!coap_request_is_observe(request)) {
		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1) {
			remove_observer(addr);
		}
		observe = false;
		
	}
	else
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			LOG_ERR("Not enough observer slots.");
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);
	}

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	char luminance[5];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(luminance, "%d",sensor_data.luminance);
	
	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,
				 &luminance, strlen(luminance));
}

static void luminance_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	if(resource == NULL || observer == NULL) return;

	char luminance[5];
	sensor_data_t sensor_data;
	get_sensor_data(&sensor_data);
	sprintf(luminance, "%d",sensor_data.luminance);

	LOG_INF("Sending Luminance Resource Notification: %s", luminance);
	
	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false,
				 &luminance, strlen(luminance));

}


void coap_resource_update(int resource_id)
{
	if(resource_id > LAST_ID_RESOURCE_ID)
	{
		return;
	}

	coap_resource_notify(&resources[resource_id]);
}