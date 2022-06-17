/* coap.c - UDP specific code for echo client */

/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_echo_client_sample, LOG_LEVEL_DBG);

#include <zephyr/sys/byteorder.h>
#include <zephyr/zephyr.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/udp.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/rand32.h>

#include "common.h"
#include "net_private.h"

#define RECV_BUF_SIZE 1280
#define UDP_SLEEP K_MSEC(150)
#define UDP_WAIT K_SECONDS(10)

static const char * const echo_path[] = { "echo", NULL };
static const char * const temperature_path[] = {"sensors", "temperature", NULL };
static const char * const humidity_path[] = {"sensors",  "humidity", NULL };
static const char * const air_quality_path[] = {"sensors",  "air_quality", NULL };
static const char * const air_pressure_path[] = {"sensors",  "air_pressure", NULL };
static const char * const presence_path[] = {"sensors",  "presence", NULL };
static const char * const luminance_path[] = {"sensors",  "luminance", NULL };

static APP_BMEM char recv_buf[RECV_BUF_SIZE];

static void wait_reply(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	/* This means that we did not receive response in time. */
	struct config *data = CONTAINER_OF(dwork, struct config, coap.recv);

	LOG_ERR("UDP %s: Data packet not received", data->proto);

	/* Send a new packet at this point */
	//send_udp_data(data);
}

static int process_coap_request(struct config *cfg)
{
	uint8_t payload[] = "Hello World!\n";
	struct coap_packet request;
	const char * const *p;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON,
			     COAP_TOKEN_MAX_LEN, coap_next_token(),
			     COAP_METHOD_GET, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
	}
	else
	{
		for (p = temperature_path; p && *p; p++) {
			r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
							*p, strlen(*p));
			if (r < 0) {
				break;
			}
		}
		
		if (r < 0) {
			LOG_ERR("Unable add option to request");
		}
		else
		{
				/*
			r = coap_packet_append_payload_marker(&request);
			if (r < 0) {
				LOG_ERR("Unable to append payload marker");
			}
			else
			{
				r = coap_packet_append_payload(&request, (uint8_t *)payload,
									sizeof(payload) - 1);
				if (r < 0) {
					LOG_ERR("Not able to append payload");
				}
				else
				*/
				{
					net_hexdump("Request", request.data, request.offset);

					static struct sockaddr_in6 mcast_addr = {
						.sin6_family = AF_INET6,
						.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
						.sin6_port = htons(COAP_PORT) };

					r = sendto(cfg->coap.sock, request.data, request.offset, 0, (struct sockaddr *) &mcast_addr, sizeof(mcast_addr));
				}
			//}
		}
	}


	k_free(data);

	return 0;
}


static int send_obs_coap_request(struct config *cfg, struct coap_reply *reply, void *user_data, const char * const path[])
{
	struct coap_packet request;
	const char * const *p;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON,
			     COAP_TOKEN_MAX_LEN, coap_next_token(),
			     COAP_METHOD_GET, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
	}
	else
	{
		r = coap_append_option_int(&request, COAP_OPTION_OBSERVE, 0);
		if (r < 0) {
			LOG_ERR("Failed to append Observe option");
		}
		else
		{
			for (p = path; p && *p; p++) {
				r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
								*p, strlen(*p));
				if (r < 0) {
					LOG_ERR("Unable add option to request");
					break;
				}
			}
			if (r == 0) {
			
				net_hexdump("Request", request.data, request.offset);

				reply->reply = obs_notification_cb;
				reply->user_data = user_data;

				static struct sockaddr_in6 mcast_addr = {
									.sin6_family = AF_INET6,
									.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
									.sin6_port = htons(COAP_PORT) };

				r = send(cfg->coap.sock, request.data, request.offset, 0, (struct sockaddr *) &mcast_addr, sizeof(mcast_addr));
			}
		}
	}
	
	k_free(data);

	return r;
}


static int obs_notification_cb(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	uint16_t id = coap_header_get_id(response);
	uint8_t type = coap_header_get_type(response);
	uint8_t *counter = (uint8_t *)reply->user_data;

	ARG_UNUSED(from);

	printk("\nCoAP OBS Notification\n");

	(*counter)++;

	if (type == COAP_TYPE_CON) {
		send_obs_reply_ack(id);
	}

	return 0;
}

int process_coap_reply(struct config *cfg)
{
	struct coap_packet reply;
	uint8_t *data;
	int rcvd;
	int ret;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	rcvd = recv(cfg->coap.sock, data, MAX_COAP_MSG_LEN, MSG_DONTWAIT);
	if (rcvd == 0) {
		ret = -EIO;
	}
	else
	{
		if (rcvd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				ret = 0;
			} else {
				ret = -errno;
			}
		}
		else
		{
			net_hexdump("Response", data, rcvd);

			ret = coap_packet_parse(&reply, data, rcvd, NULL, 0);
			if (ret < 0) {
				LOG_ERR("Invalid data received");
			}
		}
	}
	k_free(data);

	return ret;
}


static int init_coap_proto(struct config *cfg, struct sockaddr *addr,
			   socklen_t addrlen)
{
	int ret;

	k_work_init_delayable(&cfg->coap.recv, wait_reply);

	cfg->coap.sock = socket(addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);

	if (cfg->coap.sock < 0) {
		LOG_ERR("Failed to create UDP socket (%s): %d", cfg->proto,
			errno);
		return -errno;
	}

	/* Call connect so we can use send and recv. */
	ret = bind(cfg->coap.sock, addr, addrlen);
	if (ret < 0) {
		LOG_ERR("Cannot connect to UDP remote (%s): %d", cfg->proto,
			errno);
		ret = -errno;
	}

	return ret;
}


int start_coap(void)
{
	int ret = 0;
	struct sockaddr_in6 addr6;

	if (IS_ENABLED(CONFIG_NET_IPV6)) {
		
		// addr6.sin6_family = AF_INET6;
		// addr6.sin6_port = htons(COAP_PORT);
		// inet_pton(AF_INET6, CONFIG_NET_CONFIG_PEER_IPV6_ADDR,
		// 	  &addr6.sin6_addr);
		
		(void)memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(COAP_PORT);

		ret = init_coap_proto(&conf.ipv6, (struct sockaddr *)&addr6,
				      sizeof(addr6)); 
		if (ret < 0) {
			return ret;
		}
	}

	return ret;
}

int process_coap(void)
{
	int ret = 0;

	if (IS_ENABLED(CONFIG_NET_IPV6)) {
		ret = process_coap_request(&conf.ipv6);
		if (ret < 0) {
			return ret;
		}

		ret = process_coap_reply(&conf.ipv6);
		if (ret < 0) {
			return ret;
		}

	}

	return ret;
}

void stop_coap(void)
{
	if (IS_ENABLED(CONFIG_NET_IPV6)) {
		k_work_cancel_delayable(&conf.ipv6.coap.recv);
		k_work_cancel_delayable(&conf.ipv6.coap.transmit);

		if (conf.ipv6.coap.sock >= 0) {
			(void)close(conf.ipv6.coap.sock);
		}
	}
}
