#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    EPD_TYPE_800_480 = 1,
    EPD_TYPE_1024_600 = 2,
    EPD_TYPE_1600_1200_79 = 3,
    EPD_TYPE_1600_1200_133 = 4,
    EPD_TYPE_1360_480_1085 = 5,
    EPD_TYPE_800_480_4S_75 = 6,
    EPD_TYPE_1360_480_1085_3COLOR = 7,
    EPD_TYPE_800_480_4S_75_2 = 8,
    EPD_TYPE_800_480_4S_75_3 = 9,
    EPD_TYPE_1600_1200_133_DKE = 10,
} epd_type_id_t;

typedef enum {
    BWR_3_Color = 3,
    BWRY_4_Color = 4,
    BWYRBG_6_Color = 6,
} epd_color_type_t;

typedef struct {
    uint8_t type;
    uint16_t width;
    uint16_t height;
    size_t display_size;
    const char *name;
    uint8_t color_type;
} epd_type_config_t;

#ifdef __cplusplus
class ePaperPort;
extern "C" {
#endif

extern uint8_t EPD_type;

const epd_type_config_t *EpdType_GetConfig(uint8_t type);
const epd_type_config_t *EpdType_GetCurrentConfig(void);
size_t EpdType_GetCount(void);
const epd_type_config_t *EpdType_GetConfigByIndex(size_t index);
esp_err_t EpdType_LoadSavedOrDefault(void);
esp_err_t EpdType_SetAndSave(uint8_t type, bool *changed);
void EpdType_Set(uint8_t type);

#ifdef __cplusplus
}

esp_err_t EpdType_DisplayCurrent(ePaperPort &epd, const uint8_t *display_buf, size_t display_size);
void EpdType_ReportDisplayFailure(esp_err_t error);
esp_err_t EpdType_GetDisplayResult(void);
void EpdType_DispatchSleep(ePaperPort &epd);
void EpdType_DispatchInit(ePaperPort &epd);
void EpdType_DispatchDisplay(ePaperPort &epd);
void EpdType_DispatchNT61522Init(ePaperPort &epd);
void EpdType_DispatchNT61522Display(ePaperPort &epd);
void EpdType_DispatchNT61522InitDisplay(ePaperPort &epd);
void EpdType_DispatchNT61522DisplayNet(ePaperPort &epd, const uint8_t *image_data, size_t image_size);
#endif
