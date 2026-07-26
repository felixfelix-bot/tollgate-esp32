/*
 * mining_config.h — Standalone mining configuration API.
 *
 * Loads stratum pool + WiFi settings from menuconfig (Kconfig.projbuild)
 * into the tollgate_config_t stub so stratum_client.c can use them.
 */
#ifndef MINING_CONFIG_H
#define MINING_CONFIG_H

#include "config.h"
#include <stdbool.h>

/* Populate the static config from Kconfig values.  Safe to call once at boot. */
void mining_config_init(void);

/* Override the WiFi credentials at runtime (optional). */
void mining_config_set_wifi(const char *ssid, const char *pass);

/* WiFi getters for mining_main.c */
const char *mining_config_get_wifi_ssid(void);
const char *mining_config_get_wifi_pass(void);

#endif /* MINING_CONFIG_H */
