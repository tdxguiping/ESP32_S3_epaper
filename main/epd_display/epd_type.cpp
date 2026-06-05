#include "epd_type.h"

#include "display_bsp.h"
#include "epd_type_1024_600.h"
#include "epd_type_1360_480_1085.h"
#include "epd_type_1360_480_1085_3color.h"
#include "epd_type_1600_1200_133.h"
#include "epd_type_1600_1200_79.h"
#include "epd_type_800_480.h"
#include "epd_type_800_480_4s_75.h"
#include "esp_log.h"

static const char *TAG = "epd_type";

uint8_t EPD_type = EPD_TYPE_1360_480_1085_3COLOR;

static const epd_type_config_t s_epd_types[] = {
    {EPD_TYPE_800_480, 800, 480, 192000, "EPD_800_480"},//  3 色 
    {EPD_TYPE_1024_600, 1024, 600, 307200, "EPD_1024_600"}, //  6 色    307200 bytes
    {EPD_TYPE_1600_1200_79, 1600, 1200, 960000, "EPD_1600_1200_79"}, //  6 色    
    {EPD_TYPE_1600_1200_133, 1600, 1200, 960000, "EPD_1600_1200_133"},//  6 色 
    {EPD_TYPE_1360_480_1085, 1360, 480, 81600, "EPD_1360_480_1085"},//  4 色
    {EPD_TYPE_800_480_4S_75, 800, 480, 96000, "EPD_800_480_4S_75"},//  4 色 
    {EPD_TYPE_1360_480_1085_3COLOR, 1360, 480, 163200, "EPD_1360_480_1085_3COLOR"},
};

const epd_type_config_t *EpdType_GetConfig(uint8_t type)
{
    for (size_t i = 0; i < sizeof(s_epd_types) / sizeof(s_epd_types[0]); ++i) {
        if (s_epd_types[i].type == type) {
            return &s_epd_types[i];
        }
    }
    return nullptr;
}

const epd_type_config_t *EpdType_GetCurrentConfig(void)
{
    return EpdType_GetConfig(EPD_type);
}

void EpdType_Set(uint8_t type)
{
    const epd_type_config_t *config = EpdType_GetConfig(type);
    if (config == nullptr) {
        ESP_LOGE(TAG, "invalid EPD type=%u", (unsigned int)type);
        return;
    }
    EPD_type = type;
    ESP_LOGI(TAG, "EPD type=%u name=%s size=%u",
             (unsigned int)config->type,
             config->name,
             (unsigned int)config->display_size);
}

void EpdType_DisplayCurrent(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    epd.Set_EPD_type(EPD_type);

    switch (EPD_type) {
    case EPD_TYPE_800_480:
        EpdType800480_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_1024_600:
        EpdType1024600_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_1600_1200_79:
        EpdType16001200_79_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_1600_1200_133:
        EpdType16001200_133_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_1360_480_1085:
        EpdType1360480_1085_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_800_480_4S_75:
        EpdType800480_4S_75_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_1360_480_1085_3COLOR:
        EpdType1360480_1085_3Color_Display(epd, display_buf, display_size);
        break;
    default:
        ESP_LOGE(TAG, "display rejected invalid EPD type=%u", (unsigned int)EPD_type);
        break;
    }
}

void EpdType_DispatchSleep(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_Sleep(); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_Sleep(); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_Sleep(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_Sleep(); break;
    case EPD_TYPE_1360_480_1085: epd.EpdType1360480_1085_Sleep(); break;
    case EPD_TYPE_800_480_4S_75: epd.EpdType800480_4S_75_Sleep(); break;
    case EPD_TYPE_1360_480_1085_3COLOR: epd.EpdType1360480_1085_3Color_Sleep(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in sleep", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchInit(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_Init(); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_Init(); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_Init(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_Init(); break;
    case EPD_TYPE_1360_480_1085: epd.EpdType1360480_1085_Init(); break;
    case EPD_TYPE_800_480_4S_75: epd.EpdType800480_4S_75_Init(); break;
    case EPD_TYPE_1360_480_1085_3COLOR: epd.EpdType1360480_1085_3Color_Init(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in init", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchDisplay(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_Display(); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_Display(); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_Display(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_Display(); break;
    case EPD_TYPE_1360_480_1085: epd.EpdType1360480_1085_Display(); break;
    case EPD_TYPE_800_480_4S_75: epd.EpdType800480_4S_75_Display(); break;
    case EPD_TYPE_1360_480_1085_3COLOR: epd.EpdType1360480_1085_3Color_Display(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in display", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchNT61522Init(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_NT61522_Init(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_NT61522_Init(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in NT61522 init", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchNT61522Display(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_NT61522_Display(); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_NT61522_Display(); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_NT61522_Display(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_NT61522_Display(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in NT61522 display", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchNT61522InitDisplay(ePaperPort &epd)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_NT61522_InitDisplay(); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_NT61522_InitDisplay(); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_NT61522_InitDisplay(); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_NT61522_InitDisplay(); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in NT61522 init display", (unsigned int)EPD_type); break;
    }
}

void EpdType_DispatchNT61522DisplayNet(ePaperPort &epd, const uint8_t *image_data, size_t image_size)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480: epd.EpdType800480_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_1024_600: epd.EpdType1024600_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_1600_1200_79: epd.EpdType16001200_79_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_1600_1200_133: epd.EpdType16001200_133_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_1360_480_1085: epd.EpdType1360480_1085_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_800_480_4S_75: epd.EpdType800480_4S_75_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_1360_480_1085_3COLOR: epd.EpdType1360480_1085_3Color_DisplayNet(image_data, image_size); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in NT61522 display net", (unsigned int)EPD_type); break;
    }
}
