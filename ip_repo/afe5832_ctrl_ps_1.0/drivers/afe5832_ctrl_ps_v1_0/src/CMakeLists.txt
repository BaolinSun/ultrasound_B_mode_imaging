# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.15)
project(afe5832_ctrl_ps)

find_package(common)
collector_create(PROJECT_LIB_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}")
collector_create(PROJECT_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}")
include_directories(${CMAKE_BINARY_DIR}/include)
collect(PROJECT_LIB_HEADERS afe5832_ctrl_ps.h)
collect(PROJECT_LIB_SOURCES afe5832_ctrl_ps.c)
collect(PROJECT_LIB_SOURCES afe5832_ctrl_ps_selftest.c)
collector_list(_headers PROJECT_LIB_HEADERS)
collector_list(_sources PROJECT_LIB_SOURCES)
file(COPY ${_headers} DESTINATION ${CMAKE_BINARY_DIR}/include)
add_library(afe5832_ctrl_ps STATIC ${_sources})
set_target_properties(afe5832_ctrl_ps PROPERTIES LINKER_LANGUAGE C)

