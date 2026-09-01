#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACTORY_TEST_SSID_MAX_LEN 32U
#define FACTORY_TEST_KEY_MIN_LEN   8U
#define FACTORY_TEST_KEY_MAX_LEN  63U

// Reserve one validated FACTORY_DATA request before its UART ACK is sent.
esp_err_t Ch583FactoryTest_Admit(uint16_t rx_seq,
                                const char *arg,
                                size_t arg_len,
                                uint32_t *admission);
// Queue an admitted request only after the UART ACK has been sent.
esp_err_t Ch583FactoryTest_Commit(uint32_t admission);
// Roll back an admission when its UART ACK cannot be sent.
void Ch583FactoryTest_CancelAdmission(uint32_t admission);
// Let Factory Reset invalidate current and pending factory-test work.
void Ch583FactoryTest_CancelForFactoryReset(void);
// Publish the factory-test gate used by network and USB ping.
bool Ch583FactoryTest_IsBusy(void);
// Skip the normal startup connect after factory credentials own this boot.
bool Ch583FactoryTest_ManagesWifiThisBoot(void);

#ifdef __cplusplus
}
#endif
