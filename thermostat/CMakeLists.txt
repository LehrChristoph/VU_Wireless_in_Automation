# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermostat)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/hvac.c)
target_sources(app PRIVATE src/coap.c)
target_sources(app PRIVATE src/display.c)
include(${ZEPHYR_BASE}/samples/net/common/common.cmake)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/net/ip)
