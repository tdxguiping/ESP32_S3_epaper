# README_Reference.md

本文件保存代码定位资料：公共配置、WiFi/HTTP Server、EPD 驱动、静态资源、目录与函数索引，以及日志约定。功能与状态规则以 [README_Fun.md](README_Fun.md) 为准。

图片业务持久状态源码索引：`main/app_nvs.c/.h` 固定通过 `nvs_open()` 访问默认 NVS 分区的 `image_state` namespace，并负责 Blob size/read/write/erase；`main/persistent_state/app_persistent_state.c/.h` 负责 `slide_cfg`、`slide_ctl`、`last_cast` 的版本化结构、CRC、generation、字段校验、写后读回和互斥。固件不包含 SD 文本迁移模块，不读取相关 `.txt`，也不维护独立 `slide_random` key；完整烧录和应用程序 OTA 使用同一 NVS 位置。相关宏全部位于 `main/tdx_cfg.h`，`partitions/v2/16m.csv` 不为图片状态增加独立分区。

## 目录 <span id="toc"></span>

- [调试日志与敏感信息约定](#sec-debug-log)
- [2. 配置与公共参数](#sec-02)
- [5. WiFi STA 与 HTTP Server](#sec-05)
  - [5.1 V2 ping 连通性检查](#sec-05-1)
- [12. EPD 显示队列与屏幕类型](#sec-12)
  - [12.1 EPD 显示队列与显示任务](#sec-12-1)
  - [12.2 EPD 类型管理与 NVS 保存](#sec-12-2)
  - [12.3 display_bsp：EPD SPI / GPIO](#sec-12-3)
  - [12.4 epd_type_800_480](#sec-12-4)
  - [12.5 epd_type_1024_600](#sec-12-5)
  - [12.6 epd_type_1600_1200_common](#sec-12-6)
  - [12.7 epd_type_1600_1200_79](#sec-12-7)
  - [12.8 epd_type_1600_1200_133](#sec-12-8)
    - [12.8.1 epd_type_1600_1200_133_DKE](#sec-12-8-1)
  - [12.9 epd_type_1360_480_1085](#sec-12-9)
  - [12.10 epd_type_1360_480_1085_3color](#sec-12-10)
  - [12.11 epd_type_4color_common](#sec-12-11)
  - [12.12 epd_type_800_480_4s_75](#sec-12-12)
  - [12.13 epd_type_800_480_4s_75_DKE](#sec-12-13)
  - [12.14 epd_type_800_480_4s_75_mofang](#sec-12-14)
  - [12.15 三色屏测试数据源](#sec-12-15)
  - [12.16 EPD/SD 共用电源测试](#sec-12-16)
- [14. 文件服务器静态资源与旧接口](#sec-14)
- [16. 目录到功能索引](#sec-16)
- [17. zlib 文件压缩模块与自检索引](#sec-17-zlib)
- [Local Image Browsing 源码参考](#sec-local-image-reference)
- [按功能整理的相关文件与辅助函数](#moved-code-index)

## 调试日志与敏感信息约定 <span id="sec-debug-log"></span>

```text
当前工程调试日志较多，包括启动信息、heap/PSRAM、PM/light sleep、HTTP header/body、WiFi 配置和 CH583 通信状态。
当前仍处于开发阶段，允许 WiFi password、HTTP body、OTA meta 等明文调试输出，便于确认配网、NVS 读取和协议字段是否正确。
量产或外发固件前，再关闭详细日志或做敏感信息脱敏。

开发阶段允许：
- server_network_sta.c 的 WiFi 配网/恢复流程打印 ssid/password 明文。
- usb_console_wifi.c 的 USB WiFi 配网流程打印 ssid/password 明文。
- ble_data_handler.cpp 开发阶段打印完整 WiFi JSON，因此其中的 key 也会明文输出。
- process_small_json_request() 打印 JSON body；超过 240 字节时只打印前 240 字节。
- OTA 打印 meta、版本、大小、分区和进度信息。
- PM/light sleep 打印锁和阻塞源，便于功耗调试。

量产/外发前建议：
- 开发阶段 WiFi password 使用明文日志，发布前必须关闭。
- HTTP 大包不打印完整 body；small JSON正文日志最多打印前240字节。
- OTA raw meta 和进度细节降到 debug/verbose 或关闭。
- PM/light sleep 诊断日志默认关闭。
- 如仍需完整 body/password，应增加单独显式调试宏，默认关闭。
```

## 2. 配置与公共参数 <span id="sec-02"></span>

Mermaid 配置依赖图：

```mermaid
flowchart TD
    A[tdx_cfg.h] --> B[mount.c / SD SDSPI]
    A --> C[ch583_uart_app.c / UART GPIO]
    A --> D[epd_display / EPD SPI GPIO]
    A --> E[server_network_sta / HTTP body size]
    A --> F[network_ota_upload / OTA limit]
    G[sdkconfig.defaults.esp32c5] --> B
    G --> H[partition table / PSRAM / USB console]
```


相关文件：

```text
main/tdx_cfg.h
sdkconfig.defaults
sdkconfig.defaults.esp32c5
partitions/v2/16m.csv
```

功能说明：

```text
tdx_cfg.h
├─ WiFi/HTTP/USB/OTA/body size 配置
├─ CH583 UART C5 引脚配置
├─ 运行期调试日志输出配置
├─ EPD C5 SPI/GPIO 配置
├─ SD SDSPI C5 引脚配置
├─ CH583 LED 控制配置
├─ mDNS 名称配置
└─ NVS key / slideshow / work time / EPD type 配置
```

关键配置方向：

```text
sdkconfig.defaults.esp32c5
└─ C5 build config
   ├─ 16MB Flash
   ├─ partitions/v2/16m.csv
   ├─ SDSPI: MOSI=1 MISO=25 CLK=6 CS=26
   ├─ Quad PSRAM
   ├─ USB Serial/JTAG console
   └─ CPU0 timer affinity

tdx_cfg.h
└─ C source include
   ├─ mount.c 读取 USER_SD_SPI_*
   ├─ display_bsp.cpp / epd_display_app.cpp 读取 USER_EPD_*
   ├─ ch583_uart_app.c 读取 USER_CH583_UART_*
   ├─ debug_output.c 读取 USER_DEBUG_OUTPUT_* / USER_DEBUG_UART_*
   ├─ led_status.c 读取 USER_LED_CH583_*
   ├─ network_ota_upload.c 读取 SERVER_NETWORK_STA_OTA_*
   └─ server_network_sta.c 读取 USER_MDNS_*
```

串口和调试输出约定：

```text
CH583 通信串口：
UART1
TX = GPIO24
RX = GPIO23
baud = 115200

运行期调试日志：
当前 USER_DEBUG_OUTPUT_TARGET = USER_DEBUG_OUTPUT_BOTH
UserDebugOutput_Init() 后同时输出到 USB Serial/JTAG 和 UART0
也可在 tdx_cfg.h 中改为仅 USB 或仅 UART0
UART0 TX = GPIO11
UART0 RX = GPIO12
UART0 baud = 921600
```

说明：

```text
sdkconfig / sdkconfig.defaults 控制 bootloader、ESP-IDF 早期 console 和系统默认 console。
tdx_cfg.h 中的 USER_DEBUG_OUTPUT_TARGET 只控制 app_main() 调用 UserDebugOutput_Init() 之后的应用层日志输出。
应用层 `ESP_LOGx` 和已接入 `UserDebugOutput_Printf()` 的直接 `printf` 调试输出，都按 USER_DEBUG_OUTPUT_TARGET 路由。
如果要把 bootloader/早期 ESP-IDF console 也切到 UART0，必须修改 SDK configuration；只改 tdx_cfg.h 不会生效。
bootloader 和 UserDebugOutput_Init() 之前的早期 console 仍由 SDK configuration 控制，当前保留 USB Serial/JTAG。
UART0 调试启用时，GPIO11/GPIO12 不再作为 gpio_test 输出脚使用。
```

日志相关宏：

```text
当前代码处于开发调试配置，以下默认值与 tdx_cfg.h 保持一致：

SERVER_NETWORK_STA_DEBUG_LOG_ENABLE=1
当前默认保留 WiFi STA 连接细节日志，便于排查 STA_START、BSSID、RSSI、WiFi PS、esp_wifi_start/connect 返回值。

SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT=1
开发阶段打印明文 WiFi 密码；发布前改为 0。

USER_NVS_VERBOSE_LOG_ENABLE=0
只保留 NVS 失败日志。

USER_STORAGE_LIST_ON_STARTUP_ENABLE=0
默认不在启动时逐项扫描打印 /data 文件树。

USER_HTTP_FILE_LIST_LOG_ENABLE=0
默认不逐项打印 HTTP 目录列表文件；以后排查文件浏览输出时可打开。

USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE=0
默认关闭 legacy multipart fallback 的 field、boundary、slot 等细节日志；关键保存和错误日志保留。

USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE=16384
`cast_core.c`、`usb_console_common.c` 和 `usb_console_upload.c` 使用同一个 16 KiB stdio 文件缓冲。比原 64 KiB 配置每个正在保存的文件减少 49152 字节瞬时内存。缓冲申请或 `setvbuf()` 失败时打印 `ESP_LOGW` 并继续使用 stdio 默认缓冲，不改变原有保存接口。

USER_FILE_SAVE_MEMORY_LOG_ENABLE=1
开发阶段在上述三条文件保存路径的写入前后打印 internal RAM / PSRAM 的 free 和 largest block。正常开始、完成使用 `ESP_LOGI`，缓冲退化使用 `ESP_LOGW`，`fwrite()` 长度不符或 `fclose()` flush 失败使用 `ESP_LOGE`。

USER_USB_CONSOLE_ANSI_COLOR_TEST_ENABLE=0
默认关闭 ANSI 颜色测试。

SERVER_NETWORK_STA_OTA_DETAIL_LOG_ENABLE=0
只保留 OTA 关键节点和错误日志。

SERVER_NETWORK_STA_OTA_RESTART_DELAY_MS=500
OTA 成功响应结束且 HTTP handler 返回后，专用任务等待该时长再复位。

USER_OTA_LOCAL_CONFIRM_WARNING_MS=6000
本地关键初始化后的首次启动确认超过该时长时输出警告，不主动延迟确认。

USER_OTA_CH583_POWER_CUT_LIMIT_MS=10000
记录 APP 收到 ESP32-C5 OTA成功结果后，CH583允许的最大关电等待窗口，用于诊断和测试验收。

SERVER_NETWORK_STA_OTA_RESTART_TASK_STACK_SIZE=3072
SERVER_NETWORK_STA_OTA_RESTART_TASK_PRIORITY=5
仅供 network_ota_upload 模块的延时复位任务使用，不复用其他业务任务配置。
```

---




存 / 取信息（含条件限制）：

```text
存：
- 本节是编译期配置，不直接执行运行时存储。

取：
- 各模块通过 include tdx_cfg.h 读取路径、NVS key、GPIO、body size、EPD type、WiFi 工作时长等宏。
- sdkconfig.defaults.esp32c5 / partitions/v2/16m.csv 在构建期被 IDF 读取。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-02)

---

## 5. WiFi STA 与 HTTP Server <span id="sec-05"></span>

实际返回码（正式定义见 `README_Result_Code.md`）：

| 返回 | result | 说明 |
|---|---|---|
| `ping_result` | `0` | `/ping` 正常返回，包含 `EPD=BUSY/IDLE` 和非空 `Ble_MAC` |
| `ping_result` | `1405` | `Ble_MAC` 尚未从 CH583 获取；网络和 USB 都返回 `EPD=BUSY/IDLE`、空 `Ble_MAC` 和该错误码 |
| WiFi 连接事件通知 | `1307` | WiFi 连接超时 |
| WiFi 连接事件通知 | `1308` | WiFi 认证失败 |
| WiFi 连接事件通知 | `1309` | WiFi 获取 IP 失败 |

WiFi、DHCP、HTTP 与 mDNS 由唯一的 `wifi_manager` Task 管理。BLE / CH583 和 USB 的普通 `wifi` 配网请求先保存配置，再通过一个 `NEW_CREDENTIAL` 命令交给 manager；调用者不再先发送 PROVISIONING 后再发送 FORCE_CONNECT。后台结果仍使用 `1307`（连接超时）、`1308`（连续认证失败）和 `1309`（已关联但未取得 IP）。`wifi_wakeup` 会识别 CONNECTING、DISCONNECTING、WAITING_IP、RETRY_WAIT、GOT_IP、STARTING_SERVICES、READY、AUTH_FAILED、NO_CONFIG 和 FAILED；已有连接、主动断线或恢复流程时只返回当前 stage，不重复强制重连。进入 DISCONNECTING 时立即清除旧 IP、HTTP ready 和 mDNS ready。USB `/wifi` 仍只同步返回保存和 worker 提交结果，`wifi_status` 可查询完整状态快照及 last_result、连接/凭据代次、断线目的和 READY 稳定窗口剩余时间。

WiFi 重试间隔固定为 `1000/2000/3000/3500/4000/4500 ms`，第6次以后保持4500 ms。进入 READY 后必须连续稳定30秒才清零连接、抖动和认证累计计数，因此短时间反复掉线会逐级退避；稳定30秒后的第一次掉线仍从1000 ms开始。driver 的 `failure_retry_cnt=0`，不存在第二套 driver 重试。

CH583/BLE 的 `wifi_wakeup` 结果通知在 `ble_data_handler.cpp` 层单独防止过早 `1307`：旧同步调用先返回失败、但状态快照仍为 CONNECTING、DISCONNECTING、WAITING_IP、RETRY_WAIT、GOT_IP 或 STARTING_SERVICES 时，通知 task 从取得该结果起最多 10 秒、每 200 ms 只读一次状态。READY 且已有 IP、HTTP ready 时发送 `wifi_info_result`；明确终止或 10 秒到期才按原结果发送。该观察逻辑不调用 connect/disconnect，不修改 manager、退避、认证或 DHCP 状态机。

同一BLE worker内维护一个只用于wakeup流程的最新配置pending槽，状态为NONE/SAVING/READY。`wifi_wakeup` 尚未完成时，普通 `wifi` 在flow lock内先预留SAVING及generation，再写NVS，成功后只把匹配generation发布为READY；worker检测到SAVING时等待，检测到READY后取消旧wakeup尚未发送的最终通知，并使用相同任务进入现有 `User_Network_mode_app_new_credential()`。重复请求只保留最新generation，普通 `wifi` worker忙时仍保留原BUSY规则。

BLE/CH583 worker提交使用同一flow lock发布 `submit_in_progress`，成功占用提交状态后才启动对应关机guard，再创建task并发布handle。并发BUSY请求不启动或清除guard，避免失败请求误清除另一条已受理请求的保护。

`wifi_work_time` 模块独立维护WiFi connect guard的绝对FreeRTOS deadline，宏 `WIFI_CONNECT_POWER_GUARD_MAX_MS` 固定为45000，不复用但不得短于 `SERVER_NETWORK_STA_SYNC_REQUEST_TIMEOUT_MS`，`tdx_cfg.h` 使用编译期检查保持该关系。冷启动在调用现有同步联网函数前调用原子 `ServerNetworkStaWifiWorkTime_StartWifiConnectGuardIfInactive()`：没有有效guard才创建新deadline，手机请求已经建立guard时只复用且不刷新；READY或明确终止时清除，manager仍进行中时保留到绝对上限。guard在初始关机判断、LED准备后的final guard和SPI锁内locked guard三处检查；单次请求不随retry延长。worker路径在READY、终止或到期时清除；收到 `wifi_wakeup` 时manager已经在连接的无worker路径也设置同一45秒自动到期guard，且不提交新的连接命令。新配置排队期间沿用旧guard，worker接管后按新配置的接收tick只设置剩余窗口，确保新请求从接收起也不超过45秒。

`read_saved_wifi()` 的有效凭据日志由 `tdx_cfg.h` 中的 `SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT` 控制。当前开发阶段设为1，两个NVS读取来源都输出SSID和明文password；设为0时只输出SSID。该开关不改变NVS凭据和ESP-IDF WiFi配置，正式发布前必须设为0。

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant APP as app_main
    participant SYS as ESP-IDF system
    participant STA as server_network_sta.c
    participant WIFI as esp_wifi
    participant HTTP as file_server.c
    participant NET as net_data handlers
    APP->>SYS: esp_netif_init() / esp_event_loop_create_default()
    APP->>STA: User_Network_mode_app_init("/data")
    STA->>STA: Queue CONNECT command
    STA->>WIFI: ensure driver once / set_config / connect
    WIFI-->>STA: STA_CONNECTED event queued
    STA->>STA: WAITING_IP
    WIFI-->>STA: GOT_IP event queued
    STA->>STA: GOT_IP → STARTING_SERVICES
    STA->>HTTP: example_start_file_server()
    HTTP->>NET: register GET /* and POST handlers
    STA->>STA: HTTP handlers ready → READY
    STA->>STA: mDNS failure retries independently
```


相关文件：

```text
main/server_network_sta/server_network_sta.c
main/server_network_sta/server_network_sta.h
main/file_server.c
```

WiFi 启动树状时序：

```text
main/main.c
└─ app_main()
   ├─ ServerNetworkSta_Init()
   │  ├─ 创建 wifi_manager Queue / Task / status mutex
   │  └─ WiFi/IP callback 只负责非阻塞投递事件
   └─ User_Network_mode_app_init("/data")
      └─ Queue CONNECT
         └─ wifi_manager
            ├─ 读取 wifi:ssid/password，失败时回退 nvs.net80211
            ├─ ensure_wifi_stack()：driver、netif、handler 只初始化一次
            ├─ channel hint=0，failure_retry_cnt=0
            ├─ CONNECTING → WAITING_IP → GOT_IP
            ├─ STARTING_SERVICES
            │  ├─ HTTP handler 全部成功后置 ready
            │  └─ mDNS 失败独立后台重试
            └─ READY
```

事件处理时序：

```text
ESP-IDF WiFi/IP event
└─ wifi_event_handler()
   └─ xQueueSend(..., 0)
      └─ wifi_manager
         ├─ STA_CONNECTED → WAITING_IP
         ├─ GOT_IP → STARTING_SERVICES → READY
         ├─ LOST_IP → expected disconnect(DHCP_RECOVERY) → RETRY_WAIT
         ├─ DISCONNECTED → purpose/物理链路/当前代次校验
         └─ 队列满 → atomic resync/provisioning 兜底
```

HTTP server 注册时序：

```text
file_server.c
└─ example_start_file_server()
   ├─ httpd_start()
   ├─ register GET /
   │  └─ index_html_get_handler()
   ├─ register GET /*
   │  └─ download_get_handler()
   │     ├─ ServerNetworkStaPing_ProcessGet(req)
   │     │  ├─ ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity()
   │     │  ├─ get_ble_mac_no_colon()
   │     │  ├─ ServerNetworkStaEpdDisplay_IsBusy()
   │     │  ├─ 设置 application/json 和 Connection: close
   │     │  └─ 如果 URI 是 /ping，返回包含 EPD 与 Ble_MAC 的 ping JSON
   │     ├─ ServerNetworkStaTime_ProcessGet(req)
   │     │  └─ 如果 URI 是 /time，直接返回 time JSON
   │     ├─ ServerNetworkStaSavedImages_SendThumbnail()
   │     └─ 普通静态文件 / 目录列表处理
   ├─ register legacy upload/delete
   │  ├─ upload_post_handler()
   │  └─ delete_post_handler()
   └─ server_network_sta_net_data_register_handlers()
      └─ server_network_sta/net_data/server_network_sta_data.c
         ├─ register POST /dataUP
         ├─ register POST /ota
         └─ register POST /ota_upload
```

HTTP server 内存与生命周期说明：

```text
当前工程按 HTTP server 单实例持续运行设计。
file_server.c 保存 httpd handle、server_data 和 handlers-ready 状态。
任何必要 URI 注册失败都会 httpd_stop、释放 server_data、清空 handle 并返回错误。
http_running 表示 HTTP Task 实例存在；http_ready 表示当前有 IP 且必要 handlers 已完成注册。
WiFi 断线时 http_running 可以保持 true，但 http_ready 必须为 false；重新取得 IP 后不重复启动实例或注册 handlers。
```

auto light sleep / HTTP 接收注意事项：

```text
当前默认 TDX_AUTO_LIGHT_SLEEP_ENABLE=0，HTTP 接收可靠性优先。
运行时 PM 仍可配置为 80 MHz 固定频率，但不进入 Auto Light-sleep。
如果开启 auto light sleep，WiFi 省电、CPU 唤醒延迟、HTTP socket 超时可能叠加影响网络请求。
表现可能是 /ping 偶发慢响应、POST /dataUP body 接收失败、httpd_req_recv() 返回错误、客户端认为连接超时或断开。
尤其是 App/PC 发送小 JSON 后立即等待响应，或者发送大 body/multipart/OTA 时，不建议启用 auto light sleep。
若后续必须开启，需要单独验证 WiFi PS、HTTP timeout、客户端重试策略和大包上传稳定性。
```

---




### 5.1 V2 协议资料拆分：ping 连通性检查 <span id="sec-05-1"></span>

请求：

```http
GET /ping HTTP/1.1
```

BLE MAC 已取得时的返回示例：

```json
{
  "func": "ping_result",
  "result": 0,
  "message": "ok",
  "EPD": "BUSY",
  "Ble_MAC": "AABBCCDDEEFF"
}
```

BLE MAC 尚未取得时的返回示例：

```json
{
  "func": "ping_result",
  "result": 1405,
  "message": "Ble_MAC not ready",
  "EPD": "IDLE",
  "Ble_MAC": ""
}
```

`EPD` 由 `ServerNetworkStaEpdDisplay_IsBusy()` 生成：

```text
BUSY  显示任务 active、pending job 计数大于 0、EPD 队列仍有消息，任一条件成立
IDLE  以上三项均不存在
```

实际代码位置：

```text
main/server_network_sta/ping/server_network_sta_ping.c
main/usb_console_echo/ping/usb_console_ping.c
main/epd_display/epd_display_app.cpp
```

当前固件正式输出字段名固定为 `Ble_MAC`，不输出小写 `ble_mac`。前端可以兼容旧资料中的小写写法，但校验当前设备响应时应使用 `Ble_MAC`；非空 MAC 必须与目标设备一致，避免缓存 IP 指向错误设备。


存 / 取信息（含条件限制）：

```text
存：
- 本模块连接 WiFi 和启动 HTTP Server，不直接保存 WiFi 配置。

取：
- `read_saved_wifi()` 优先读取 namespace="wifi" 的 ssid/password。
- 读取失败后读取 namespace="nvs.net80211" 的 sta.ssid / sta.pswd blob。
- manager 收到 IP_EVENT_STA_GOT_IP 后校验当前 AP/IP，再推进 GOT_IP、STARTING_SERVICES 和 READY。
- HTTP handlers 注册失败会独立重试；mDNS 失败不阻止直接通过 IP 使用 HTTP，并在后台独立重试。

日志：
- 开发阶段 WiFi credential 打印 SSID 和明文密码；发布前关闭。
- 保留关键节点：saved WiFi 读取结果、WiFi IP、断开原因、认证累计达到阈值、mDNS ready、HTTP server ready、网络初始化失败。
- 当前 `SERVER_NETWORK_STA_DEBUG_LOG_ENABLE=1`，默认保留 STA_START、BSSID/RSSI、WiFi PS、esp_wifi_start/connect 返回值等细节日志；发布前建议改为 0。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-05)

---

## 12. EPD 显示队列与屏幕类型汇总 <span id="sec-12"></span>

本章把 `main/epd_display/` 目录拆成二级目录。当前 `main/CMakeLists.txt` 编译的 EPD 相关源码包括显示队列、显示模式、BSP、屏幕类型管理、各具体屏幕驱动和测试图文件。

Mermaid 总览图：

```mermaid
flowchart TD
    A[cast/upload/slideshow/USB] --> B[epd_display_app.cpp 队列]
    B --> C[epd_type.cpp 类型分发]
    C --> D[display_bsp.cpp SPI/GPIO]
    C --> E[具体 epd_type_*.cpp]
    E --> D
    D --> F[EPD Panel]
    G[USB /epd_type] --> C
    C --> H[NVS epd_type]
    A --> I[epd_display_mode.c]
    I --> J[NVS epd_mode]
```

EPD 显示模式：

```text
PhotoPainter:epd_mode，u8
0 NORMAL     普通模式
1 SLIDESHOW  轮播模式
2 DAILY      每日更新模式
3 LOCAL_IMAGE_BROWSING  本地图片浏览模式
```

启动时 `EpdDisplayMode_Init()` 读取 `epd_mode`，不存在时写入默认 `0`。如果读到非法值，恢复为 `0`。模式为 `3` 时初始化本地图片浏览状态，不启动 DAILY 或 SLIDESHOW，并等待 PB2 事件。`app_main()` 会打印当前模式，例如 `EPD display mode=0(NORMAL)`。

同步规则：

```text
NVS slide_ctl enabled=true 写入并读回成功 -> EpdDisplayMode_SetBySlideshowSwitch(true)  -> epd_mode=1
NVS slide_ctl enabled=false 写入并读回成功 -> EpdDisplayMode_SetBySlideshowSwitch(false) -> epd_mode=0
```

存 / 取信息（含条件限制）：

```text
存：
- EpdType_SetAndSave() 只保存合法屏幕类型；合法条件是 EpdType_GetConfig(type) 能找到配置。
- EpdDisplayMode_Set() 只保存 0/1/2；轮播控制写0/1，每日一图配置成功写2，cast/cast2pic成功接收写0。
- 如果当前 EPD_type 已经等于目标 type，则 changed=false，不重复写 NVS。
- 显示队列只保存 RAM buffer，不写 SD；队列长度由 USER_EPD_DISPLAY_QUEUE_LENGTH 限制。
- 入队需要 malloc/copy display buffer 成功；队列满或内存不足时失败。

取：
- EpdType_LoadSavedOrDefault() 从 PhotoPainter NVS 的 USER_EPD_TYPE_NVS_KEY 读取屏幕类型。
- EpdDisplayMode_Init() 从 PhotoPainter NVS 的 USER_EPD_DISPLAY_MODE_NVS_KEY 读取显示模式。
- 如果保存值非法，则回退 USER_EPD_TYPE_DEFAULT，并尝试把默认值写回 NVS。
- 显示任务从 RAM 队列取 buffer；按 EPD_type 分发到具体 EPD 驱动。
- 具体驱动读取当前 display buffer，不直接读取 SD/NVS。
```

### 12.1 EPD 显示队列与显示任务 <span id="sec-12-1"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant SRC as cast/upload/slideshow/USB
    participant Q as s_epd_display_queue
    participant TASK as ServerNetworkStaEpdDisplay_Task
    participant TYPE as EpdType_DisplayCurrent
    participant LED as led_status
    SRC->>Q: Queue()/QueueToScreen()/QueueToScreenAndWait()
    Q->>TASK: xQueueReceive()
    TASK->>LED: ActivityBegin(EPD)
    TASK->>TYPE: EpdType_DisplayCurrent(buffer,size)
    TYPE-->>TASK: display done/fail
    TASK->>LED: ActivityEnd(EPD)
    TASK-->>SRC: completion result (sync wait only)
```

相关文件：

```text
main/epd_display/epd_display_app.cpp
main/epd_display/epd_display_app.h
main/tdx_cfg.h
```

树状时序：

```text
cast/upload/slideshow/USB
├─ asynchronous source: ServerNetworkStaEpdDisplay_Queue()
│  └─ ServerNetworkStaEpdDisplay_QueueToScreen()
└─ completion-required source: ServerNetworkStaEpdDisplay_QueueToScreenAndWait()
   ├─ malloc/copy display buffer
   ├─ xQueueSend(s_epd_display_queue)
   └─ wait actual display result

ServerNetworkStaEpdDisplay_Task()
├─ xQueueReceive()
├─ UserLedStatus_ActivityBegin(USER_LED_ACTIVITY_EPD)
├─ ePaperDisplay.Set_EPD_which_one()
├─ EpdType_DisplayCurrent()
└─ UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_EPD)
```

关键辅助函数：

```text
ServerNetworkStaEpdDisplay_Init()
ServerNetworkStaEpdDisplay_Queue()
ServerNetworkStaEpdDisplay_QueueToScreen()
ServerNetworkStaEpdDisplay_QueueToScreenAndWait()
ServerNetworkStaEpdDisplay_Task()
```

存 / 取信息（含条件限制）：

```text
存：
- RAM 中保存待显示 buffer；不是持久化数据。
- 队列长度：USER_EPD_DISPLAY_QUEUE_LENGTH=2。
- 入队前必须分配内存并复制 display buffer；内存不足或队列满会导致入队失败。
- 同步等待最长 USER_EPD_DISPLAY_WAIT_TIMEOUT_MS；completion 使用双引用生命周期，等待超时不会释放 EPD 任务仍在使用的对象。

取：
- 显示任务从 s_epd_display_queue 取出 buffer。
- 取出后按当前 EPD_type 调用 EpdType_DisplayCurrent()。
- EpdType_DisplayCurrent() 返回实际 esp_err_t；尺寸、内存、SPI 写入或 BUSY 超时会传回调用方。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.2 EPD 类型管理与 NVS 保存 <span id="sec-12-2"></span>

实际返回码（正式定义见 `README_Result_Code.md`）：

| 返回 | result | 说明 |
|---|---|---|
| `epd_type` | `0` | 当前 EPD 类型读取成功 |
| `epd_type` | `1801` | 当前 EPD 类型非法 |
| `epd_type_list` | `0` | EPD 类型列表返回成功 |
| `set_epd_type_result` | `0` | 设置成功，或目标类型与当前类型相同 |
| `set_epd_type_result` | `1801` | 目标 EPD 类型非法 |
| `set_epd_type_result` | `1802` | 保存 EPD 类型失败 |

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant USB as USB /epd_type
    participant TYPE as epd_type.cpp
    participant NVS as PhotoPainter NVS
    participant DRV as 具体 EPD 驱动
    USB->>TYPE: EpdType_SetAndSave(type)
    TYPE->>TYPE: EpdType_GetConfig(type)
    alt type 合法且变化
        TYPE->>NVS: app_nvs_write_u8(USER_EPD_TYPE_NVS_KEY,type)
    else 非法或未变化
        TYPE-->>USB: reject / skip
    end
    TYPE->>DRV: EpdType_DisplayCurrent() dispatch
```

相关文件：

```text
main/epd_display/epd_type.cpp
main/epd_display/epd_type.h
main/usb_console_echo/epd_type/usb_console_epd_type.c
main/app_nvs.c
```

树状时序：

```text
EpdType_LoadSavedOrDefault()
├─ app_nvs_read_u8(USER_EPD_TYPE_NVS_KEY)
├─ EpdType_GetConfig(saved_type)
├─ invalid saved type
│  ├─ fallback USER_EPD_TYPE_DEFAULT
│  └─ app_nvs_write_u8(USER_EPD_TYPE_NVS_KEY, default)
└─ EpdType_Set(saved_type)

EpdType_SaveForNextBoot(type)
├─ EpdType_GetConfig(type)
├─ app_nvs_read_u8(USER_EPD_TYPE_NVS_KEY)
├─ 保存值相同则跳过写入
└─ 保存值不同时写 NVS
   └─ 不调用 EpdType_Set()，本次启动不切换显示驱动

EpdType_DisplayCurrent()
└─ switch EPD_type
   ├─ EpdType800480_Display()
   ├─ EpdType1024600_Display()
   ├─ EpdType16001200_79_Display()
   ├─ EpdType16001200_133_Display()
   ├─ EpdType1360480_1085_Display()
   ├─ EpdType800480_4S_75_Display()
   ├─ EpdType1360480_1085_3Color_Display()
   ├─ EpdType800480_4S_75_DKE_Display()
   ├─ EpdType800480_4S_75_Mofang_Display()
   └─ EpdType16001200_133_DKE_Display()
```

关键辅助函数：

```text
EpdType_GetConfig()
EpdType_GetCurrentConfig()
EpdType_GetCount()
EpdType_GetConfigByIndex()
EpdType_Set()
EpdType_LoadSavedOrDefault()
EpdType_SetAndSave()
EpdType_SaveForNextBoot()
EpdType_DisplayCurrent()
EpdType_DispatchInit()
EpdType_DispatchDisplay()
EpdType_DispatchSleep()
EpdType_DispatchNT61522DisplayNet()
```

存 / 取信息（含条件限制）：

```text
存：
- 只保存合法 type；非法 type 返回 ESP_ERR_INVALID_ARG。
- type 未变化时不写 NVS，changed=false。
- 写入 key：USER_EPD_TYPE_NVS_KEY。

取：
- 启动时读取 NVS；不存在则使用 USER_EPD_TYPE_DEFAULT。
- 保存值非法时回退默认值，并尝试把默认值补写回 NVS。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.3 display_bsp：EPD SPI / GPIO 底层适配 <span id="sec-12-3"></span>

Mermaid 时序图：

```mermaid
flowchart TD
    A[具体 EPD 驱动] --> B[display_bsp.cpp / ePaperPort]
    B --> P[Set_Power / GPIO4 EPD-SD shared power]
    B --> C[EPD_WriteCMD]
    B --> D[EPD_WriteDATA]
    B --> E[EPD_Check_Busy]
    B --> F[spiTransmitData]
    F --> G[USER_EPD_SPI_HOST + GPIO]
```

相关文件：

```text
main/epd_display/display_bsp.cpp
main/epd_display/display_bsp.h
main/epd_display/epd_display_app.cpp
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.c
main/tdx_cfg.h
```

树状时序：

```text
EpdType*_Display()
└─ display_bsp / ePaperPort
   ├─ Set_Power(1)：GPIO4 HIGH；EPD/SD 正常运行期间保持供电
   ├─ spiTransmitData()
   ├─ EPD_WriteCMD()
   ├─ EPD_WriteDATA()
   ├─ EPD_Check_Busy()
   ├─ reset / dc / cs gpio control
   └─ refresh / sleep：只让 EPD 控制器休眠，不拉低 GPIO4
```

关键辅助函数：

```text
spiTransmitData()
EPD_WriteCMD()
EPD_WriteDATA()
EPD_Check_Busy()
ePaperPort::Set_Power()
ServerNetworkStaEpdDisplay_SetPower()
ePaperPort methods used by EpdType_* drivers
```

EPD / SD 公共电源规则：

```text
- EPD_SD_Power_PIN 固定为 GPIO_NUM_4，并配置为 GPIO_MODE_OUTPUT。
- ePaperPort 构造先调用 Set_Power(1)，再配置 EPD 控制脚和共享 SPI；ServerNetworkStaEpdDisplay_Init() 会再次确认 GPIO4 为 HIGH，避免冷启动时由未供电外设的信号脚产生瞬时反向供电。
- EPD_Reset() 再次调用 Set_Power(1)；EPD refresh/sleep 本身不调用 Set_Power(0)。独立 EPD/SD 电源测试会在 EPD 完成后等待所有安全条件满足，并先卸载外部 SD，之后才临时关闭 GPIO4。
- 当前 `USER_POWER_OFF_LOCAL_EPD_SD_CUTOFF_ENABLE=0`，work_state_task() 发送 CH583 POWER_OFF 后不调用 Set_Power(0)，GPIO4 持续保持 HIGH。
- work_state_task() 在共用 SPI 锁内复检所有 guard，再发送 POWER_OFF；无论 UART 写入成功或失败，GPIO4 都保持 HIGH。
- POWER_OFF UART 写入成功后等待 CH583 切断 ESP32-C5 电源；2 秒内仍未断电则沿用原逻辑调用 esp_restart()。
```

SPI DMA 分包规则：

```text
EPD 显示数据可能来自 PSRAM。ESP-IDF SPI driver 对 PSRAM 源数据可能临时申请内部 DMA TX buffer；
如果一次发送 30000/32768 bytes，在内部 DMA heap 碎片化时可能返回 ESP_ERR_NO_MEM。
因此 EPD 大数据发送统一使用 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072`，并由 `NT61522_SPI_SAFE_DMA_TX_CHUNK` 引用该配置。3072 bytes 低于实测碎片化场景的 3968 bytes 最大 DMA 空闲块。
spiTransmitData() 会兜底拆包；EPD_Sendbuffera()、EPD_WriteMultiData_ToMaster/Slave/Both() 以及各屏型直接发送路径也按同一安全分包发送。
EPD_Sendbuffera() 和 EPD_WriteMultiData_ToMaster/Slave/Both/Target() 返回 esp_err_t；任一 SPI transaction 失败时只打印关键 ESP_LOGE，调用 EpdType_ReportDisplayFailure(ret)，并让本次 EPD 显示最终返回失败。
spiTransmitCommand() 发送失败时不使用断言终止系统；它打印一次包含命令和错误码的 ESP_LOGE、上报显示失败并返回原始错误。`USER_EPD_SPI_SAFE_DMA_TX_CHUNK` 带非0编译期检查，避免错误配置导致发送循环无法推进。
NT61522_Display_net() 返回当前 EPD 显示结果；外层屏幕适配在数据加载失败时直接退出，不继续调用 update / refresh / sleep 刷新流程。
直接调用 EPD_WriteMultiData_Target() 的屏幕适配也会在失败时停止后续数据加载/刷新流程，避免底层 SPI 已失败但上层仍继续按成功刷新。
800x480、1024x600、1600x1200 7.9/13.3、1360x480、800x480 4S、DKE、mofang 等驱动均受该规则保护。发送失败后当前驱动立即停止装载，外层不得继续 update/refresh，也不得把本地浏览或轮播游标按成功推进。
```

存 / 取信息（含条件限制）：

```text
存：
- 不写 SD/NVS。
- 只把命令和数据通过 SPI/GPIO 写到 EPD 控制器。
- GPIO4 HIGH 期间为 EPD 和 SD 提供公共电源；本模块不在单次 EPD refresh/sleep 后关闭该电源。

取：
- 读取 tdx_cfg.h 中 USER_EPD_SPI_HOST、USER_EPD_*_PIN 等配置。
- 读取 BUSY 引脚状态判断屏幕忙闲。
- 读取 `Power_switch`：等于 1 时 GPIO4 输出 HIGH，其余值输出 LOW。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---
### 12.4 epd_type_800_480：800x480 三色屏 <span id="sec-12-4"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_800_480.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_800_480.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType800480_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType800480_Display()
EpdType800480_NT61522_DisplayNet()
EpdType800480_NT61522_InitDisplay()
```

屏幕配置：

```text
type=EPD_TYPE_800_480, width=800, height=480, display_size=192000, color=BWR_3_Color
名称：EPD_800_480_XingTai
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。
- 帧数据通过 spiTransmitData() 发送，按 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072` 小分包，避免 PSRAM 源数据触发临时 DMA TX buffer 分配失败。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.5 epd_type_1024_600：1024x600 六色屏 <span id="sec-12-5"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1024_600.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1024_600.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType1024600_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType1024600_Display()
EpdType1024600_NT61522_DisplayNet()
EpdType1024600_NT61522_InitDisplay()
```

屏幕配置：

```text
type=EPD_TYPE_1024_600, width=1024, height=600, display_size=307200, color=BWYRBG_6_Color
名称：EPD_1024_600_XingTai
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。
- 帧数据通过 spiTransmitData() 发送，按 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072` 小分包，避免 PSRAM 源数据触发临时 DMA TX buffer 分配失败。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.6 epd_type_1600_1200_common：1600x1200 公共逻辑 <span id="sec-12-6"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1600_1200_common.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1600_1200_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType16001200_79_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType16001200_79_Display()
EpdType16001200_133_Display()
EpdType_DispatchNT61522DisplayNet()
```

屏幕配置：

```text
供 1600x1200 79/133 版本复用的公共显示逻辑。
公共文件本身不对应单独 EPD_type，由 79/133 具体类型调用。
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.7 epd_type_1600_1200_79：1600x1200 79 版本 <span id="sec-12-7"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1600_1200_79.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1600_1200_79.cpp
main/epd_display/epd_type_1600_1200_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType16001200_79_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType16001200_79_Display()
EpdType16001200_79_NT61522_Init()
EpdType16001200_79_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_1600_1200_79, width=1600, height=1200, display_size=960000, color=BWYRBG_6_Color
名称：EPD_1600_1200_79_XingTai
```

实现说明：

```text
- 帧数据通过 spiTransmitData() 发送，按 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072` 小分包，避免 PSRAM 源数据触发临时 DMA TX buffer 分配失败。
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.8 epd_type_1600_1200_133：1600x1200 133 版本 <span id="sec-12-8"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1600_1200_133.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1600_1200_133.cpp
main/epd_display/epd_type_1600_1200_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType16001200_133_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType16001200_133_Display()
EpdType16001200_133_NT61522_Init()
EpdType16001200_133_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_1600_1200_133, width=1600, height=1200, display_size=960000, color=BWYRBG_6_Color
名称：EPD_1600_1200_133_XingTai
```

实现说明：

```text
- 帧数据通过 spiTransmitData() 发送，按 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072` 小分包，避免 PSRAM 源数据触发临时 DMA TX buffer 分配失败。首个发送错误立即中止MASTER/SLAVE数据装载并上报，本次不继续刷新。
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.8.1 epd_type_1600_1200_133_DKE：1600x1200 13.3 DKE 六色屏 <span id="sec-12-8-1"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1600_1200_133_DKE.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: EPD_TYPE_1600_1200_133_DKE dispatch
    DRV->>DRV: check display_size=960000
    DRV->>BSP: DKE init command sequence
    DRV->>BSP: write master frame 480000 bytes
    DRV->>BSP: write slave frame 480000 bytes
    DRV->>BSP: PON / DRF / POF update
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1600_1200_133_DKE.cpp
main/epd_display/epd_type_1600_1200_133_DKE.h
main/epd_display/epd_type.cpp
main/epd_display/display_bsp.h
main/epd_display/epd_display_app.cpp
tdx_esp32s3_web_flasher_file_input_v20/js/serial_protocol.js
```

树状时序：

```text
epd_type.cpp
└─ EpdType16001200_133_DKE_Display()
   ├─ 校验 display buffer / display_size=960000
   ├─ EpdType16001200_133_DKE_Init()
   │  └─ 使用工厂 EL133UF1 初始化参数
   ├─ EpdType16001200_133_DKE_NT61522_DisplayNet()
   │  ├─ MASTER 写入 image[0..479999]
   │  └─ SLAVE 写入 image[480000..959999]
   ├─ EpdType16001200_133_DKE_Update()
   │  ├─ R04_PON
   │  ├─ R12_DRF
   │  └─ R02_POF
   └─ display_bsp.cpp / ePaperPort
      ├─ setPinCs(TARGET_MASTER / TARGET_SLAVE / TARGET_BOTH)
      ├─ spiTransmitCommand()
      ├─ spiTransmitData()
      └─ Get_BusyIOLevel()
```

关键辅助函数：

```text
EpdType16001200_133_DKE_Display()
EpdType16001200_133_DKE_Init()
EpdType16001200_133_DKE_NT61522_DisplayNet()
EpdType16001200_133_DKE_Update()
EpdType16001200_133_DKE_Sleep()
EpdType16001200_133_DKE_WaitBusy()
```

屏幕配置：

```text
type=EPD_TYPE_1600_1200_133_DKE, width=1600, height=1200, display_size=960000, color=BWYRBG_6_Color
名称：EPD_1600_1200_133_DKE
```

实现说明：

```text
- DKE 13.3 与 EPD_TYPE_1600_1200_133 分开实现，不复用兴泰 13.3 的初始化参数，避免影响已验证成功的兴泰屏。
- DKE 参考工厂 EL133UF1.cpp / EPD_IO.cpp：初始化、分帧写入、PON/DRF/POF 更新和 sleep 参数独立维护。
- 图像总长度 960000 bytes；MASTER 和 SLAVE 各写 480000 bytes。
- DKE 帧数据按 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK=3072` 小分包调用 spiTransmitData()；轮播/投图数据常在 PSRAM，SPI driver 可能临时申请内部 DMA TX buffer，小分包可避免内部 DMA heap 碎片化时出现 `ESP_ERR_NO_MEM`。任一半帧写入失败时不继续 update，调用方不推进轮播进度。
- 调试日志会打印 init、master write、slave write、update、busy timeout 等步骤，便于确认卡在哪个阶段。
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须为 960000，否则拒绝显示。
- PC 端 serial_protocol.js 也需要有 type=10 的静态类型信息，方便离线显示下拉列表。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.9 epd_type_1360_480_1085：1360x480 四色屏 <span id="sec-12-9"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1360_480_1085.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1360_480_1085.cpp
main/epd_display/epd_type_4color_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType1360480_1085_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType1360480_1085_Display()
EpdType1360480_1085_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_1360_480_1085, width=1360, height=480, display_size=81600, color=BWRY_4_Color
名称：EPD_1360_480_1085_XingTai
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.10 epd_type_1360_480_1085_3color：1360x480 三色屏 <span id="sec-12-10"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_1360_480_1085_3color.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_1360_480_1085_3color.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType1360480_1085_3Color_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType1360480_1085_3Color_Display()
EpdType1360480_1085_3Color_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_1360_480_1085_3COLOR, width=1360, height=480, display_size=163200, color=BWR_3_Color
名称：EPD_1360_480_1085_3COLOR_YSGD
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.11 epd_type_4color_common：四色屏公共逻辑 <span id="sec-12-11"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_4color_common.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_4color_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType800480_4S_75_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType800480_4S_75_Display()
EpdType1360480_1085_Display()
EpdType800480_4S_75_DKE_Display()
```

屏幕配置：

```text
供 BWRY_4_Color 类型复用的公共显示逻辑。
公共文件本身不保存屏幕类型，也不对应单独 type。
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.12 epd_type_800_480_4s_75：800x480 4S 75 兴泰屏 <span id="sec-12-12"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_800_480_4s_75.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_800_480_4s_75.cpp
main/epd_display/epd_type_4color_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType800480_4S_75_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType800480_4S_75_Display()
EpdType800480_4S_75_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_800_480_4S_75, width=800, height=480, display_size=96000, color=BWRY_4_Color
名称：EPD_800_480_4S_75_XingTai
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.13 epd_type_800_480_4s_75_DKE：800x480 4S 75 DKE 屏 <span id="sec-12-13"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_800_480_4s_75_DKE.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_800_480_4s_75_DKE.cpp
main/epd_display/epd_type_4color_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType800480_4S_75_DKE_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType800480_4S_75_DKE_Display()
EpdType800480_4S_75_DKE_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_800_480_4S_75_2, width=800, height=480, display_size=96000, color=BWRY_4_Color
名称：EPD_800_480_4S_75_DKE
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.14 epd_type_800_480_4s_75_mofang：800x480 4S 75 墨方屏 <span id="sec-12-14"></span>

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant TYPE as epd_type.cpp
    participant DRV as epd_type_800_480_4s_75_mofang.cpp
    participant BSP as display_bsp/ePaperPort
    participant EPD as EPD Panel
    TYPE->>DRV: dispatch display/init/sleep
    DRV->>DRV: check display_size / prepare sequence
    DRV->>BSP: write CMD/DATA
    BSP->>EPD: SPI refresh
```

相关文件：

```text
main/epd_display/epd_type_800_480_4s_75_mofang.cpp
main/epd_display/epd_type_4color_common.cpp
```

树状时序：

```text
epd_type.cpp
└─ EpdType800480_4S_75_Mofang_Display()
   ├─ 校验 display buffer / display_size
   ├─ 调用对应 Init / Display / Sleep 流程
   └─ display_bsp.cpp / ePaperPort
      ├─ EPD_WriteCMD()
      ├─ EPD_WriteDATA()
      └─ EPD_Check_Busy()
```

关键辅助函数：

```text
EpdType800480_4S_75_Mofang_Display()
EpdType800480_4S_75_Mofang_NT61522_DisplayNet()
```

屏幕配置：

```text
type=EPD_TYPE_800_480_4S_75_3, width=800, height=480, display_size=96000, color=BWRY_4_Color
名称：EPD_800_480_4S_75_mofang
```

存 / 取信息（含条件限制）：

```text
存：
- 本驱动不写 SD/NVS；只向 EPD 控制器写命令和显示数据。
- 是否保存屏幕类型由 EpdType_SetAndSave() 统一处理，不在本文件直接保存。

取：
- 从 RAM buffer 读取待显示数据。
- 从 epd_type.cpp 当前 EPD_type 分发进入本驱动。
- display_size 必须与当前屏幕配置匹配，否则显示数据长度不可靠。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.15 epd_test_1360_480_1085_3color_const：1360x480 三色测试数据源 <span id="sec-12-15"></span>

实际返回码（正式定义见 `README_Result_Code.md`）：

| 返回 | result | 说明 |
|---|---|---|
| `test_epd_display_result` | `0` | 测试图显示驱动执行完成 |
| `test_epd_display_result` | `1801` | 当前 EPD 类型非法 |
| `test_epd_display_result` | `1803` | 测试显示失败 |

Mermaid 时序图：

```mermaid
sequenceDiagram
    participant USB as USB /epd_test
    participant EPDAPP as epd_display_app.cpp
    participant CONST as epd_test_1360_480_1085_3color_const.cpp/.h
    participant QUEUE as EPD Display Queue
    USB->>EPDAPP: test_epd_display()
    EPDAPP->>CONST: 读取 g_epd_test_* 常量数组和 size
    EPDAPP->>QUEUE: ServerNetworkStaEpdDisplay_Queue()
    QUEUE-->>EPDAPP: 后台显示任务刷新屏幕
```

相关文件：

```text
main/epd_display/epd_test_1360_480_1085_3color_const.cpp
main/epd_display/epd_test_1360_480_1085_3color_const.h
main/epd_display/epd_display_app.cpp
main/usb_console_echo/epd_type/usb_console_epd_type.c
```

树状时序：

```text
USB /epd_test
└─ UsbConsoleEpdType_HandleTest()
   └─ epd_display_app.cpp
      └─ test_epd_display()
         ├─ 读取 g_epd_test_1360_480_1085_3color_display_b[]
         ├─ 读取 g_epd_test_1360_480_1085_3color_display_r[]
         ├─ 读取 g_epd_test_1360_480_1085_3color_*_size
         └─ ServerNetworkStaEpdDisplay_Queue()
            └─ ServerNetworkStaEpdDisplay_Task()
               └─ 按当前 EPD 类型进入正常显示链路
```

关键辅助函数：

```text
test_epd_display()
UsbConsoleEpdType_HandleTest()
ServerNetworkStaEpdDisplay_Queue()
ServerNetworkStaEpdDisplay_Task()
```

说明：

```text
- 本文件不是普通 EPD 类型驱动 dispatch 文件。
- 它主要提供 1360x480 三色测试图的常量数据数组和 size。
- 真正的显示仍通过 epd_display_app.cpp 的测试入口投递到 EPD 显示队列。
- 普通屏幕类型分发仍由 epd_type.cpp / epd_type_*.cpp 负责。
- test_epd_display() 已包含 EPD_TYPE_1600_1200_133_DKE，用于验证 DKE 13.3 新驱动的 init/write/update 流程。
```

存 / 取信息（含条件限制）：

```text
存：
- 本文件不写 SD/NVS，也不保存屏幕类型。
- 常量测试图数据编译进固件镜像，属于只读程序数据。

取：
- 测试入口读取 g_epd_test_1360_480_1085_3color_display_b[]。
- 测试入口读取 g_epd_test_1360_480_1085_3color_display_r[]。
- 测试入口读取 g_epd_test_1360_480_1085_3color_plane_size / image_size。
- 显示前仍需要当前 EPD 类型、显示队列和底层 EPD 驱动可用。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

### 12.16 EPD/SD 共用电源独立测试 <span id="sec-12-16"></span>

相关文件：

```text
main/epd_sd_power_test/epd_sd_power_test.c
main/epd_sd_power_test/epd_sd_power_test.h
main/epd_display/display_bsp.cpp
main/epd_display/display_bsp.h
main/epd_display/epd_display_app.cpp
main/epd_display/epd_display_app.h
main/tdx_shared_spi.c
main/mount.c
main/tdx_cfg.h
main/server_network_sta/slideshow/server_network_sta_slideshow.c
```

该功能是 GPIO4 EPD/SD 共用电源的独立测试状态机，不复用 `wifi_work_time`，也不发送 CH583 `POWER_OFF`。只有实际 EPD 显示任务完成后才 armed；启动后未发生 EPD 显示时不执行测试。EPD 队列仍有任务、HTTP 请求未结束、图片 show/save 事务未结束、轮播显示后的进度保存或下一张 SD 预读未结束、普通共享 SPI 请求正在执行或等待时均不关电。启动存储类型未知时测试不启动，原有业务继续运行。

网络 cast、cast2pic、upload 和 USB 共用的 `TdxImageTransfer_ProcessItems()` 使用独立引用计数覆盖完整的“先 show、后 save”事务，避免 EPD 完成与保存任务投递之间的短暂空闲被误判。轮播没有请求字段 `show=true/save=true`，使用单独的 slideshow follow-up 引用计数：在同步 EPD 显示前登记，在显示完成后的进度保存和下一张 SD BIN 预读结束后释放，避免轮播任务调度空隙产生约几十毫秒的无效短促断电；释放后若其他安全条件均满足，独立状态机再执行完整的 2 秒断电测试。

安全断电流程：

```text
EPD job done
-> wait EPD queue idle
-> wait image transfer count = 0
-> wait slideshow follow-up count = 0
-> test-only zero-wait shared SPI lock
-> verify no normal SPI request is active or waiting
-> external SD: unmount while holding the test lock
-> SPIFFS: skip all storage unmount/remount operations
-> recheck activity generation
-> remove the EPD SPI device and free the shared SPI bus
-> set GPIO0/1/6/7/8/9/10/25/26 to input with all internal pulls disabled
-> recheck all activity counters and the event generation after IO isolation
-> GPIO4 LOW last
-> release test lock
-> interruptible wait, maximum 2000 ms
-> GPIO4 HIGH first
-> wait 100 ms for shared-rail stabilization
-> restore EPD control pins and rebuild the shared SPI bus/device
-> external SD: use the test-only remount path
-> release blocked normal SPI callers
```

普通 `TdxSharedSpi_Lock()` 在测试 armed、preparing、off 或 restoring 状态下会先登记请求并通知测试模块。登记发生在 mutex take 之前，因此已经等待 SPI 的 SD/EPD任务也会阻止断电。GPIO4 已关闭时，普通 SPI 调用等待电源开启；使用外部 SD 时还会等待 SD 重新挂载，请求不会因测试被丢弃。状态机使用单独的 `TdxSharedSpi_LockForEpdSdPowerTest()`，避免最终检查把自身误报为新 SPI 活动。关闭 `USER_EPD_SD_POWER_TEST_ENABLE` 时，普通 SPI 锁不执行测试计数或电源等待。

HTTP handler 使用 Begin/End 引用计数覆盖完整请求周期；请求到达时若 GPIO4 已关闭，会等待电源恢复后再进入原 handler。提前恢复条件：任何 HTTP 业务请求、CH583 发来的合法 `BLE_DATA`、新 EPD 请求或普通共享 SPI 请求。ESP32 发给 CH583 的命令以及 CH583 发来的其他命令都不影响本测试。2000 ms 内没有事件时也会自动恢复。所有配置、事件位和状态值统一定义在 `tdx_cfg.h` 的 `USER_EPD_SD_POWER_TEST_*` 宏中。

`USER_EPD_SD_POWER_TEST_IO_ISOLATION_ENABLE=1` 时，断电测试通过专用 EPD C 接口释放 SPI 外设路由，并将外部电源域相关 IO 切换为无上下拉的输入高阻，避免 GPIO 反向给 EPD 或 SD 供电。恢复时先开启 GPIO4，等待 100 ms，再恢复控制脚和 EPD 所有的共享 SPI 总线。公共 `Set_Power()` 仍只控制 GPIO4，不改变普通 EPD 初始化、复位和显示流程；测试路径不使用 `gpio_reset_pin()`，避免默认上拉造成反向供电。

外部 SD 测试恢复使用 `example_storage_remount_sd_for_epd_power_test()`，不调用启动入口 `example_mount_storage()`。恢复顺序是先恢复 EPD 共享 SPI，再重新挂载 SD。ESP-IDF 卸载接口一旦被调用，测试模块不再保留旧 card handle；即使卸载最后返回 VFS 清理错误，也保持普通 SPI 请求阻塞并进入现有重挂载重试路径。恢复失败时 GPIO4 保持 HIGH，状态机限频记录错误并定时重试；不修改启动挂载失败计数、不调用 `esp_restart()`、不回退 SPIFFS、不扫描目录。当前存储为 SPIFFS 时，内部 SPIFFS 不卸载也不重新挂载，但 EPD IO 仍按相同顺序隔离和恢复。

存 / 取信息（含条件限制）：

```text
存：
- 状态、事件代数、HTTP/图片事务/轮播后续 SD 工作引用计数，以及 EPD SPI 总线/IO 恢复需求状态只保存在 RAM，不写 NVS。
- 断电前卸载 SD；恢复供电后重新建立 SD card 和 FAT/VFS 挂载状态。

取：
- 读取 EPD 队列/执行 busy 状态。
- 读取普通共享 SPI 请求计数，覆盖正在执行和等待 mutex 的任务。
- 读取测试模块事件通知；只有校验通过的 CH583 `BLE_DATA` 作为 CH583 提前恢复事件。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-12)

---

## 14. 文件服务器静态资源与旧接口 <span id="sec-14"></span>

Mermaid 路由图：

```mermaid
flowchart TD
    A[example_start_file_server] --> B[GET /]
    A --> C[GET /favicon.ico]
    A --> D[GET static file]
    A --> E[legacy upload/delete]
    A --> F[register new POST /dataUP /ota /ota_upload]
```


相关文件：

```text
main/file_server.c
main/upload_script.html
main/favicon.ico
main/server_network_sta/index_html.h
```

树状时序：

```text
example_start_file_server()
├─ index_html_get_handler()
│  └─ send embedded/index html
├─ favicon_get_handler()
├─ download_get_handler()
│  ├─ get_path_from_uri()
│  ├─ set_content_type_from_file()
│  └─ httpd_resp_send_chunk()
├─ upload_post_handler()
│  └─ legacy raw file upload
├─ delete_post_handler()
│  └─ legacy delete
└─ server_network_sta_net_data_register_handlers()
   └─ new POST /dataUP /ota /ota_upload handlers

GET /ping
└─ download_get_handler()
   └─ ServerNetworkStaPing_ProcessGet() 优先拦截

GET /time
└─ download_get_handler()
   └─ ServerNetworkStaTime_ProcessGet() 优先拦截
```

---




存 / 取信息（含条件限制）：

```text
存：
- legacy upload_post_handler() 会把上传文件写入 /data 下目标路径。
- delete_post_handler() 删除 /data 下文件。

取：
- download_get_handler() 根据 URI 读取 /data 下静态文件并分块返回。
- index_html_get_handler() / favicon_get_handler() 读取固件内嵌资源并返回。
- 缩略图由 saved_images 模块按 /thumb 路径读取 jpg。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-14)

---

## 16. 目录到功能索引 <span id="sec-16"></span>

Mermaid 目录功能图：

```mermaid
flowchart TD
    A[main] --> B[server_network_sta]
    A --> C[usb_console_echo]
    A --> D[ch583_uart]
    A --> E[ble]
    A --> F[epd_display]
    A --> G[led_status]
    A --> H[mount.c / app_nvs.c]
```


```text
main/
├─ main.c
│  └─ 系统启动、任务初始化、模块总入口
├─ tdx_cfg.h
│  └─ C5 板级参数、协议限制、NVS key、功能开关
├─ app_nvs.c
│  └─ 小型 NVS 读写封装
├─ mount.c
│  └─ SD/SPIFFS 挂载、目录创建、容量查询
├─ file_server.c
│  └─ HTTP server、静态文件、旧上传/删除入口、新网络接口注册
├─ epd_sd_power_test/
│  └─ EPD任务完成后执行 GPIO4 EPD/SD 共用电源的独立2秒断电测试
├─ server_network_sta/
│  ├─ server_network_sta.c
│  │  └─ WiFi STA、mDNS、HTTP server 启动
│  ├─ net_data/
│  │  └─ /dataUP /ota /small JSON 统一入口
│  ├─ cast/
│  │  └─ 网络 cast 投图
│  ├─ cast2pic/
│  │  └─ 网络双屏/分屏投图
│  ├─ upload/
│  │  └─ 网络通用上传
│  ├─ ota/
│  │  └─ 网络 OTA
│  ├─ delete/
│  │  └─ 删除图片和关联状态
│  ├─ saved_images/
│  │  └─ 已保存图片列表
│  ├─ snapshot/
│  │  └─ 当前图片/轮播状态快照
│  ├─ slideshow/
│  │  └─ 轮播配置与后台任务
│  ├─ slideshow_control/
│  │  └─ 轮播开关/间隔控制
│  ├─ wifi_work_time/
│  │  └─ WiFi 工作时间、CH583 POWER_OFF
│  ├─ ping/
│  │  └─ 网络心跳
│  └─ time/
│     └─ RTC 默认时间、SNTP 网络校时、GET /time
├─ usb_console_echo/
│  └─ USB Serial/JTAG HTTP-like 请求、路由和响应
├─ ch583_uart/
│  └─ CH583 UART V1 协议、DEVICE_INFO/BLE_DATA/PING/GPIO/POWER_OFF
├─ ble/
│  └─ CH583 BLE JSON 分发；可选 ESP32 本机 BLE stub/实现
├─ epd_display/
│  ├─ epd_display_app.cpp：EPD 显示队列与任务
│  ├─ epd_type.cpp：屏幕类型表、NVS 读取/保存、驱动分发
│  ├─ display_bsp.cpp：SPI/GPIO 底层适配
│  ├─ epd_type_800_480.cpp：800x480 三色屏
│  ├─ epd_type_1024_600.cpp：1024x600 六色屏
│  ├─ epd_type_1600_1200_common.cpp：1600x1200 公共逻辑
│  ├─ epd_type_1600_1200_79.cpp：1600x1200 79 版本
│  ├─ epd_type_1600_1200_133.cpp：1600x1200 133 版本
│  ├─ epd_type_1360_480_1085.cpp：1360x480 四色屏
│  ├─ epd_type_1360_480_1085_3color.cpp：1360x480 三色屏
│  ├─ epd_type_4color_common.cpp：四色屏公共逻辑
│  ├─ epd_type_800_480_4s_75.cpp：800x480 4S 75 兴泰屏
│  ├─ epd_type_800_480_4s_75_DKE.cpp：800x480 4S 75 DKE 屏
│  ├─ epd_type_800_480_4s_75_mofang.cpp：800x480 4S 75 墨方屏
│  └─ epd_test_1360_480_1085_3color_const.cpp：1360x480 三色测试图
└─ led_status/
   └─ CH583 PB5/PB6 状态灯控制
```


存 / 取信息（含条件限制）：

```text
存：
- 本节是目录索引，不执行存储。

取：
- 按目录查找各源码文件对应功能。
```

[⬆ 返回目录](#toc) | [↩ 返回当前目录](#sec-16)

---

## 按功能整理的相关文件与辅助函数 <span id="moved-code-index"></span>

公共测试设置

相关文件：

```text
main/app_nvs.c
main/tdx_cfg.h
main/persistent_state/app_persistent_state.c
main/persistent_state/app_persistent_state.h
main/epd_display/epd_display_mode.c
main/epd_display/epd_display_mode.h
```

---

公共测试设置

相关文件：

```text
main/mount.c
main/file_serving_example_common.h
```

---

公共测试设置

关键辅助函数：

```text
receive_data_redirect_handler()
├─ get_request_header_value()
├─ ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity()
├─ NetworkOtaUpload_IsOtaRequest()
├─ alloc_request_body_buffer()
├─ read_request_body_to_buffer()
├─ process_small_json_request()
└─ server_network_sta_net_data_register_handlers()
```

---

### 7.1 cast：投屏业务模块

相关文件：

```text
main/server_network_sta/cast/server_network_sta_cast.c
main/server_network_sta/cast/server_network_sta_cast.h
main/cast_core/cast_core.c
main/cast_core/cast_core.h
main/epd_display/epd_display_app.cpp
```

---

### 7.1 cast：投屏业务模块

关键辅助函数：

```text
server_network_sta_cast.c
├─ send_cast_received()
├─ send_cast_result()
├─ cast_async_process()
└─ ServerNetworkStaCast_Process()

cast_core.c
├─ TdxCastCore_ParseAndValidate()
├─ TdxCastCore_ProcessValidatedCastDir()
├─ CastSaveTask()
├─ stop_slideshow_for_cast()
└─ record_last_cast()
```

---

### 7.2 cast2pic：投屏转图片缓存 / 显示

相关文件：

```text
main/server_network_sta/cast2pic/server_network_sta_cast2pic.c
main/server_network_sta/cast2pic/server_network_sta_cast2pic.h
main/cast_core/cast_core.c
main/cast_core/cast_core.h
```

---

### 7.2 cast2pic：投屏转图片缓存 / 显示

关键辅助函数：

```text
server_network_sta_cast2pic.c
├─ extract_boundary()
├─ parse_cast2pic_multipart()
├─ assign_text_part()
├─ assign_image_part()
├─ validate_cast2pic_meta()
├─ screen_to_epd_number()
├─ process_cast2pic_items()
├─ cast2pic_async_process()
└─ ServerNetworkStaCast2Pic_Process()

cast_core.c
├─ TdxImageTransfer_ProcessItems()
└─ CastSaveTask()
```

---

### 7.3 delete：图片 / 缓存文件删除逻辑

相关文件：

```text
main/server_network_sta/delete/server_network_sta_delete.c
main/server_network_sta/delete/server_network_sta_delete.h
```

---

### 7.3 delete：图片 / 缓存文件删除逻辑

关键辅助函数：

```text
server_network_sta_delete.c
├─ parse_file_names()
├─ delete_one_path()
└─ ServerNetworkStaDelete_ProcessJson()
```

---

### 7.4 net_data：通用网络数据封装

相关文件：

```text
main/server_network_sta/net_data/server_network_sta_data.c
main/server_network_sta/net_data/server_network_sta_data.h
```

---

### 7.4 net_data：通用网络数据封装

关键辅助函数：

```text
server_network_sta_data.c
├─ log_heap_watermark()
├─ alloc_request_body_buffer()
├─ read_request_body_to_buffer()
├─ process_small_json_request()
├─ receive_data_redirect_handler()
└─ server_network_sta_net_data_register_handlers()
```

---

### 7.5 ota：设备在线升级模块

相关文件：

```text
main/server_network_sta/ota/network_ota_upload.c
main/server_network_sta/ota/network_ota_upload.h
main/server_network_sta/ota/network_ota_boot.c
main/server_network_sta/ota/network_ota_boot.h
```

---

### 7.5 ota：设备在线升级模块

关键辅助函数：

```text
network_ota_upload.c
├─ NetworkOtaUpload_IsOtaRequest()
├─ NetworkOtaUpload_IsRestartPending()
├─ NetworkOtaUpload_GetMaxBodySize()
├─ NetworkOtaUpload_ProcessReceivedBody()
├─ schedule_ota_restart()
├─ ota_restart_task()
└─ write_firmware_to_ota_partition()

network_ota_boot.c
├─ NetworkOtaBoot_Init()
├─ NetworkOtaBoot_WasPendingVerify()
├─ NetworkOtaBoot_IsPendingVerify()
├─ NetworkOtaBoot_EnablePendingProtection()
└─ NetworkOtaBoot_ConfirmCurrentImage()
```

成功 OTA 的 `rebooting` event 位于最终 `ota_result` 之前。chunked response 结束后，处理函数返回到网络数据入口完成 body、mutex 和 socket 清理；随后 OTA 专用任务延时复位。restart-pending 状态使用原子读写；网络入口用它保留成功指示并拒绝重启前的新 multipart 上传，GET 和非 multipart 路径不变。

`network_ota_boot` 直接读取 bootloader `otadata`，仅把 `ESP_OTA_IMG_PENDING_VERIFY` 视为 OTA 首次启动标志，不增加应用 NVS key。该模块保存本次启动和当前待确认两个原子 RAM 状态，输出一次关键启动诊断，并通过 `USER_WORK_STATE_OTA_HOLD_PENDING_VERIFY_BIT` 阻止自动关机。启动最早阶段读取失败时，`main.c` 在 work-time 初始化后、GPIO test 前调用 `NetworkOtaBoot_Init()` 重试一次；调用方不重复打印模块已经报告的读取错误。

`NetworkOtaBoot_ConfirmAfterLocalInit()` 在网络管理对象及 CH583 UART 初始化成功后、现有500ms CH583时间同步等待之前执行。该位置不等待 WiFi、DHCP、SNTP、HTTP、SD或DAILY，目标是在外部10秒关电窗口内完成 `esp_ota_mark_app_valid_cancel_rollback()`。确认成功清除 pending-verify hold；确认失败使用 `ESP_LOGE`并继续安全启动。`NetworkOtaBoot_ConfirmCurrentImage()` 仍在 `/dataUP`、`/ota`、`/ota_upload` 全部注册成功后执行，作为本地确认失败时的容错重试。两个入口共用 OTA模块内部确认实现，并在每次确认前重新读取权威otadata状态。

普通启动不启用上述保护。只有本次从 `PENDING_VERIFY` 启动时，`GpioTest_Init()` 和 `FactoryReset_Init()` 失败才降级为记录错误并继续；其他初始化规则不变。当前 `app_auto_light_sleep_init()` 固定配置 `light_sleep_enable=false`，pending 状态只增加一次明确提示。

---

### 7.6 ping：网络连通检测

相关文件：

```text
main/server_network_sta/ping/server_network_sta_ping.c
main/server_network_sta/ping/server_network_sta_ping.h
main/epd_display/epd_display_app.cpp
main/epd_display/epd_display_app.h
```

---

### 7.6 ping：网络连通检测

关键辅助函数：

```text
server_network_sta_ping.c
└─ ServerNetworkStaPing_ProcessGet()
epd_display_app.cpp
└─ ServerNetworkStaEpdDisplay_IsBusy()
```

---

### 7.7 get_saved_images：取出本地存储图片

相关文件：

```text
main/server_network_sta/saved_images/server_network_sta_saved_images.c
main/server_network_sta/saved_images/server_network_sta_saved_images.h
```

---

### 7.7 get_saved_images：取出本地存储图片

关键辅助函数：

```text
server_network_sta_saved_images.c
├─ json_func_equals()
├─ saved_image_entry_name()
├─ has_jpg_extension()
├─ saved_image_name_is_safe()
├─ send_saved_images_empty()
├─ ServerNetworkStaSavedImages_SendThumbnail()
└─ ServerNetworkStaSavedImages_ProcessJson()
```

---

### 7.8 slideshow：图片轮播的文件列表，轮播间隔，是否随机

相关文件：

```text
main/server_network_sta/slideshow/server_network_sta_slideshow.c
main/server_network_sta/slideshow/server_network_sta_slideshow.h
```

轮播容量与状态参考：

- `TDX_SLIDESHOW_MAX_FILES=150`；该常量只控制轮播，`TDX_DELETE_MAX_FILES` 仍为 50。
- `TDX_IMAGE_BASE_NAME_MAX_BYTES=16` 是 APP/USB 业务基础文件名上限；`TDX_IMAGE_BASE_NAME_BUFFER_SIZE=17` 包含字符串结尾，`TDX_SLIDESHOW_FILE_NAME_MAX_LEN` 是该缓冲区大小的兼容别名。multipart 原始 `filename` 仍使用 `SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX=96`，以便先保留完整输入再拒绝超长业务名；legacy multipart fallback 按去掉匹配的 `.bin/.jpg` 扩展名后的基础名执行相同上限。
- `slideshow_progress_t.order_count`、`position` 和 `order[]` 使用 `uint8_t`，索引 `0..149` 在当前范围内安全；如果未来超过 255，必须先调整这些字段及配置 hash 中的数量编码。
- APP 在 `random=true` 时负责将每个原始文件复制 3 次并打乱；ESP32-C5 允许最终列表重复、不再次随机，保存状态仍为 `random=false`。
- `startIndex` 始终索引 APP 最终发送的列表，校验条件为 `startIndex < file_count`，不得再次乘 3。
- 允许重复后，SNTP 开机恢复使用 `progress.order[progress.position]` 与目标索引比较，并同时核对 `pending_file`，不能只按文件名判断。
- `slideshow_progress_t` 随名称缓冲区缩小而改变 blob 大小，progress version 同步升级；旧 NVS `slide_progress` 会因大小/版本不匹配由现有流程按配置重建，不需要迁移或主动删除。本地图片浏览持久化状态版本也同步升级并重建旧尺寸状态。
- 网络 small JSON 仍受 `SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX=4096` 限制，`slideshow_config` 生成缓冲仍为 `SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX=8192`；150 个当前常用的 16 位名称在这两个边界内，长名称组合必须单独核对请求总长度。

---

### 7.8 slideshow：图片轮播的文件列表，轮播间隔，是否随机

关键辅助函数：

```text
server_network_sta_slideshow.c
├─ parse_start_slideshow_request()
├─ check_slideshow_files_exist()
├─ save_slideshow_persistent_state()
├─ ServerNetworkStaSlideshow_ShowFirst()
├─ ServerNetworkStaSlideshow_StartSaved()
├─ ServerNetworkStaSlideshow_StartSavedResetInterval()
├─ ServerNetworkStaSlideshow_StartSavedForNewCommand()
├─ ServerNetworkStaSlideshow_StartSavedDelayed()
├─ ServerNetworkStaSlideshow_GetRuntimeTiming()
├─ ServerNetworkStaSlideshow_Stop()
├─ ImageBusinessWorker_Submit(SLIDESHOW)（提交给9KB统一常驻worker）
└─ slideshow_run_runtime()（由统一worker串行执行，结束后释放 runtime）
```

`main/image_business_worker/` 提供一个固定9KB内部RAM静态栈、一个静态TCB、一个静态状态mutex和一个640字节单pending命令槽。该worker在读取 `epd_mode` 后立即创建并永久常驻，串行执行每日一图、轮播runtime和轮播启动延迟；旧6KB轮播静态任务、旧7KB daily静态任务及6KB动态启动延迟任务均已取消。轮播runtime仍优先从PSRAM分配约2.9KB，结束或在pending阶段被取消时只释放一次。启动延迟使用统一worker的task notification进行可中断等待，模式切换会增加generation并立即唤醒，禁止过期延迟在10秒后重新启动旧轮播。新命令失败、开机恢复和CH583模式协调规则保持不变。

---

### 7.9 slideshow_control：轮播控制模块

相关文件：

```text
main/server_network_sta/slideshow_control/server_network_sta_slideshow_control.c
main/server_network_sta/slideshow_control/server_network_sta_slideshow_control.h
```

---

### 7.9 slideshow_control：轮播控制模块

关键辅助函数：

```text
server_network_sta_slideshow_control.c
├─ parse_json_bool_optional()
├─ parse_json_u32()
├─ parse_json_i64()
├─ timestamp_reasonable()
├─ ServerNetworkStaSlideshowControl_ApplyJson()
├─ write_control_state()
├─ rollback_slideshow_control_failure()
└─ ServerNetworkStaSlideshowControl_ProcessJson()
```

---

### 7.10 snapshot：读取图片列表和轮播状态

相关文件：

```text
main/server_network_sta/snapshot/server_network_sta_snapshot.c
main/server_network_sta/snapshot/server_network_sta_snapshot.h
```

---

### 7.10 snapshot：读取图片列表和轮播状态

关键辅助函数：

```text
server_network_sta_snapshot.c
├─ append_images_json()
├─ read_slideshow_state()
├─ append_slideshow_json()
└─ ServerNetworkStaSnapshot_ProcessJson()
```

---

### 7.11 upload：PC或手机传文件到ESP32-C5，并存

相关文件：

```text
main/server_network_sta/upload/server_network_sta_upload.c
main/server_network_sta/upload/server_network_sta_upload.h
main/cast_core/cast_core.c
main/cast_core/cast_core.h
```

---

### 7.11 upload：PC或手机传文件到ESP32-C5，并存

关键辅助函数：

```text
server_network_sta_upload.c
├─ send_upload_result()
├─ upload_async_process()
└─ ServerNetworkStaUpload_Process()

cast_core.c
├─ TdxImageTransfer_ParseSingle()
├─ TdxImageTransfer_ProcessItems()
└─ CastSaveTask()
```

---

### 7.12 wifi_work_time：WiFi 省电管理

相关文件：

```text
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.c
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.h
```

---

### 7.12 wifi_work_time：WiFi 省电管理

关键辅助函数：

```text
server_network_sta_wifi_work_time.c
├─ ServerNetworkStaWifiWorkTime_Init()
├─ ServerNetworkStaWifiWorkTime_OnNetworkData()
├─ ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity()
├─ ServerNetworkStaWifiWorkTime_OnCh583Activity()
├─ ServerNetworkStaWifiWorkTime_OnCh583Initialized()
├─ ServerNetworkStaWifiWorkTime_ProcessJson()
├─ ServerNetworkStaWifiWorkTime_SetAndSave()
├─ ServerNetworkStaWifiWorkTime_SetOtaWriteInProgress()
├─ ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress()
├─ configure_ch583_wake_timer_before_power_off()
└─ work_state_task()
```

---

### 7.13 time：RTC 默认时间与 SNTP 网络校时

相关文件：

```text
main/server_network_sta/time/server_network_sta_time.c
main/server_network_sta/time/server_network_sta_time.h
main/file_server.c
main/main.c
sdkconfig
sdkconfig.defaults.esp32c5
```

---

公共测试设置

相关文件：

```text
main/usb_console_echo/usb_console_echo.c
main/usb_console_echo/transport/usb_console_transport.c
main/usb_console_echo/http_text/usb_console_http_text.c
main/usb_console_echo/router/usb_console_router.c
main/usb_console_echo/worker/usb_console_worker.c
main/usb_console_echo/common/usb_console_common.c
main/usb_console_echo/common/usb_console_common_async.c
main/usb_console_echo/ping/usb_console_ping.c
main/usb_console_echo/restart/usb_console_restart.c
main/usb_console_echo/wifi/usb_console_wifi.c
main/usb_console_echo/epd_type/usb_console_epd_type.c
main/usb_console_echo/net_data/usb_console_net_data.c
main/usb_console_echo/cast/usb_console_cast.c
main/usb_console_echo/cast/usb_console_cast_worker.c
main/usb_console_echo/cast2pic/usb_console_cast2pic.c
main/usb_console_echo/delete/usb_console_delete.c
main/usb_console_echo/saved_images/usb_console_saved_images.c
main/usb_console_echo/slideshow/usb_console_slideshow.c
main/usb_console_echo/slideshow_control/usb_console_slideshow_control.c
main/usb_console_echo/snapshot/usb_console_snapshot.c
main/usb_console_echo/upload/usb_console_upload.c
main/usb_console_echo/wifi_work_time/usb_console_wifi_work_time.c
```

---

公共测试设置

关键辅助函数：

```text
UsbConsoleEcho_Init()
UsbConsoleEcho_Task()
UsbConsoleTransport_Init()
UsbConsoleTransport_Read()
UsbConsoleHttp_TryParseRequest()
UsbConsoleRouter_Handle()
UsbConsoleHttp_SendResponse()
```

---

### 9.3 common：公共工具、通用函数、全局定义

相关文件：

```text
main/usb_console_echo/common/usb_console_common.c
main/usb_console_echo/common/usb_console_common.h
main/usb_console_echo/common/usb_console_common_async.c
main/cast_core/cast_core.c
main/cast_core/cast_core.h
```

---

### 9.3 common：公共工具、通用函数、全局定义

关键辅助函数：

```text
UsbConsoleCommon_ExtractBoundary()
UsbConsoleCommon_MultipartParts()
UsbConsoleCommon_FileNameIsSafe()
TdxImageTransfer_ParseSingle()
TdxImageTransfer_ProcessItems()
CastSaveTask()
UsbConsoleCommon_ListSavedImages()
UsbConsoleCommon_AppendSnapshot()
UsbConsoleCommon_SetJsonf()
```

---

### 9.5 epd_type：电子墨水屏（EPD）屏幕类型适配、驱动适配

相关文件：

```text
main/usb_console_echo/epd_type/usb_console_epd_type.c
main/usb_console_echo/epd_type/usb_console_epd_type.h
main/epd_display/epd_type.cpp
main/epd_display/epd_display_app.cpp
```

---

### 9.5 epd_type：电子墨水屏（EPD）屏幕类型适配、驱动适配

关键辅助函数：

```text
UsbConsoleEpdType_SendList()
UsbConsoleEpdType_SendCurrent()
UsbConsoleEpdType_HandleSet()
UsbConsoleEpdType_HandleTest()
EpdType_SetAndSave()
EpdType_GetCurrentConfig()
```

---

### 9.6 http_text：HTTP-like 文本请求解析

相关文件：

```text
main/usb_console_echo/http_text/usb_console_http_text.c
main/usb_console_echo/http_text/usb_console_http_text.h
```

---

### 9.6 http_text：HTTP-like 文本请求解析

关键辅助函数：

```text
UsbConsoleHttp_TryParseRequest()
UsbConsoleHttp_SetJson()
UsbConsoleHttp_SendResponse()
```

---

### 9.7 net_data：USB /dataUP 与网络功能复用入口

相关文件：

```text
main/usb_console_echo/net_data/usb_console_net_data.c
main/usb_console_echo/net_data/usb_console_net_data.h
main/usb_console_echo/router/usb_console_router.c
```

---

### 9.7 net_data：USB /dataUP 与网络功能复用入口

关键辅助函数：

```text
UsbConsoleNetData_Handle()
UsbConsoleCommon_JsonFuncEquals()
TdxImageTransfer_ProcessItems()
UsbConsoleRouter_Handle()
```

---

### 9.9 router：USB HTTP-like 路由、API 分发

相关文件：

```text
main/usb_console_echo/router/usb_console_router.c
main/usb_console_echo/router/usb_console_router.h
main/usb_console_echo/restart/usb_console_restart.c
main/usb_console_echo/restart/usb_console_restart.h
```

---

### 9.9 router：USB HTTP-like 路由、API 分发

关键辅助函数：

```text
UsbConsoleRouter_Handle()
path_is()
usb_request_requires_storage()
usb_request_is_set_epd_type()
example_storage_get_type()
UsbConsoleHttp_SendResponse()
```

---

### 9.14 transport：USB Serial/JTAG 底层传输封装

相关文件：

```text
main/usb_console_echo/transport/usb_console_transport.c
main/usb_console_echo/transport/usb_console_transport.h
main/usb_console_echo/usb_console_echo.c
```

关键辅助函数：

```text
UsbConsoleTransport_Init()
UsbConsoleTransport_FlushRx()
UsbConsoleTransport_Read()
UsbConsoleTransport_WriteAll()
```

---

### 9.16 wifi：USB WiFi 配网请求解析与 NVS 保存

相关文件：

```text
main/usb_console_echo/wifi/usb_console_wifi.c
main/usb_console_echo/wifi/usb_console_wifi.h
main/usb_console_echo/worker/usb_console_worker.c
main/server_network_sta/server_network_sta.c
```

---

### 9.16 wifi：USB WiFi 配网请求解析与 NVS 保存

关键辅助函数：

```text
UsbConsoleWifi_Handle()
save_wifi_namespace()
save_net80211_namespace()
UsbConsoleWorker_SubmitWifiConnect()
User_Network_mode_app_init()
```

---

### 9.18 worker：USB 后台任务与异步工作队列

相关文件：

```text
main/usb_console_echo/worker/usb_console_worker.c
main/usb_console_echo/worker/usb_console_worker.h
main/usb_console_echo/cast/usb_console_cast_worker.c
```

---

### 9.18 worker：USB 后台任务与异步工作队列

关键辅助函数：

```text
UsbConsoleWorker_Init()
UsbConsoleWorker_SubmitWifiConnect()
UsbConsoleWorker_Task()
handle_wifi_connect()
UsbConsoleCast_SubmitAsync()
cast_worker_job()
```

---

### 9.19 restart：复位重起 ESP32-C5

相关文件：

```text
main/usb_console_echo/restart/usb_console_restart.c
main/usb_console_echo/restart/usb_console_restart.h
main/usb_console_echo/router/usb_console_router.c
tdx_esp32s3_web_flasher_file_input_v20/serial_protocol.html
tdx_esp32s3_web_flasher_file_input_v20/js/serial_protocol.js
```

---

公共测试设置

相关文件：

```text
main/ch583_uart/ch583_uart_app.c
main/ch583_uart/ch583_uart_app.h
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_wifi_uart_protocol.h
main/ble/ble_data_handler.cpp
main/server_network_sta/server_network_sta.c
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.c
main/led_status/led_status.c
```

---

公共测试设置

关键辅助函数：

```text
ch583_wifi_uart_process_bytes()
ch583_wifi_parse_frame()
ch583_wifi_validate_len_and_part()
ch583_wifi_handle_frame_body()
ch583_wifi_parse_device_info_arg()
ch583_wifi_handle_device_info()
ch583_wifi_handle_ble_data()
ch583_wifi_send_frame()
ch583_wifi_send_ack()
ch583_wifi_send_err()
ch583_wifi_uart_send_wifi_data()
ch583_wifi_uart_get_ble_ver()
ch583_wifi_uart_send_wifi_ver()
ch583_wifi_uart_send_wifi_provision_status()
ch583_wifi_uart_send_wake_timer_on()
ch583_wifi_uart_send_wake_timer_off()
ch583_wifi_uart_send_power_off()
ch583_wifi_uart_send_gpio()
```

---

### 10.1 通讯基础与帧格式

相关文件：

```text
main/ch583_uart/ch583_uart_app.c
main/ch583_uart/ch583_wifi_uart_protocol.c
main/tdx_cfg.h
```

---

### 10.1 通讯基础与帧格式

关键辅助函数：

```text
Ch583UartApp_Init()
User_UartEventTask()
Ch583Uart_ReadAndProcess()
ch583_wifi_uart_process_bytes()
ch583_wifi_handle_frame_body()
ch583_wifi_send_frame()
```

---

### 10.2 SEQ / LEN / PART / CRC / ACK / ERR 校验规则

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_wifi_uart_protocol.h
```

---

### 10.2 SEQ / LEN / PART / CRC / ACK / ERR 校验规则

关键辅助函数：

```text
ch583_wifi_crc16_ccitt_false()
ch583_wifi_parse_frame()
ch583_wifi_validate_len_and_part()
ch583_wifi_send_ack()
ch583_wifi_send_err()
ch583_wifi_handle_reply_status()
```

---

### 10.3 DEVICE_INFO 设备信息同步、版本交换与 EPD 类型

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/epd_display/epd_type.cpp
main/app_nvs.c
main/tdx_cfg.h
```

---

### 10.3 DEVICE_INFO 设备信息同步、版本交换与 EPD 类型

关键辅助函数：

```text
ch583_wifi_handle_device_info()
ch583_wifi_parse_device_info_arg()
ch583_wifi_is_upper_hex_string()
ch583_wifi_parse_u8_dec_arg()
ch583_wifi_parse_hex_byte()
app_nvs_write_str()
app_nvs_write_u8()
EpdType_SaveForNextBoot()
ch583_wifi_send_ack()
ch583_wifi_uart_get_ble_mac()
ch583_wifi_uart_get_ble_ver()
ServerNetworkStaTime_BackupCurrentToCh583()
```

---

### 10.4 PING / PONG 心跳流程

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_uart_app.c
```

---

### 10.4 PING / PONG 心跳流程

关键辅助函数：

```text
ch583_wifi_handle_frame_body()
ch583_wifi_send_frame()
ch583_wifi_write_frame_text()
uart_write_bytes()
uart_wait_tx_done()
```

---

### 10.5 BLE_DATA：前端到 WiFi 透传

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_uart_app.c
main/ble/ble_data_handler.cpp
main/ble/ble_data_handler.h
```

---

### 10.5 BLE_DATA：前端到 WiFi 透传

关键辅助函数：

```text
ch583_wifi_handle_ble_data()
ch583_wifi_reset_ble_join()
Ch583Uart_HandleBleDataText()
User_HandleWifiJsonTextFromCh583()
handle_wifi_json_text_with_sender()
parse_wifi_config_json()
parse_wifi_wakeup_json()
parse_wifi_work_time_json()
```

---

### 10.6 WIFI_DATA：WiFi 到前端通知

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ble/ble_data_handler.cpp
main/server_network_sta/server_network_sta.c
main/server_network_sta/ping/server_network_sta_ping.c
```

---

### 10.6 WIFI_DATA：WiFi 到前端通知

关键辅助函数：

```text
ch583_wifi_uart_send_wifi_data()
ch583_wifi_send_frame()
ch583_wifi_write_frame_text()
ch583_wifi_handle_reply_status()
send_base_info_to_mobile()
ch583_send_json()
```

---

### 10.7 POWER_OFF：WiFi 主动关电

相关文件：

```text
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.c
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.h
main/factory_reset/factory_reset.c
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_wifi_uart_protocol.h
```

---

### 10.7 POWER_OFF：WiFi 主动关电

关键辅助函数：

```text
ServerNetworkStaWifiWorkTime_Init()
work_state_task()
ServerNetworkStaWifiWorkTime_OnNetworkData()
ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity()
ServerNetworkStaWifiWorkTime_OnCh583Activity()
ServerNetworkStaWifiWorkTime_SetFactoryResetGuard()
ServerNetworkStaWifiWorkTime_RequestFactoryResetPowerCycle()
ServerNetworkStaWifiWorkTime_SetOtaWriteInProgress()
ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress()
UserLedStatus_PreparePowerOffSync()
UserLedStatus_CancelPowerOffSync()
ch583_wifi_uart_send_power_off()
ch583_wifi_send_frame()
```

GPIO28长按达到5秒后、删除任何文件前调用 `ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(true)`；短按不设置。PB1远程请求使用单个RAM状态合并，在Factory Reset任务已就绪时立即设置同一guard，初始化前请求则在任务创建成功后补设。`work_state_task()`在普通入口、LED后和SPI锁内复检该标志，避免普通关机抢占等待、清理或欢迎图显示。欢迎图完成后先调用 `ServerNetworkStaWifiWorkTime_RequestFactoryResetPowerCycle(10)`再清guard；失败时不发布请求并清guard。文件清理按best-effort继续执行，`ENOENT`不算失败，真实unlink、路径长度、目录读取或关闭错误累计到 `file_delete_failed/file_ret`，任一失败都会使总ret失败并禁止关机。

Factory Reset 开始清理前通过统一EPD准入接口预约IDLE状态，并持有预约直到文件和NVS清理完成，避免PB2本地浏览在BUSY检查之后插入。成功后清除 `wifi` namespace 全部键以及 `nvs.net80211:sta.ssid/sta.pswd`，清除本地浏览 `PhotoPainter:local_img_state`，并把 `PhotoPainter:epd_mode` 强制写入并读回校验为 `USER_EPD_DISPLAY_MODE_DEFAULT`。`main/CMakeLists.txt`把 `DOC/welcome.bin`作为二进制常量嵌入固件；Factory Reset校验当前EPD原始显示长度为960000字节后，将该zlib数据通过已有预约提交到1号屏并同步等待完成，等待上限沿用 `USER_EPD_DISPLAY_WAIT_TIMEOUT_MS=5分钟`。只有显示返回 `ESP_OK` 后，`work_state_task()`才强制发送 `WAKE_TIMER ON,10`，再沿用现有LED、SPI和busy复检发送 `WIFI_PROVISION 4F`、`POWER_OFF`；显示失败不提交专用关机请求，但guard仍清除。CH583断电计时10秒后重新上电并以 `DEVICE_INFO.wake_reason=TIMER` 握手。PB1远程请求使用IDLE/PENDING/RUNNING/COMPLETED RAM状态合并重复帧，并复用同一Factory Reset任务；该请求不写工作时间NVS，也不改变普通、轮播、DAILY或LOCAL_IMAGE_BROWSING关机路径。

---

### 10.8 GPIO / GPIO_READ：CH583 GPIO 控制

相关文件：

```text
main/led_status/led_status.c
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_wifi_uart_protocol.h
main/tdx_cfg.h
```

---

### 10.8 GPIO / GPIO_READ：CH583 GPIO 控制

关键辅助函数：

```text
UserLedStatus_Init()
UserLedStatus_Set()
UserLedStatus_Task()
set_ch583_led_level()
ch583_wifi_uart_send_gpio()
ch583_wifi_send_frame()
```

---

### 10.9 接收方处理原则

相关文件：

```text
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_uart_app.c
```

---

### 10.9 接收方处理原则

关键辅助函数：

```text
ch583_wifi_uart_process_bytes()
ch583_wifi_handle_frame_body()
ch583_wifi_parse_frame()
ch583_wifi_validate_len_and_part()
ch583_wifi_send_err()
ch583_wifi_send_ack()
```

---

### 10.10 当前工程源码对应关系

相关文件：

```text
main/ch583_uart/ch583_uart_app.c
main/ch583_uart/ch583_uart_app.h
main/ch583_uart/ch583_wifi_uart_protocol.c
main/ch583_uart/ch583_wifi_uart_protocol.h
main/ble/ble_data_handler.cpp
main/server_network_sta/server_network_sta.c
main/server_network_sta/wifi_work_time/server_network_sta_wifi_work_time.c
main/led_status/led_status.c
```

---

### 10.10 当前工程源码对应关系

关键辅助函数：

```text
Ch583UartApp_Init()
User_UartEventTask()
Ch583Uart_ReadAndProcess()
Ch583Uart_HandleBleDataText()
ch583_wifi_uart_process_bytes()
ch583_wifi_uart_send_wifi_data()
ch583_wifi_uart_send_wake_timer_on()
ch583_wifi_uart_send_wake_timer_off()
ch583_wifi_uart_send_power_off()
ch583_wifi_uart_send_gpio()
ch583_wifi_uart_send_led_blink()
ch583_wifi_uart_send_led_blink_stop()
ch583_wifi_uart_get_ble_mac()
User_HandleWifiJsonTextFromCh583()
```

---

公共测试设置

相关文件：

```text
main/ble/ble_data_handler.cpp
main/ble/ble_data_handler.h
main/ble/user_app.cpp
main/ble/user_app.h
main/ch583_uart/ch583_uart_app.c
main/ch583_uart/ch583_wifi_uart_protocol.c
```

## Local Image Browsing 源码参考 <span id="sec-local-image-reference"></span>

```text
main/local_image_browsing/local_image_browsing.h
├─ 模式3和本功能全部专用宏
├─ PB2触发来源枚举
└─ 初始化、请求、状态和Factory Reset接口

main/local_image_browsing/local_image_browsing.c
├─ /data/bin_img常量内存扫描与稳定有序选择
├─ local_img_state NVS CRC32持久化
├─ PREPARED/IDLE断电事务
├─ 缺失文件有界跳过
├─ DAILY/SLIDESHOW同步停止
└─ EPD空闲预留、BIN加载和同步显示
```

EPD显示模块增加空闲预留接口。预留建立后 `ServerNetworkStaEpdDisplay_IsBusy()` 返回true，普通显示入口在预留有效时拒绝新任务；本地浏览完成文件准备后消费预留并排队。EPD worker先发布active再减少pending，避免任务从队列取出时出现瞬时IDLE。

本地浏览worker栈为8 KB。扫描过程不保存完整目录列表，只在栈中保留全局最小两项、游标之后最小两项和扫描结果等少量17字节缓冲；模块级150项 `s_list` 已删除，静态内部RAM减少2556字节。算法保持原有不区分大小写优先、原始大小写次序补充的稳定顺序，并同时得到当前项、下一项和有效文件总数。worker在每次请求结束后检查栈最低余量，仅低于 `LOCAL_IMAGE_BROWSING_STACK_WARNING_BYTES` 时输出 `ESP_LOGW`。

`ch583_wifi_uart_protocol.c` 仍拥有DEVICE_INFO保存和协议ACK/ERR，并通过一个统一按键分发函数保持两种来源的映射一致：PB2交给本地浏览模块，PB1交给Factory Reset异步请求。DEVICE_INFO严格解析五字段，首次成功ACK才执行该帧的wake_reason业务，重发只重ACK；合法KEY_EVENT不依赖DEVICE_INFO状态，相同SEQ只重ACK。PB3/PB4在业务未定义前返回BAD_ARG。完整通信规则见 [README_Protocol.md](README_Protocol.md#sec-13-local-image)。

`ch583_uart_app.c` 使用长度为4的BLE_DATA指针队列和独立业务任务，隔离UART协议任务与业务模块就绪状态。UART在DEVICE_INFO或业务初始化之前也可接收完整BLE_DATA，但必须先成功预留队列和内存，之后才ACK并提交；预留失败回复`ERR,BUSY`或`ERR,NO_MEM`。SD、EPD、网络完成启动尝试并恢复保存的DAILY/SLIDESHOW状态后，`Ch583UartApp_SetBleDataBusinessReady()`通知业务任务按FIFO处理。DEVICE_INFO或KEY_EVENT的PB2可进入本地浏览启动FIFO，PB1进入Factory Reset单请求状态；KEY_EVENT无论DEVICE_INFO是否完成都按合法帧分发。

---

## 17. zlib 文件压缩模块与自检索引 <span id="sec-17-zlib"></span>

第三方组件：

```text
components/zlib/CMakeLists.txt
components/zlib/idf_component.yml
zlib/
```

项目模块：

```text
main/data_compression/tdx_zlib_file.h
main/data_compression/tdx_zlib_file.c
main/data_compression/tdx_zlib_buffer.h
main/data_compression/tdx_zlib_buffer.c
main/data_compression/test/tdx_zlib_self_test.h
main/data_compression/test/tdx_zlib_self_test.c
main/data_compression/test/tdx_zlib_epd_test.h
main/data_compression/test/tdx_zlib_epd_test.c
```

公开函数：

```c
esp_err_t TdxZlibFile_Compress(const char *source_path,
                               const char *compressed_path,
                               uint64_t *source_size,
                               uint64_t *compressed_size);

esp_err_t TdxZlibFile_Decompress(const char *compressed_path,
                                 const char *output_path,
                                 uint64_t *compressed_size,
                                 uint64_t *output_size);

esp_err_t TdxZlibSelfTest_Run(const char *base_path);

size_t TdxZlibBuffer_GetCompressBound(size_t input_size);

esp_err_t TdxZlibBuffer_Decompress(const uint8_t *compressed_data,
                                   size_t compressed_size,
                                   uint8_t *output_data,
                                   size_t output_capacity,
                                   size_t *output_size);

esp_err_t TdxZlibEpdTest_Run(const char *base_path);
```

正式格式开关位于 `main/tdx_cfg.h` 的EPD配置章节：

```text
USER_EPD_DISPLAY_DATA_ZLIB_ENABLE
```

原文件测试参数仍集中在“Zlib File Compression Self-Test”章节，但旧测试函数没有启动调用：

```text
USER_ZLIB_COMPRESSION_LEVEL
USER_ZLIB_STREAM_BUFFER_SIZE
USER_ZLIB_TEST_PATH_BUFFER_SIZE
USER_ZLIB_TEST_SOURCE_RELATIVE_PATH
USER_ZLIB_TEST_COMPRESSED_RELATIVE_PATH
USER_ZLIB_TEST_DECOMPRESSED_RELATIVE_PATH
```

公共业务入口 `ServerNetworkStaEpdDisplay_QueueToScreen()` 和 `ServerNetworkStaEpdDisplay_QueueToScreenAndWait()` 根据 `USER_EPD_DISPLAY_DATA_ZLIB_ENABLE` 决定解压或复制，队列任务与具体EPD驱动不变。内部 `test_epd_display_and_wait()` 通过静态raw入口继续显示未压缩测试图。

cast、cast2pic和upload入口只校验 `bin_size` 是否等于实际收到的 `bin` part长度，不把压缩输入长度与屏幕原始 `display_size` 比较。宏为 `1` 时由公共EPD入口解压并校验输出长度；宏为 `0` 时继续把raw数据交给原有EPD长度检查。

每日一图下载接口增加 `exact_size_required` 参数：非压缩模式维持严格原始长度，压缩模式使用 `TdxZlibBuffer_GetCompressBound()` 作为容量并接受实际压缩长度。轮播在压缩模式下跳过旧的原始BIN文件名SHA诊断，避免对压缩字节产生误报；非压缩模式保留原诊断。ESP32-C5使用 ESP-IDF 5.5.3 FreeRTOS `xTaskCreateStatic()`：daily、slideshow及轮播启动延迟共用9216字节内部RAM统一静态栈和一个静态TCB，通过按值复制的小型pending命令及generation取消机制串行运行，不依赖运行期heap最大连续块。

`main/main.c` 完整保留 `TdxZlibEpdTest_Run()` 的启动调用代码，当前使用局部 `#if 0` 关闭，没有增加临时测试宏；以后需要诊断时可临时改为 `#if 1`。该函数读取已生成的 `.bin.zlib` 并提交公共EPD入口；读取期间持有 `TdxSharedSpi`，提交显示前释放锁，避免同步等待EPD任务时死锁。

---

公共测试设置

相关文件：

```text
main/led_status/led_status.c
main/led_status/led_status.h
main/tdx_cfg.h
main/ch583_uart/ch583_wifi_uart_protocol.c
```
