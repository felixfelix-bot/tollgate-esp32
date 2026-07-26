# SPDX-License-Identifier: MIT
# Standalone ESP-IDF mining project extracted from esp32-tollgate
# Phase 1: sw_miner + stratum_client only
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mining-standalone)
