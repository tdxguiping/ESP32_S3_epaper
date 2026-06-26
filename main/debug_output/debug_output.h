#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t UserDebugOutput_Init(void);
void UserDebugOutput_Printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
