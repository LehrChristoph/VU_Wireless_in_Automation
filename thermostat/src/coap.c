/* coap.c - UDP specific code for echo client */

/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coap, LOG_LEVEL_INF);

#include <zephyr/sys/byteorder.h>
#include <zephyr/zephyr.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

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

#define UDP_SLEEP K_MSEC(150)
#define UDP_WAIT K_SECONDS(10)
#define NUM_REPLIES 10

static const char * const echo_path[] = { "echo", NULL };
static const char * const temperature_path[] = {"sensors", "temperature", NULL };
static const char * const humidity_path[] = {"sensors",  "humidity", NULL };
static const char * const air_quality_path[] = {"sensors",  "air_quality", NULL };
static const char * const presence_path[] = {"sensors",  "presence", NULL };

// currently not used
// static const char * const air_pressure_path[] = {"sensors",  "air_pressure", NULL };
// static const char * const luminance_path[] = {"sensors",  "luminance", NULL };

static bool echo_received;
static struct coap_reply replies[NUM_REPLIES];
static int reply_acks[NUM_REPLIES];
static int reply_acks_wr_ptr;

static void wait_reply(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	/* This means that we did not receive response in time. */
	struct config *data = CONTAINER_OF(dwork, struct config, coap.recv);

	LOG_ERR("UDP %s: Data packet not received", data->proto);

	/* Send a new packet at this point */
	//send_udp_data(data);
}

//----------------------------------------------------------------
// Reply Callbacks
//----------------------------------------------------------------

static void send_obs_reply_ack(struct coap_packet *reply)
{
	struct coap_packet request;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return;
	}

	r = coap_ack_init(&request, reply, data, MAX_COAP_MSG_LEN, COAP_CODE_EMPTY);
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
		goto end;
	}

	net_hexdump("ACK", request.data, request.offset);

	static struct sockaddr_in6 mcast_addr = {
						.sin6_family = AF_INET6,
						.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
						.sin6_port = htons(COAP_PORT) };

	r = sendto(conf.ipv6.coap.sock, request.data, request.offset, 0, (struct sockaddr *) &mcast_addr, sizeof(mcast_addr));

	if (r < 0) {
		LOG_ERR("Failed to send CoAP ACK: %i", -r);
	}
end:
	k_free(data);
}

static int echo_request_cb(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	
	echo_received = true;
	coap_reply_clear(reply);
	
	return 0;
}

static int notification_cb_temp(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	const uint8_t *payload;
	uint16_t payload_len;

	payload = coap_packet_get_payload(response, &payload_len);
	if (payload == 0) {
		LOG_ERR("NO PAYLOAD RECEIVED");
	}
	else
	{
		double result = atof(payload);
		LOG_DBG("Tempereature %lf", result);
		hvac_update_temperatur(result);
		display_update_temperatur(result);
	}

	return 0;
}

static int notification_cb_humidity(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	const uint8_t *payload;
	uint16_t payload_len;

	payload = coap_packet_get_payload(response, &payload_len);
	if (payload == 0) {
		LOG_ERR("NO PAYLOAD RECEIVED");
	}
	else
	{
		double result = atof(payload);
		LOG_DBG("Humidity %lf", result);
		hvac_update_humidity(result);
		display_update_humidity(result);
	}

	return 0;
}

static int notification_cb_air_quality(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	const uint8_t *payload;
	uint16_t payload_len;

	payload = coap_packet_get_payload(response, &payload_len);
	if (payload == 0) {
		LOG_ERR("NO PAYLOAD RECEIVED");
	}
	else
	{
		double result = atof(payload);
		LOG_DBG("Air Quality %lf", result);
		hvac_update_air_quality(result);
		display_update_air_quality(result);
	}
	
	return 0;
}

static int notification_cb_presence(const struct coap_packet *response,
			       struct coap_reply *reply,
			       const struct sockaddr *from)
{
	const uint8_t *payload;
	uint16_t payload_len;

	payload = coap_packet_get_payload(response, &payload_len);
	if (payload == 0) {
		LOG_ERR("NO PAYLOAD RECEIVED");
	}
	else
	{
		int result = payload[0] == '0' ? 0 :1;
		LOG_DBG("Presence %i", result);
		hvac_update_pressence(result);
	}
	
	return 0;
}

//----------------------------------------------------------------
// CoAP Send and Receive Functions
//----------------------------------------------------------------,

static int coap_send_echo_request(struct config *cfg)
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
			     COAP_METHOD_PUT, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
	}
	else
	{
		for (p = echo_path; p && *p; p++) {
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
				{
					net_hexdump("Request", request.data, request.offset);


					struct coap_reply *reply =coap_reply_next_unused((struct coap_reply *)&replies, sizeof(replies));
					coap_reply_init(reply, &request);
					reply->reply = echo_request_cb;

					static struct sockaddr_in6 mcast_addr = {
						.sin6_family = AF_INET6,
						.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
						.sin6_port = htons(COAP_PORT) };

					r = sendto(cfg->coap.sock, request.data, request.offset, 0, (struct sockaddr *) &mcast_addr, sizeof(mcast_addr));
				}
			}
		}
	}

	k_free(data);

	return 0;
}


static int coap_send_observer_request(struct config *cfg, const char * const path[], coap_reply_t reply_cb)
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
			
				if(reply_acks_wr_ptr < NUM_REPLIES)
				{
					net_hexdump("Request", request.data, request.offset);

					struct coap_reply *reply =coap_reply_next_unused((struct coap_reply *)&replies, sizeof(replies));
					coap_reply_init(reply, &request);
					reply->reply = reply_cb;
					reply_acks[reply_acks_wr_ptr] = -1;
					reply->user_data = &reply_acks[reply_acks_wr_ptr];
					reply_acks_wr_ptr++;

					static struct sockaddr_in6 mcast_addr = {
										.sin6_family = AF_INET6,
										.sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
										.sin6_port = htons(COAP_PORT) };

					r = sendto(cfg->coap.sock, request.data, request.offset, 0, (struct sockaddr *) &mcast_addr, sizeof(mcast_addr));
				}
			}
		}
	}
	
	k_free(data);

	return r;
}

int process_coap_reply(struct config *cfg, int flags)
{
	struct coap_packet reply;
	uint8_t *data;
	int rcvd;
	int ret;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}
	LOG_INF("Waiting for Reception");
	rcvd = recv(cfg->coap.sock, data, MAX_COAP_MSG_LEN, flags);
	if (rcvd == 0) {
		ret = -EIO;
	}
	else
	{
		if (rcvd < 0) {
			LOG_ERR("Error in Reception: %i", -rcvd);
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
			}else{
				(void) coap_response_received(&reply, NULL, (struct coap_reply *) &replies, sizeof(replies));
				uint8_t type = coap_header_get_type(&reply);

				if( type == COAP_TYPE_CON )
				{
					send_obs_reply_ack(&reply);
					
				}
			}
		}
	}
	k_free(data);

	return ret;
}


//----------------------------------------------------------------
// Setup, Teardown and runtime functions
//----------------------------------------------------------------

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

int coap_find_server(void)
{
	int ret = 0;
	echo_received = false;
	while(ret == 0 && echo_received == false)
	{
		ret = coap_send_echo_request(&conf.ipv6);
		if (ret < 0) {
			return ret;
		}

		k_sleep(K_MSEC(5000));

		ret = process_coap_reply(&conf.ipv6, MSG_DONTWAIT);
		if (ret < 0) {
			return ret;
		}

	}
	return ret;
}

int coap_register_observers(void)
{
	int ret = 0;

	ret = coap_send_observer_request(&conf.ipv6, temperature_path, notification_cb_temp);
	if (ret < 0) {
		return ret;
	}

	ret = process_coap_reply(&conf.ipv6, 0);
	if (ret < 0) {
		LOG_ERR("process_coap_replD");
		return ret;
	}

	ret = coap_send_observer_request(&conf.ipv6, humidity_path, notification_cb_humidity);
	if (ret < 0) {
		return ret;
	}

	ret = process_coap_reply(&conf.ipv6, 0);
	if (ret < 0) {
		LOG_ERR("process_coap_replD");
		return ret;
	}

		ret = coap_send_observer_request(&conf.ipv6, air_quality_path, notification_cb_air_quality);
	if (ret < 0) {
		return ret;
	}

	ret = process_coap_reply(&conf.ipv6, 0);
	if (ret < 0) {
		LOG_ERR("process_coap_replD");
		return ret;
	}

	ret = coap_send_observer_request(&conf.ipv6, presence_path, notification_cb_presence);
	if (ret < 0) {
		return ret;
	}

	ret = process_coap_reply(&conf.ipv6, 0);
	if (ret < 0) {
		LOG_ERR("process_coap_replD");
		return ret;
	}

	return ret;
}

int coap_process(void)
{
	int ret = 0;

	ret = process_coap_reply(&conf.ipv6, 0);
	if (ret < 0) {
		LOG_ERR("process_coap_replD");
		return ret;
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
