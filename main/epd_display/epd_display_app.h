#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "epd_type.h"

typedef struct {
    bool valid;
} epd_display_reservation_t;

#ifdef __cplusplus
#include "display_bsp.h"
extern ePaperPort ePaperDisplay;
extern "C" {
#endif

esp_err_t ServerNetworkStaEpdDisplay_Init(void);
esp_err_t ServerNetworkStaEpdDisplay_SetPower(bool power_on);
esp_err_t ServerNetworkStaEpdDisplay_PrepareRailIoForPowerTestOff(void);
esp_err_t ServerNetworkStaEpdDisplay_RestoreRailIoAfterPowerTestOn(void);
esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size);
esp_err_t ServerNetworkStaEpdDisplay_QueueToScreen(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one);
esp_err_t ServerNetworkStaEpdDisplay_QueueToScreenAndWait(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one);
// Atomically reserve an idle EPD for a caller that must not queue behind other work.
esp_err_t ServerNetworkStaEpdDisplay_TryReserveIdle(epd_display_reservation_t *reservation);
esp_err_t ServerNetworkStaEpdDisplay_QueueReservedToScreenAndWait(
    epd_display_reservation_t *reservation,
    const uint8_t *display_buf,
    size_t display_size,
    uint8_t epd_which_one);
void ServerNetworkStaEpdDisplay_ReleaseReservation(epd_display_reservation_t *reservation);
bool ServerNetworkStaEpdDisplay_IsBusy(void);
esp_err_t test_epd_display_and_wait(void);
void test_epd_display_EPD_1600_1200_79(void);
void test_epd_display_EPD_1600_1200_133(void);
void test_epd_display_EPD_1600_1200_133_DKE(void);
void test_epd_display_EPD_EPD_1024_600(void);
void test_epd_display_EPD_800_480(void);
void test_epd_display_EPD_1360_480_1085(void);
void test_epd_display_EPD_800_480_4S_75_2(void);
void test_epd_display_EPD_800_480_4S_75_3(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_horizontal(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_vertical(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_const(void);
void test_epd_display(void);

#ifdef __cplusplus
}
#endif
