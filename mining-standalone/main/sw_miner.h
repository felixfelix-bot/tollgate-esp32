#ifndef SW_MINER_H
#define SW_MINER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t sw_miner_start(void);
void sw_miner_stop(void);
bool sw_miner_is_running(void);
double sw_miner_get_hashrate(void);

#endif
