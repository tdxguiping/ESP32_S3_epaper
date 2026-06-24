#pragma once

#include <stdint.h>

#include "tdx_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t User_Network_mode_app_init(const char *base_path);
int ServerNetworkSta_GetLastConnectResult(void);

#ifdef __cplusplus
}
#endif
