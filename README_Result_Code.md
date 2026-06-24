# README_Result_Code.md

本文定义当前工程接口返回 JSON 的 `result` 编码规则。

内部保存用 JSON 不属于接口返回，例如 `slideshow_config.txt`、`show_control.txt`、`last_cast.txt`，不需要加入 `result`。

---

## 目录 <span id="toc"></span>

- [1. 总规则](#sec-01)
  - [1.1 JSON 返回格式](#sec-01-1)
  - [1.2 result 编码原则](#sec-01-2)
  - [1.3 内部保存 JSON 不加 result](#sec-01-3)
- [2. 网络 HTTP 汇总](#sec-02)
  - [2.1 网络 HTTP 通用错误](#sec-02-1)
  - [2.2 ping：网络连通检测](#sec-02-2)
  - [2.3 saved_images：本地图片列表](#sec-02-3)
  - [2.4 thumb：缩略图读取](#sec-02-4)
  - [2.5 snapshot：图片与轮播状态快照](#sec-02-5)
  - [2.6 delete：图片删除](#sec-02-6)
  - [2.7 slideshow：启动轮播](#sec-02-7)
  - [2.8 slideshow_control：轮播开关控制](#sec-02-8)
  - [2.9 cast：普通投图](#sec-02-9)
  - [2.10 upload：图片上传](#sec-02-10)
  - [2.11 cast2pic：双屏投图 / 缓存](#sec-02-11)
  - [2.12 ota：网络 OTA](#sec-02-12)
  - [2.13 wifi_work_time：WiFi 工作时长](#sec-02-13)
- [3. 有线 USB 汇总](#sec-03)
  - [3.1 USB 接收层错误](#sec-03-1)
  - [3.2 USB router 路由错误](#sec-03-2)
  - [3.3 USB ping](#sec-03-3)
  - [3.4 USB saved_images / thumb](#sec-03-4)
  - [3.5 USB snapshot](#sec-03-5)
  - [3.6 USB delete](#sec-03-6)
  - [3.7 USB slideshow](#sec-03-7)
  - [3.8 USB slideshow_control](#sec-03-8)
  - [3.9 USB cast / upload / cast2pic](#sec-03-9)
  - [3.10 USB wifi](#sec-03-10)
  - [3.11 USB wifi_work_time](#sec-03-11)
  - [3.12 USB epd_type / set_epd_type / test_epd_display](#sec-03-12)
- [4. CH583 蓝牙汇总](#sec-04)
  - [4.1 CH583 / BLE JSON 通用错误](#sec-04-1)
  - [4.2 wifi：CH583 蓝牙配网](#sec-04-2)
  - [4.3 wifi_wakeup：CH583 唤醒 WiFi](#sec-04-3)
  - [4.4 set_wifi_work_time / wifi_standby：CH583 设置 WiFi 工作时间](#sec-04-4)
  - [4.5 BLE_MAC / ping 相关返回](#sec-04-5)
- [5. 其他](#sec-05)
  - [5.1 EPD type result](#sec-05-1)
  - [5.2 EPD test result](#sec-05-2)
  - [5.3 本地配置 JSON 文件](#sec-05-3)
- [6. 公共码表](#sec-06)
  - [6.1 通用 result 编码](#sec-06-1)
  - [6.2 USB result 编码](#sec-06-2)
  - [6.3 BLE / CH583 result 编码](#sec-06-3)
  - [6.4 WiFi result 编码](#sec-06-4)
  - [6.5 图片 / ping / 快照 result 编码](#sec-06-5)
  - [6.6 delete / slideshow result 编码](#sec-06-6)
  - [6.7 cast / upload / cast2pic result 编码](#sec-06-7)
  - [6.8 OTA result 编码](#sec-06-8)
  - [6.9 EPD result 编码](#sec-06-9)
  - [6.10 result 编码范围总表](#sec-06-10)

---

## 1. 总规则 <span id="sec-01"></span>

### 1.1 JSON 返回格式 <span id="sec-01-1"></span>

成功返回建议格式：

```json
{"func":"xxx_result","result":0,"message":"ok"}
```

失败返回建议格式：

```json
{"func":"xxx_result","result":1604,"message":"xxx failed","error":"missing_bin"}
```

字段建议：

| 字段 | 要求 | 说明 |
|---|---|---|
| `func` | 必须 | 返回类型，例如 `cast_result` |
| `result` | 必须 | `0` 成功，非 `0` 失败 |
| `message` | 建议必须 | 给人看的简短说明 |
| `error` | 失败时建议有 | 给程序判断的稳定错误名 |
| `stage` | 阶段流程可选 | WiFi / OTA / BLE 等流程使用 |
| `esp_err` | ESP-IDF 错误可选 | OTA、NVS、WiFi 等底层错误可带 |

[⬆ 返回目录](#toc)

### 1.2 result 编码原则 <span id="sec-01-2"></span>

```text
0         成功
1001~1099 通用错误
1101~1199 USB 接收 / 路由错误
1201~1299 BLE / CH583 通用错误
1301~1399 WiFi / WiFi 工作时间错误
1401~1499 图片列表 / 快照 / ping / thumb 错误
1501~1599 delete / slideshow / slideshow_control 错误
1601~1699 cast / upload / cast2pic / multipart 错误
1701~1799 OTA 错误
1801~1899 EPD 类型 / EPD 测试错误
1901~1999 预留
```

[⬆ 返回目录](#toc)

### 1.3 内部保存 JSON 不加 result <span id="sec-01-3"></span>

以下 JSON 是设备本地保存状态，不是接口返回，不需要加入 `result`：

```text
/data/bin_img/slideshow_config.txt
/data/bin_img/show_control.txt
/data/bin_img/last_cast.txt
```

[⬆ 返回目录](#toc)

---

## 2. 网络 HTTP 汇总 <span id="sec-02"></span>

### 2.1 网络 HTTP 通用错误 <span id="sec-02-1"></span>

网络 HTTP 接口通用使用 `1001~1016`。例如 JSON 格式错误、`func` 不支持、method 不支持、body 太大、内存不足、存储未就绪等。

[⬆ 返回目录](#toc)

### 2.2 ping：网络连通检测 <span id="sec-02-2"></span>

返回 func：

```text
ping_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 连通性检查成功 |
| `1405` | `TDX_JSON_RESULT_BLE_MAC_EMPTY` | BLE MAC 尚未获取 |

[⬆ 返回目录](#toc)

### 2.3 saved_images：本地图片列表 <span id="sec-02-3"></span>

返回 func：

```text
get_saved_images_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 图片列表读取成功 |
| `1401` | `TDX_JSON_RESULT_IMAGES_READ_FAILED` | 图片列表读取失败 |

[⬆ 返回目录](#toc)

### 2.4 thumb：缩略图读取 <span id="sec-02-4"></span>

返回 func：

```text
thumb_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 缩略图读取成功 |
| `1402` | `TDX_JSON_RESULT_THUMB_NAME_INVALID` | 缩略图名称非法 |
| `1403` | `TDX_JSON_RESULT_THUMB_NOT_FOUND` | 缩略图不存在 |

网络 thumb 成功响应是 `HTTP 200 image/jpeg` 二进制，不额外包装成功 JSON；`1402/1403` 用于 HTTP 400/404 的 JSON 错误响应。USB thumb 失败同样使用这两个 result。

[⬆ 返回目录](#toc)

### 2.5 snapshot：图片与轮播状态快照 <span id="sec-02-5"></span>

返回 func：

```text
get_snapshot_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 快照读取成功 |
| `1011` | `TDX_JSON_RESULT_NO_MEMORY` | 内存分配失败 |
| `1401` | `TDX_JSON_RESULT_IMAGES_READ_FAILED` | 图片列表读取失败 |
| `1404` | `TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED` | 快照 JSON 生成失败 |

[⬆ 返回目录](#toc)

### 2.6 delete：图片删除 <span id="sec-02-6"></span>

返回 func：

```text
delete_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 删除成功 |
| `1001` | `TDX_JSON_RESULT_JSON_INVALID` | JSON 格式错误 |
| `1003` | `TDX_JSON_RESULT_FIELD_MISSING` | 缺少必要字段 |
| `1501` | `TDX_JSON_RESULT_FILE_NAMES_MISSING` | `fileNames` 缺失或为空 |
| `1502` | `TDX_JSON_RESULT_FILE_NAME_INVALID` | 文件名非法 |
| `1503` | `TDX_JSON_RESULT_DELETE_FAILED` | 删除文件或清理关联状态失败 |

[⬆ 返回目录](#toc)

### 2.7 slideshow：启动轮播 <span id="sec-02-7"></span>

返回 func：

```text
start_slideshow_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 轮播启动成功 |
| `1501` | `TDX_JSON_RESULT_FILE_NAMES_MISSING` | `fileNames` 缺失 |
| `1502` | `TDX_JSON_RESULT_FILE_NAME_INVALID` | 文件名非法 |
| `1504` | `TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED` | 轮播配置保存失败 |
| `1505` | `TDX_JSON_RESULT_SLIDESHOW_START_FAILED` | 轮播启动失败 |
| `1506` | `TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED` | 轮播运行时启动失败 |
| `1507` | `TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID` | 轮播间隔非法 |
| `1508` | `TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND` | 轮播文件不存在 |

[⬆ 返回目录](#toc)

### 2.8 slideshow_control：轮播开关控制 <span id="sec-02-8"></span>

返回 func：

```text
set_slideshow_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 轮播控制设置成功 |
| `1004` | `TDX_JSON_RESULT_PARAM_INVALID` | `sw` / `random` / `interval` 参数非法 |
| `1506` | `TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED` | 开启轮播时运行时启动失败 |
| `1507` | `TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID` | 轮播间隔非法 |
| `1509` | `TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED` | 轮播控制状态保存失败 |

[⬆ 返回目录](#toc)

### 2.9 cast：普通投图 <span id="sec-02-9"></span>

返回 func：

```text
cast_received
cast_result
```

`cast` 使用 `1601~1612` 图片上传类 result，详见 [6.7](#sec-06-7)。

[⬆ 返回目录](#toc)

### 2.10 upload：图片上传 <span id="sec-02-10"></span>

返回 func：

```text
upload_result
upload_raw_result
```

`upload` 使用 `1601~1615` 图片上传类 result，详见 [6.7](#sec-06-7)。

[⬆ 返回目录](#toc)

### 2.11 cast2pic：双屏投图 / 缓存 <span id="sec-02-11"></span>

返回 func：

```text
cast2pic_result
```

`cast2pic` 使用 `1601~1617` 图片上传类 result，并可复用 `1012/1013` 存储错误，详见 [6.1](#sec-06-1) 和 [6.7](#sec-06-7)。

[⬆ 返回目录](#toc)

### 2.12 ota：网络 OTA <span id="sec-02-12"></span>

返回 func：

```text
ota_event
ota_result
```

OTA 使用 `1701~1713`，详见 [6.8](#sec-06-8)。

[⬆ 返回目录](#toc)

### 2.13 wifi_work_time：WiFi 工作时长 <span id="sec-02-13"></span>

返回 func：

```text
set_wifi_work_time_result
```

`wifi_work_time` 使用 `1351~1354`，详见 [6.4](#sec-06-4)。

[⬆ 返回目录](#toc)

---

## 3. 有线 USB 汇总 <span id="sec-03"></span>

### 3.1 USB 接收层错误 <span id="sec-03-1"></span>

返回 func：

```text
usb_receive_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `1101` | `TDX_JSON_RESULT_USB_REQUEST_TOO_LARGE` | USB 请求太大 |
| `1102` | `TDX_JSON_RESULT_USB_REQUEST_TIMEOUT` | USB 请求接收超时 |
| `1103` | `TDX_JSON_RESULT_USB_BAD_REQUEST` | USB HTTP-like 请求格式错误 |

[⬆ 返回目录](#toc)

### 3.2 USB router 路由错误 <span id="sec-03-2"></span>

返回 func：

```text
usb_unknown_result
usb_route_result
unknown_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `1104` | `TDX_JSON_RESULT_USB_ROUTE_NOT_FOUND` | USB 路由不存在 |
| `1105` | `TDX_JSON_RESULT_USB_HANDLER_FAILED` | USB handler 执行失败 |
| `1106` | `TDX_JSON_RESULT_USB_ASYNC_FAILED` | USB 异步任务失败 |

[⬆ 返回目录](#toc)

### 3.3 USB ping <span id="sec-03-3"></span>

返回 func：

```text
ping_result
```

使用规则与网络 HTTP `ping_result` 一致，详见 [2.2](#sec-02-2)。

[⬆ 返回目录](#toc)

### 3.4 USB saved_images / thumb <span id="sec-03-4"></span>

返回 func：

```text
get_saved_images_result
thumb_result
```

使用规则与网络 HTTP `saved_images`、`thumb` 一致，详见 [2.3](#sec-02-3)、[2.4](#sec-02-4)。

[⬆ 返回目录](#toc)

### 3.5 USB snapshot <span id="sec-03-5"></span>

返回 func：

```text
get_snapshot_result
```

使用规则与网络 HTTP `snapshot` 一致，详见 [2.5](#sec-02-5)。

[⬆ 返回目录](#toc)

### 3.6 USB delete <span id="sec-03-6"></span>

返回 func：

```text
delete_result
```

使用规则与网络 HTTP `delete` 一致，详见 [2.6](#sec-02-6)。

[⬆ 返回目录](#toc)

### 3.7 USB slideshow <span id="sec-03-7"></span>

返回 func：

```text
start_slideshow_result
```

使用规则与网络 HTTP `slideshow` 一致，详见 [2.7](#sec-02-7)。

[⬆ 返回目录](#toc)

### 3.8 USB slideshow_control <span id="sec-03-8"></span>

返回 func：

```text
set_slideshow_result
```

使用规则与网络 HTTP `slideshow_control` 一致，详见 [2.8](#sec-02-8)。

[⬆ 返回目录](#toc)

### 3.9 USB cast / upload / cast2pic <span id="sec-03-9"></span>

返回 func：

```text
cast_result
upload_result
cast2pic_result
upload_raw_result
```

USB 图片传输接口与网络 HTTP 图片接口共用 `1601~1617`，详见 [6.7](#sec-06-7)。

[⬆ 返回目录](#toc)

### 3.10 USB wifi <span id="sec-03-10"></span>

返回 func：

```text
wifi_result
```

USB WiFi 使用 `1301~1306`，详见 [6.4](#sec-06-4)。

[⬆ 返回目录](#toc)

### 3.11 USB wifi_work_time <span id="sec-03-11"></span>

返回 func：

```text
set_wifi_work_time_result
```

USB WiFi 工作时间使用 `1351~1354`，详见 [6.4](#sec-06-4)。

[⬆ 返回目录](#toc)

### 3.12 USB epd_type / set_epd_type / test_epd_display <span id="sec-03-12"></span>

返回 func：

```text
epd_type
set_epd_type_result
test_epd_display_result
```

USB EPD 类型和测试使用 `1801~1803`，详见 [6.9](#sec-06-9)。

[⬆ 返回目录](#toc)

---

## 4. CH583 蓝牙汇总 <span id="sec-04"></span>

### 4.1 CH583 / BLE JSON 通用错误 <span id="sec-04-1"></span>

返回 func：

```text
ble_json_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `1201` | `TDX_JSON_RESULT_BLE_JSON_EMPTY` | BLE / CH583 透传 JSON 为空 |
| `1202` | `TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED` | BLE / CH583 `func` 不支持 |
| `1203` | `TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED` | BLE / CH583 JSON 解析失败 |
| `1204` | `TDX_JSON_RESULT_BLE_SEND_FAILED` | BLE / CH583 返回发送失败 |

[⬆ 返回目录](#toc)

### 4.2 wifi：CH583 蓝牙配网 <span id="sec-04-2"></span>

请求 func：

```text
wifi
```

返回 func：

```text
wifi_result
```

同步保存/提交结果使用 `1301~1306`；后台连接完成后可继续通过 `wifi_result` 通知 `1307~1309`，详见 [6.4](#sec-06-4)。

[⬆ 返回目录](#toc)

### 4.3 wifi_wakeup：CH583 唤醒 WiFi <span id="sec-04-3"></span>

请求 func：

```text
wifi_wakeup
```

返回 func：

```text
wifi_wakeup_result
wifi_info_result
```

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | `wifi_wakeup_result` 表示连接已提交/正在连接（`stage=connecting`）；`wifi_info_result` 表示已取得 IP（`stage=<IP>`） |
| `1205` | `TDX_JSON_RESULT_BLE_NO_SAVED_WIFI` | 没有可用 WiFi 配置 |
| `1307` | `TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT` | WiFi 连接超时 |
| `1308` | `TDX_JSON_RESULT_WIFI_AUTH_FAILED` | WiFi 认证失败 |
| `1309` | `TDX_JSON_RESULT_WIFI_GOT_IP_FAILED` | 获取 IP 失败 |

[⬆ 返回目录](#toc)

### 4.4 set_wifi_work_time / wifi_standby：CH583 设置 WiFi 工作时间 <span id="sec-04-4"></span>

请求 func：

```text
set_wifi_work_time
wifi_standby
```

返回 func：

```text
set_wifi_work_time_result
```

CH583 侧兼容 `seconds` 和旧字段 `time`，使用 `1351~1354`，详见 [6.4](#sec-06-4)。

[⬆ 返回目录](#toc)

### 4.5 BLE_MAC / ping 相关返回 <span id="sec-04-5"></span>

CH583 上报 BLE MAC 后，设备可在 `ping_result` 或 `wifi_info_result` 中携带：

```json
{"Ble_MAC":"AABBCCDDEEFF"}
```

如果 BLE MAC 缺失但不影响 ping 成功，可以继续返回 `result=0`。如果业务要求 BLE MAC 必须存在，再返回 `1405`。

[⬆ 返回目录](#toc)

---

## 5. 其他 <span id="sec-05"></span>

### 5.1 EPD type result <span id="sec-05-1"></span>

EPD 相关 result 当前主要由 USB 使用。如果以后网络 HTTP 或 CH583 也支持设置 EPD type，统一复用这里的 result。

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | EPD 类型读取或设置成功 |
| `1801` | `TDX_JSON_RESULT_EPD_TYPE_INVALID` | 当前或目标 EPD 类型非法 |
| `1802` | `TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED` | EPD 类型保存失败 |

[⬆ 返回目录](#toc)

### 5.2 EPD test result <span id="sec-05-2"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | EPD 测试显示成功 |
| `1801` | `TDX_JSON_RESULT_EPD_TYPE_INVALID` | 当前 EPD 类型非法 |
| `1803` | `TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED` | EPD 测试显示失败 |

[⬆ 返回目录](#toc)

### 5.3 本地配置 JSON 文件 <span id="sec-05-3"></span>

以下文件不是接口返回，不需要 `result`：

```text
slideshow_config.txt
show_control.txt
last_cast.txt
```

示例：

```json
{"fileNames":["26422","26423"],"interval":60,"random":false}
```

```json
{"sw":1,"interval":60,"random":false,"run_mode":0}
```

[⬆ 返回目录](#toc)

---

## 6. 公共码表 <span id="sec-06"></span>

### 6.1 通用 result 编码 <span id="sec-06-1"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 成功 |
| `1001` | `TDX_JSON_RESULT_JSON_INVALID` | JSON 格式错误 |
| `1002` | `TDX_JSON_RESULT_FUNC_UNSUPPORTED` | `func` 不支持 |
| `1003` | `TDX_JSON_RESULT_FIELD_MISSING` | 缺少必要字段 |
| `1004` | `TDX_JSON_RESULT_PARAM_INVALID` | 参数值非法 |
| `1005` | `TDX_JSON_RESULT_METHOD_UNSUPPORTED` | HTTP method 不支持 |
| `1006` | `TDX_JSON_RESULT_BODY_TOO_LARGE` | body 太大 |
| `1007` | `TDX_JSON_RESULT_BUSY` | 当前忙 |
| `1008` | `TDX_JSON_RESULT_TIMEOUT` | 超时 |
| `1009` | `TDX_JSON_RESULT_INTERNAL_ERROR` | 内部错误 |
| `1010` | `TDX_JSON_RESULT_JSON_TOO_LONG` | 返回 JSON 太长 |
| `1011` | `TDX_JSON_RESULT_NO_MEMORY` | 内存分配失败 |
| `1012` | `TDX_JSON_RESULT_STORAGE_NOT_READY` | 存储未就绪 |
| `1013` | `TDX_JSON_RESULT_STORAGE_NO_SPACE` | 存储空间不足 |
| `1014` | `TDX_JSON_RESULT_NOT_FOUND` | 文件或资源不存在 |
| `1015` | `TDX_JSON_RESULT_PATH_UNSAFE` | 文件名或路径不安全 |
| `1016` | `TDX_JSON_RESULT_QUEUE_FAILED` | 队列提交失败 |

[⬆ 返回目录](#toc)

### 6.2 USB result 编码 <span id="sec-06-2"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1101` | `TDX_JSON_RESULT_USB_REQUEST_TOO_LARGE` | USB 请求太大 |
| `1102` | `TDX_JSON_RESULT_USB_REQUEST_TIMEOUT` | USB 请求接收超时 |
| `1103` | `TDX_JSON_RESULT_USB_BAD_REQUEST` | USB HTTP-like 请求格式错误 |
| `1104` | `TDX_JSON_RESULT_USB_ROUTE_NOT_FOUND` | USB 路由不存在 |
| `1105` | `TDX_JSON_RESULT_USB_HANDLER_FAILED` | USB handler 执行失败 |
| `1106` | `TDX_JSON_RESULT_USB_ASYNC_FAILED` | USB 异步任务失败 |

[⬆ 返回目录](#toc)

### 6.3 BLE / CH583 result 编码 <span id="sec-06-3"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1201` | `TDX_JSON_RESULT_BLE_JSON_EMPTY` | BLE / CH583 透传 JSON 为空 |
| `1202` | `TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED` | BLE / CH583 `func` 不支持 |
| `1203` | `TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED` | BLE / CH583 JSON 解析失败 |
| `1204` | `TDX_JSON_RESULT_BLE_SEND_FAILED` | BLE / CH583 返回发送失败 |
| `1205` | `TDX_JSON_RESULT_BLE_NO_SAVED_WIFI` | `wifi_wakeup` 未找到已保存 WiFi |

[⬆ 返回目录](#toc)

### 6.4 WiFi result 编码 <span id="sec-06-4"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1301` | `TDX_JSON_RESULT_WIFI_SSID_MISSING` | `ssid` 缺失 |
| `1302` | `TDX_JSON_RESULT_WIFI_KEY_MISSING` | `key` / password 缺失 |
| `1303` | `TDX_JSON_RESULT_WIFI_SSID_INVALID` | `ssid` 长度或内容非法 |
| `1304` | `TDX_JSON_RESULT_WIFI_KEY_INVALID` | `key` 长度或内容非法 |
| `1305` | `TDX_JSON_RESULT_WIFI_SAVE_FAILED` | WiFi 配置保存失败 |
| `1306` | `TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED` | WiFi 连接任务提交失败 |
| `1307` | `TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT` | WiFi 连接超时 |
| `1308` | `TDX_JSON_RESULT_WIFI_AUTH_FAILED` | WiFi 认证失败 |
| `1309` | `TDX_JSON_RESULT_WIFI_GOT_IP_FAILED` | WiFi 获取 IP 失败 |
| `1351` | `TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING` | `seconds` / `time` 缺失 |
| `1352` | `TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE` | 工作时间超出范围 |
| `1353` | `TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED` | 工作时间保存失败 |
| `1354` | `TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED` | 工作时间应用失败 |

`1307~1309` 是异步连接结果码。BLE / CH583 的 `wifi` 与 `wifi_wakeup` 在 worker 完成后分别通过 `wifi_result`、`wifi_wakeup_result` 通知连接超时、认证失败或取 IP 失败；USB `/wifi` 原请求仍只返回保存/worker 提交结果。`1354` 已用于工作状态任务尚未初始化、运行时参数无法应用的路径。

[⬆ 返回目录](#toc)

### 6.5 图片 / ping / 快照 result 编码 <span id="sec-06-5"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1401` | `TDX_JSON_RESULT_IMAGES_READ_FAILED` | 图片列表读取失败 |
| `1402` | `TDX_JSON_RESULT_THUMB_NAME_INVALID` | 缩略图名称非法 |
| `1403` | `TDX_JSON_RESULT_THUMB_NOT_FOUND` | 缩略图不存在 |
| `1404` | `TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED` | 快照 JSON 生成失败 |
| `1405` | `TDX_JSON_RESULT_BLE_MAC_EMPTY` | BLE MAC 尚未获取 |

[⬆ 返回目录](#toc)

### 6.6 delete / slideshow result 编码 <span id="sec-06-6"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1501` | `TDX_JSON_RESULT_FILE_NAMES_MISSING` | `fileNames` 缺失 |
| `1502` | `TDX_JSON_RESULT_FILE_NAME_INVALID` | 文件名非法 |
| `1503` | `TDX_JSON_RESULT_DELETE_FAILED` | 删除失败 |
| `1504` | `TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED` | 轮播配置保存失败 |
| `1505` | `TDX_JSON_RESULT_SLIDESHOW_START_FAILED` | 轮播启动失败 |
| `1506` | `TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED` | 轮播运行时启动失败 |
| `1507` | `TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID` | 轮播间隔非法 |
| `1508` | `TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND` | 轮播文件不存在 |
| `1509` | `TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED` | 轮播控制状态保存失败 |

[⬆ 返回目录](#toc)

### 6.7 cast / upload / cast2pic result 编码 <span id="sec-06-7"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1601` | `TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING` | multipart boundary 缺失 |
| `1602` | `TDX_JSON_RESULT_UPLOAD_FUNC_MISSING` | multipart `func` 缺失 |
| `1603` | `TDX_JSON_RESULT_UPLOAD_INVALID` | 上传内容格式非法 |
| `1604` | `TDX_JSON_RESULT_UPLOAD_BIN_MISSING` | `bin` 缺失 |
| `1605` | `TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING` | `image` 缺失 |
| `1606` | `TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH` | 声明大小和实际大小不一致 |
| `1607` | `TDX_JSON_RESULT_SAVE_BIN_FAILED` | 保存 bin 失败 |
| `1608` | `TDX_JSON_RESULT_SAVE_IMAGE_FAILED` | 保存 image 失败 |
| `1609` | `TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED` | EPD 显示队列提交失败 |
| `1610` | `TDX_JSON_RESULT_LAST_CAST_SAVE_FAILED` | last cast 保存失败 |
| `1611` | `TDX_JSON_RESULT_SAVE_REQUIRED_FOR_LAST_CAST` | cast 需要 `save=true` 才能记录 last cast |
| `1612` | `TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID` | 上传文件名非法 |
| `1613` | `TDX_JSON_RESULT_UPLOAD_RAW_PATH_MISSING` | raw upload 缺少 path |
| `1614` | `TDX_JSON_RESULT_UPLOAD_RAW_PATH_INVALID` | raw upload path 非法 |
| `1615` | `TDX_JSON_RESULT_UPLOAD_RAW_SAVE_FAILED` | raw upload 保存失败 |
| `1616` | `TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID` | cast2pic `screen` 非法 |
| `1617` | `TDX_JSON_RESULT_CAST2PIC_SCREEN_UNSUPPORTED` | cast2pic `screen` 暂不支持 |

[⬆ 返回目录](#toc)

### 6.8 OTA result 编码 <span id="sec-06-8"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1701` | `TDX_JSON_RESULT_OTA_BOUNDARY_MISSING` | OTA boundary 缺失 |
| `1702` | `TDX_JSON_RESULT_OTA_META_MISSING` | OTA meta 缺失 |
| `1703` | `TDX_JSON_RESULT_OTA_META_INVALID` | OTA meta 非法 |
| `1704` | `TDX_JSON_RESULT_OTA_FIRMWARE_MISSING` | OTA firmware 缺失 |
| `1705` | `TDX_JSON_RESULT_OTA_FIRMWARE_SIZE_INVALID` | OTA firmware size 非法 |
| `1706` | `TDX_JSON_RESULT_OTA_BEGIN_FAILED` | `esp_ota_begin()` 失败 |
| `1707` | `TDX_JSON_RESULT_OTA_WRITE_FAILED` | `esp_ota_write()` 失败 |
| `1708` | `TDX_JSON_RESULT_OTA_END_FAILED` | `esp_ota_end()` 失败 |
| `1709` | `TDX_JSON_RESULT_OTA_VERIFY_FAILED` | OTA 校验失败 |
| `1710` | `TDX_JSON_RESULT_OTA_SET_BOOT_FAILED` | 设置 boot partition 失败 |
| `1711` | `TDX_JSON_RESULT_OTA_VERSION_MISMATCH` | meta version 与固件版本不一致 |
| `1712` | `TDX_JSON_RESULT_OTA_PARTITION_TOO_SMALL` | OTA 分区空间不足 |
| `1713` | `TDX_JSON_RESULT_OTA_BUSY` | OTA 正在执行 |

[⬆ 返回目录](#toc)

### 6.9 EPD result 编码 <span id="sec-06-9"></span>

| result | 名称建议 | 含义 |
|---:|---|---|
| `1801` | `TDX_JSON_RESULT_EPD_TYPE_INVALID` | 当前或目标 EPD 类型非法 |
| `1802` | `TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED` | EPD 类型保存失败 |
| `1803` | `TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED` | EPD 测试显示失败 |
| `1804` | `TDX_JSON_RESULT_EPD_DISPLAY_FAILED` | cast/upload/cast2pic 的 EPD 驱动执行失败 |

[⬆ 返回目录](#toc)

### 6.10 result 编码范围总表 <span id="sec-06-10"></span>

| 范围 | 类型 |
|---:|---|
| `0` | 成功 |
| `1001~1099` | 通用错误 |
| `1101~1199` | USB 接收 / 路由错误 |
| `1201~1299` | BLE / CH583 通用错误 |
| `1301~1399` | WiFi / WiFi 工作时间错误 |
| `1401~1499` | 图片列表 / 快照 / ping / thumb 错误 |
| `1501~1599` | delete / slideshow / slideshow_control 错误 |
| `1601~1699` | cast / upload / cast2pic / multipart 错误 |
| `1701~1799` | OTA 错误 |
| `1801~1899` | EPD 类型 / EPD 测试错误 |
| `1901~1999` | 预留 |

[⬆ 返回目录](#toc)
