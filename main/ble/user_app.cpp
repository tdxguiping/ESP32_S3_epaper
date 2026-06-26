#include "user_app.h"
#include "debug_output.h"
#include "tdx_cfg.h"

#include <stdio.h>
#include <string.h>

#include "ch583_wifi_uart_protocol.h"

#if USER_BLE_ENABLE

#include <inttypes.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "ble_data_handler.h"

enum {
    SPP_IDX_SVC,

    TDX_IDX_01_CHAR,
    TDX_IDX_01_VAL,
    TDX_IDX_01_CFG,

    TDX_IDX_14_CHAR,
    TDX_IDX_14_VAL,
    TDX_IDX_14_CFG,

    SPP_IDX_NB,
};

enum {
    TDXINFO_GET_DATA = 3,
    TDXINFO_SWITCH_MODE = 31,
};

static const uint8_t spp_service_uuid[TDX_BLE_ATT_UUID_SIZE] = {
    0x41, 0x59, 0x8b, 0x7b, 0x99, 0x74, 0x07, 0xa3,
    0xc1, 0x49, 0x13, 0x44, 0x00, 0xff, 0x12, 0x7b,
};
static const uint8_t tdxInfoGetInfoUUID[TDX_BLE_ATT_UUID_SIZE] = {
    0x41, 0x59, 0x8b, 0x7b, 0x99, 0x74, 0x07, 0xa3,
    0xc1, 0x49, 0x13, 0x44, 0x01, 0xff, 0x12, 0x7b,
};
static const uint8_t tdxInfoSwitchModeUUID[TDX_BLE_ATT_UUID_SIZE] = {
    0x41, 0x59, 0x8b, 0x7b, 0x99, 0x74, 0x07, 0xa3,
    0xc1, 0x49, 0x13, 0x44, 0x14, 0xff, 0x12, 0x7b,
};

static uint8_t scan_rsp_data[24] = {
    23, 0x09,
    0x54, 'd', 0x37, 0x30, 0x31, 0x39, 0x38, 0x38,
    0x43, 0x46, 0x37, 0x42, 0x46, 0x34, 0x4F, 0x34,
    0x34, 0x30, 0x40, 0x41, 0x45, 0x30,
};

static uint8_t spp_adv_data[21] = {
    0x02, 0x01, 0x06,
    17, 0x06,
    0x41, 0x59, 0x8b, 0x7b, 0x99, 0x74, 0x07, 0xa3,
    0xc1, 0x49, 0x13, 0x44, 0x00, 0xff, 0x12, 0x6b,
};

static uint16_t spp_conn_id = 0xffff;
static esp_gatt_if_t spp_gatts_if = ESP_GATT_IF_NONE;
static uint16_t spp_handle_table[SPP_IDX_NB];
static bool tdx01_notify_enabled = false;
static bool switch_mode_notify_enabled = false;
bool is_connected = false;

static esp_ble_adv_params_t spp_adv_params = {
    .adv_int_min = 0x100,
    .adv_int_max = 0x200,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst spp_profile_tab[TDX_BLE_PROFILE_NUM] = {
    [TDX_BLE_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write_notify =
    ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static const uint8_t tdx_idx_01_val[20] = {0x00};
static const uint8_t tdx_idx_14_val[20] = {0x00};
static uint8_t tdx_idx_01_ccc[2] = {0x00, 0x00};
static uint8_t tdx_idx_14_ccc[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t spp_gatt_db[SPP_IDX_NB] = {
    [SPP_IDX_SVC] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
          sizeof(spp_service_uuid), sizeof(spp_service_uuid), (uint8_t *)spp_service_uuid}},

    [TDX_IDX_01_CHAR] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
          TDX_BLE_CHAR_DECLARATION_SIZE, TDX_BLE_CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_notify}},
    [TDX_IDX_01_VAL] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_128, (uint8_t *)tdxInfoGetInfoUUID, ESP_GATT_PERM_READ,
          TDX_BLE_DATA_MAX_LEN, sizeof(tdx_idx_01_val), (uint8_t *)tdx_idx_01_val}},
    [TDX_IDX_01_CFG] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
          ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t),
          sizeof(tdx_idx_01_ccc), tdx_idx_01_ccc}},

    [TDX_IDX_14_CHAR] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
          TDX_BLE_CHAR_DECLARATION_SIZE, TDX_BLE_CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write_notify}},
    [TDX_IDX_14_VAL] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_128, (uint8_t *)tdxInfoSwitchModeUUID,
          ESP_GATT_PERM_WRITE, TDX_BLE_DATA_MAX_LEN, sizeof(tdx_idx_14_val),
          (uint8_t *)tdx_idx_14_val}},
    [TDX_IDX_14_CFG] =
        {{ESP_GATT_AUTO_RSP},
         {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
          ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t),
          sizeof(tdx_idx_14_ccc), tdx_idx_14_ccc}},
};

static char hex_to_char(uint8_t hex)
{
    return hex < 10 ? (char)('0' + hex) : (char)('A' + hex - 10);
}

static esp_err_t read_ble_mac(uint8_t mac[6])
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TDX_BLE_LOG_TAG, "read BLE MAC failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

extern "C" void get_ble_mac_no_colon(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';
#if USER_BLE_ENABLE
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        snprintf(out, out_size, "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
#else
    const char *mac = ch583_wifi_uart_get_ble_mac();
    if (mac != NULL) {
        snprintf(out, out_size, "%s", mac);
    }
#endif
}


static void init_broadcast_data(void)
{
    uint8_t mac[6] = {};
    if (read_ble_mac(mac) != ESP_OK) {
        return;
    }

    ESP_LOGI(TDX_BLE_LOG_TAG, "Bluetooth MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    for (int i = 0; i < 6; ++i) {
        scan_rsp_data[4 + i * 2] = hex_to_char((mac[i] & 0xf0) >> 4);
        scan_rsp_data[5 + i * 2] = hex_to_char(mac[i] & 0x0f);
    }
    scan_rsp_data[16] = '0';
    scan_rsp_data[17] = '4';
    scan_rsp_data[18] = '4';
    scan_rsp_data[19] = '0';
}

static uint8_t find_attr_index(uint16_t handle)
{
    for (uint8_t i = 0; i < SPP_IDX_NB; i++) {
        if (handle == spp_handle_table[i]) {
            return i;
        }
    }
    return 0xff;
}

static const char *gatts_evt_name(esp_gatts_cb_event_t event)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        return "REG";
    case ESP_GATTS_READ_EVT:
        return "READ";
    case ESP_GATTS_WRITE_EVT:
        return "WRITE";
    case ESP_GATTS_MTU_EVT:
        return "MTU";
    case ESP_GATTS_CONNECT_EVT:
        return "CONNECT";
    case ESP_GATTS_DISCONNECT_EVT:
        return "DISCONNECT";
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        return "CREAT_ATTR_TAB";
    default:
        return "OTHER";
    }
}

static void print_ble_received_data(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        ESP_LOGI(TDX_BLE_LOG_TAG, "ble rx empty");
        return;
    }

    UserDebugOutput_Printf("BLE RX len=%u: ", (unsigned int)len);
    for (uint16_t i = 0; i < len; i++) {
        UserDebugOutput_Printf("%02X ", data[i]);
    }
    UserDebugOutput_Printf("\r\n");
    UserDebugOutput_Printf("BLE RX text: %.*s\r\n", len, (const char *)data);
}

static void dump_handle_table(void)
{
    // Print the GATT handle table once so phone-side UUID problems can be matched to ESP-IDF handles.
    // 灞炴€ц〃鍒涘缓鍚庢墦鍗颁竴娆?GATT handle锛屾柟渚夸互鍚庢妸鎵嬫満绔?UUID 闂瀵瑰簲鍒?ESP-IDF handle銆?    for (int i = 0; i < SPP_IDX_NB; i++) {
        ESP_LOGI(TDX_BLE_LOG_TAG, "handle_table[%02d]=0x%04X", i, spp_handle_table[i]);
    }
}

static void handle_cccd_write(uint8_t idx, const esp_ble_gatts_cb_param_t *p_data)
{
    uint16_t cccd = 0;
    if (p_data->write.len >= 2) {
        cccd = (uint16_t)p_data->write.value[0] | ((uint16_t)p_data->write.value[1] << 8);
    }

    bool enabled = (cccd & 0x0001U) != 0;
    if (idx == TDX_IDX_01_CFG) {
        tdx01_notify_enabled = enabled;
    } else if (idx == TDX_IDX_14_CFG) {
        switch_mode_notify_enabled = enabled;
    }

    ESP_LOGI(TDX_BLE_LOG_TAG, "CCCD write idx=%u value=0x%04X enabled=%d",
             idx, cccd, enabled);
}

static esp_err_t send_notify_by_index(uint16_t idx, uint8_t *data, uint16_t len)
{
    if (!is_connected || spp_gatts_if == ESP_GATT_IF_NONE) {
        ESP_LOGW(TDX_BLE_LOG_TAG, "notify skipped, BLE not connected");
        return ESP_ERR_INVALID_STATE;
    }
    if (idx >= SPP_IDX_NB || spp_handle_table[idx] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((idx == TDX_IDX_01_VAL && !tdx01_notify_enabled) ||
        (idx == TDX_IDX_14_VAL && !switch_mode_notify_enabled)) {
        ESP_LOGW(TDX_BLE_LOG_TAG, "notify skipped, idx=%u not subscribed", idx);
        return ESP_ERR_INVALID_STATE;
    }

    return esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id,
                                       spp_handle_table[idx], len, data, false);
}

extern "C" esp_err_t SendData_indicate(uint8_t *data, uint16_t len)
{
    ESP_LOGI(TDX_BLE_LOG_TAG, "SendData_indicate len=%u", (unsigned int)len);
    return send_notify_by_index(TDX_IDX_14_VAL, data, len);
}

extern "C" void Tdx01_indicate(uint8_t *data, uint16_t len)
{
    ESP_LOGI(TDX_BLE_LOG_TAG, "Tdx01_indicate len=%u", (unsigned int)len);
    (void)send_notify_by_index(TDX_IDX_01_VAL, data, len);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TDX_BLE_LOG_TAG, "Advertising data configured, start advertising");
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TDX_BLE_LOG_TAG, "Scan response data configured");
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TDX_BLE_LOG_TAG, "Advertising start failed: %s",
                     esp_err_to_name(param->adv_start_cmpl.status));
        } else {
            ESP_LOGI(TDX_BLE_LOG_TAG, "Advertising started");
        }
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(TDX_BLE_LOG_TAG, "GATTS EVT=%s(%d)", gatts_evt_name(event), event);

    switch (event) {
    case ESP_GATTS_REG_EVT:
        init_broadcast_data();
        // Log this stage before configuring raw advertising data so init failures show the last completed step.
        // 閰嶇疆鍘熷骞挎挱鏁版嵁鍓嶆墦鍗伴樁娈垫棩蹇楋紝鍒濆鍖栧け璐ユ椂鍙互鐪嬪嚭鏈€鍚庡畬鎴愬埌鍝竴姝ャ€?        ESP_LOGI(TDX_BLE_LOG_TAG, "Register OK app_id=%u, device_name=%s",
                 param->reg.app_id, TDX_BLE_DEVICE_NAME);
        esp_ble_gap_set_device_name(TDX_BLE_DEVICE_NAME);
        esp_ble_gap_config_adv_data_raw(spp_adv_data, sizeof(spp_adv_data));
        esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_data, sizeof(scan_rsp_data));
        esp_ble_gatts_create_attr_tab(spp_gatt_db, gatts_if, SPP_IDX_NB, TDX_BLE_SERVICE_INST_ID);
        break;

    case ESP_GATTS_WRITE_EVT: {
        uint8_t idx = find_attr_index(param->write.handle);
        ESP_LOGI(TDX_BLE_LOG_TAG,
                 "WRITE handle=0x%04X idx=%u len=%u prep=%d need_rsp=%d trans_id=%" PRIu32,
                 param->write.handle, idx, param->write.len, param->write.is_prep,
                 param->write.need_rsp, param->write.trans_id);

        if (idx == TDX_IDX_01_CFG || idx == TDX_IDX_14_CFG) {
            handle_cccd_write(idx, param);
        } else if (idx == TDX_BLE_SWITCH_MODE_VALUE_INDEX && !param->write.is_prep) {
            print_ble_received_data(param->write.value, param->write.len);
            (void)User_QueueBleWriteBytes(param->write.value, param->write.len);
        }

        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                        param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        spp_conn_id = param->connect.conn_id;
        spp_gatts_if = gatts_if;
        is_connected = true;
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, TDX_BLE_TX_POWER_LOWEST);
        ESP_LOGI(TDX_BLE_LOG_TAG, "BLE connected conn_id=%u remote=%02X:%02X:%02X:%02X:%02X:%02X",
                 spp_conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1],
                 param->connect.remote_bda[2], param->connect.remote_bda[3],
                 param->connect.remote_bda[4], param->connect.remote_bda[5]);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TDX_BLE_LOG_TAG, "BLE disconnected reason=0x%02X", param->disconnect.reason);
        is_connected = false;
        tdx01_notify_enabled = false;
        switch_mode_notify_enabled = false;
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TDX_BLE_TX_POWER_LOWEST);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, TDX_BLE_TX_POWER_LOWEST);
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TDX_BLE_LOG_TAG, "Create attr table failed: 0x%x",
                     param->add_attr_tab.status);
        } else if (param->add_attr_tab.num_handle != SPP_IDX_NB) {
            ESP_LOGE(TDX_BLE_LOG_TAG, "Attr table handle count mismatch: %d != %d",
                     param->add_attr_tab.num_handle, SPP_IDX_NB);
        } else {
            memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
            dump_handle_table();
            ESP_LOGI(TDX_BLE_LOG_TAG, "Start GATT service handle=0x%04X", spp_handle_table[SPP_IDX_SVC]);
            esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
        }
        break;

    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            spp_profile_tab[TDX_BLE_PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(TDX_BLE_LOG_TAG, "Register app failed, status=%d", param->reg.status);
            return;
        }
    }

    for (int idx = 0; idx < TDX_BLE_PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == spp_profile_tab[idx].gatts_if) {
            if (spp_profile_tab[idx].gatts_cb != NULL) {
                spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

extern "C" void Init_Bl(void)
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    // Keep each BLE init stage logged so board bring-up can identify the exact failing API.
    // 姣忎釜 BLE 鍒濆鍖栭樁娈甸兘鎵撳嵃鏃ュ織锛屾柟渚夸笂鏉胯皟璇曟椂瀹氫綅鍏蜂綋澶辫触鍦ㄥ摢涓?API銆?    ESP_LOGI(TDX_BLE_LOG_TAG, "BLE init start");
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    ESP_LOGI(TDX_BLE_LOG_TAG, "Classic BT memory released");
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_LOGI(TDX_BLE_LOG_TAG, "BT controller initialized");
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_LOGI(TDX_BLE_LOG_TAG, "BT controller enabled in BLE mode");
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_LOGI(TDX_BLE_LOG_TAG, "Bluedroid initialized");
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI(TDX_BLE_LOG_TAG, "Bluedroid enabled");

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, TDX_BLE_TX_POWER_LOWEST);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, TDX_BLE_TX_POWER_LOWEST);
    ESP_LOGI(TDX_BLE_LOG_TAG, "BLE TX power set to lowest");

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(UserBleDataHandler_Init());
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(TDX_BLE_APP_ID));

    ESP_LOGI(TDX_BLE_LOG_TAG, "BLE init done");
}

#else

#include "esp_log.h"

bool is_connected = false;

extern "C" void Init_Bl(void)
{
    // English: Leave BLE startup as a no-op when USER_BLE_ENABLE is disabled.
    // 涓枃锛氬叧闂?USER_BLE_ENABLE 鏃讹紝钃濈墮鍒濆鍖栦繚鎸佺┖鎿嶄綔锛岄伩鍏嶆媺璧疯摑鐗欏崗璁爤銆?    ESP_LOGI("BLE", "BLE disabled by USER_BLE_ENABLE=0");
}

extern "C" void get_ble_mac_no_colon(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';
    const char *mac = ch583_wifi_uart_get_ble_mac();
    if (mac != NULL) {
        snprintf(out, out_size, "%s", mac);
    }
}



extern "C" esp_err_t SendData_indicate(uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void Tdx01_indicate(uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

#endif
