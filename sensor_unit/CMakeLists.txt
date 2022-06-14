# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sockets_echo_client)

target_sources(                     app PRIVATE src/echo-client.c)
target_sources(                     app PRIVATE src/adc.c)
target_sources(                     app PRIVATE src/pir.c)
target_sources(                     app PRIVATE src/bme680.c)
target_sources_ifdef(CONFIG_NET_UDP app PRIVATE src/udp.c)

include(${ZEPHYR_BASE}/samples/net/common/common.cmake)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)
