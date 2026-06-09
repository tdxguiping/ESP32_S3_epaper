#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_console_worker_job_fn_t)(void *ctx);

esp_err_t UsbConsoleWorker_Init(void);
esp_err_t UsbConsoleWorker_SubmitJob(const char *name, usb_console_worker_job_fn_t job, void *ctx);
esp_err_t UsbConsoleWorker_SubmitWifiConnect(void);

#ifdef __cplusplus
}
#endif
