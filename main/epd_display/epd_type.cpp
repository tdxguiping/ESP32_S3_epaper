#include "epd_type.h"

#include "display_bsp.h"
#include "epd_type_1024_600.h"
#include "epd_type_1360_480_1085.h"
#include "epd_type_1360_480_1085_3color.h"
#include "epd_type_1600_1200_133.h"
#include "epd_type_1600_1200_79.h"
#include "epd_type_800_480.h"
#include "epd_type_800_480_4s_75.h"
#include "epd_type_800_480_4s_75_DKE.h"
#include "epd_type_800_480_4s_75_mofang.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "epd_type";

uint8_t EPD_type = EPD_TYPE_800_480_4S_75_3; // 默认使用 1600x1200 133ms 的屏幕
static bool s_epd_type_loaded = false;

static const epd_type_config_t s_epd_types[] = {
    {EPD_TYPE_800_480, 800, 480, 192000, "EPD_800_480_XingTai", BWR_3_Color},//  3 色
    {EPD_TYPE_1024_600, 1024, 600, 307200, "EPD_1024_600_XingTai", BWYRBG_6_Color}, //  6 色    307200 bytes
    {EPD_TYPE_1600_1200_79, 1600, 1200, 960000, "EPD_1600_1200_79_XingTai", BWYRBG_6_Color}, //  6 色
    {EPD_TYPE_1600_1200_133, 1600, 1200, 960000, "EPD_1600_1200_133_XingTai", BWYRBG_6_Color},//  6 色
    {EPD_TYPE_1360_480_1085, 1360, 480, 81600, "EPD_1360_480_1085_XingTai", BWRY_4_Color},//  4 色
    {EPD_TYPE_800_480_4S_75, 800, 480, 96000, "EPD_800_480_4S_75_XingTai", BWRY_4_Color},//  4 色 兴泰<使用6色芯片>
    {EPD_TYPE_1360_480_1085_3COLOR, 1360, 480, 163200, "EPD_1360_480_1085_3COLOR_YSGD", BWR_3_Color},//  3 色 亚寺光电
    {EPD_TYPE_800_480_4S_75_2, 800, 480, 96000, "EPD_800_480_4S_75_DKE", BWRY_4_Color},//  4 色 DKE
    {EPD_TYPE_800_480_4S_75_3, 800, 480, 96000, "EPD_800_480_4S_75_mofang", BWRY_4_Color},//  4 色 mofang 墨方
};

static uint8_t EpdType_GetHardwareVersion(uint8_t type)
{
    return (type == EPD_TYPE_800_480_4S_75 ||
            type == EPD_TYPE_800_480_4S_75_2 ||
            type == EPD_TYPE_800_480_4S_75_3) ? 2U : 1U;
}

static void EpdType_UpdateHardwareVersion(uint8_t type)
{
    uint8_t next_version = EpdType_GetHardwareVersion(type);
    if (Hardware_Version_ != next_version) {
        Hardware_Version_ = next_version;
        ESP_LOGI(TAG, "EPD hardware version=%u type=%u",
                 (unsigned int)Hardware_Version_,
                 (unsigned int)type);
    }
}

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

size_t EpdType_GetCount(void)
{
    return sizeof(s_epd_types) / sizeof(s_epd_types[0]);
}

const epd_type_config_t *EpdType_GetConfigByIndex(size_t index)
{
    if (index >= EpdType_GetCount()) {
        return nullptr;
    }
    return &s_epd_types[index];
}

void EpdType_Set(uint8_t type)
{
    const epd_type_config_t *config = EpdType_GetConfig(type);
    if (config == nullptr) {
        ESP_LOGE(TAG, "invalid EPD type=%u", (unsigned int)type);
        return;
    }
    EPD_type = type;
    EpdType_UpdateHardwareVersion(type);
    ESP_LOGI(TAG, "EPD type=%u name=%s size=%u",
             (unsigned int)config->type,
             config->name,
             (unsigned int)config->display_size);
}

esp_err_t EpdType_LoadSavedOrDefault(void)
{
    if (s_epd_type_loaded) {
        return ESP_OK;
    }

    uint8_t saved_type = USER_EPD_TYPE_DEFAULT;
    esp_err_t ret = app_nvs_read_u8(USER_EPD_TYPE_NVS_KEY, &saved_type, USER_EPD_TYPE_DEFAULT);
    const epd_type_config_t *config = EpdType_GetConfig(saved_type);

    if (config == nullptr) {
        ESP_LOGW(TAG, "saved EPD type invalid value=%u, fallback=%u",
                 (unsigned int)saved_type,
                 (unsigned int)USER_EPD_TYPE_DEFAULT);
        saved_type = USER_EPD_TYPE_DEFAULT;
        config = EpdType_GetConfig(saved_type);
        if (config == nullptr) {
            ESP_LOGE(TAG, "default EPD type invalid value=%u", (unsigned int)saved_type);
            return ESP_ERR_INVALID_STATE;
        }
        ret = app_nvs_write_u8(USER_EPD_TYPE_NVS_KEY, saved_type);
    }

    EpdType_Set(saved_type);
    s_epd_type_loaded = true;
    ESP_LOGI(TAG, "EPD type loaded value=%u name=%s ret=%s",
             (unsigned int)saved_type,
             config->name,
             esp_err_to_name(ret));
    return ret;
}

esp_err_t EpdType_SetAndSave(uint8_t type, bool *changed)
{
    const epd_type_config_t *config = EpdType_GetConfig(type);
    if (changed != nullptr) {
        *changed = false;
    }
    if (config == nullptr) {
        ESP_LOGW(TAG, "set/save rejected invalid EPD type=%u", (unsigned int)type);
        return ESP_ERR_INVALID_ARG;
    }

    if (EPD_type == type) {
        ESP_LOGI(TAG, "set/save skipped unchanged EPD type=%u name=%s",
                 (unsigned int)type,
                 config->name);
        return ESP_OK;
    }

    EpdType_Set(type);
    if (changed != nullptr) {
        *changed = true;
    }
    return app_nvs_write_u8(USER_EPD_TYPE_NVS_KEY, type);
}

void EpdType_DisplayCurrent(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    EpdType_UpdateHardwareVersion(EPD_type);
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
    case EPD_TYPE_800_480_4S_75_2:
        EpdType800480_4S_75_DKE_Display(epd, display_buf, display_size);
        break;
    case EPD_TYPE_800_480_4S_75_3:
        EpdType800480_4S_75_Mofang_Display(epd, display_buf, display_size);
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
    case EPD_TYPE_800_480_4S_75_2: epd.EpdType800480_4S_75_DKE_Sleep(); break;
    case EPD_TYPE_800_480_4S_75_3: epd.EpdType800480_4S_75_Mofang_Sleep(); break;
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
    case EPD_TYPE_800_480_4S_75_2: epd.EpdType800480_4S_75_DKE_Init(); break;
    case EPD_TYPE_800_480_4S_75_3: epd.EpdType800480_4S_75_Mofang_Init(); break;
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
    case EPD_TYPE_800_480_4S_75_2: epd.EpdType800480_4S_75_DKE_Display(); break;
    case EPD_TYPE_800_480_4S_75_3: epd.EpdType800480_4S_75_Mofang_Display(); break;
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
    case EPD_TYPE_800_480_4S_75_2: epd.EpdType800480_4S_75_DKE_NT61522_DisplayNet(image_data, image_size); break;
    case EPD_TYPE_800_480_4S_75_3: epd.EpdType800480_4S_75_Mofang_NT61522_DisplayNet(image_data, image_size); break;
    default: ESP_LOGE(TAG, "unsupported EPD type=%u in NT61522 display net", (unsigned int)EPD_type); break;
    }
}
