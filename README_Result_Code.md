# README_Result_Code.md

本文只定义接口返回 JSON 的 `result` 编码建议。内部保存用 JSON 不属于接口返回，不需要加入 `result`。

This file defines suggested `result` codes for API JSON responses only.

## 1. JSON response rule

成功返回：

```json
{"func":"xxx_result","result":0,"message":"ok"}
```

失败返回：

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

## 2. Common result

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

## 3. USB receive result

| result | 名称建议 | 含义 |
|---:|---|---|
| `1101` | `TDX_JSON_RESULT_USB_REQUEST_TOO_LARGE` | USB 请求太大 |
| `1102` | `TDX_JSON_RESULT_USB_REQUEST_TIMEOUT` | USB 请求接收超时 |
| `1103` | `TDX_JSON_RESULT_USB_BAD_REQUEST` | USB HTTP-like 请求格式错误 |

## 4. USB router result

| result | 名称建议 | 含义 |
|---:|---|---|
| `1104` | `TDX_JSON_RESULT_USB_ROUTE_NOT_FOUND` | USB 路由不存在 |
| `1105` | `TDX_JSON_RESULT_USB_HANDLER_FAILED` | USB handler 执行失败 |
| `1106` | `TDX_JSON_RESULT_USB_ASYNC_FAILED` | USB 异步任务失败 |

## 5. BLE / CH583 JSON result

| result | 名称建议 | 含义 |
|---:|---|---|
| `1201` | `TDX_JSON_RESULT_BLE_JSON_EMPTY` | BLE / CH583 透传 JSON 为空 |
| `1202` | `TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED` | BLE / CH583 `func` 不支持 |
| `1203` | `TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED` | BLE / CH583 JSON 解析失败 |
| `1204` | `TDX_JSON_RESULT_BLE_SEND_FAILED` | BLE / CH583 返回发送失败 |
| `1205` | `TDX_JSON_RESULT_BLE_NO_SAVED_WIFI` | `wifi_wakeup` 未找到已保存 WiFi |

## 6. WiFi result

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

## 7. WiFi work time result

| result | 名称建议 | 含义 |
|---:|---|---|
| `1351` | `TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING` | `seconds` / `time` 缺失 |
| `1352` | `TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE` | 工作时间超出范围 |
| `1353` | `TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED` | 工作时间保存失败 |
| `1354` | `TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED` | 工作时间应用失败 |

## 8. saved_images result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 图片列表读取成功 |
| `1401` | `TDX_JSON_RESULT_IMAGES_READ_FAILED` | 图片列表读取失败 |

## 9. thumb result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 缩略图读取成功 |
| `1402` | `TDX_JSON_RESULT_THUMB_NAME_INVALID` | 缩略图名称非法 |
| `1403` | `TDX_JSON_RESULT_THUMB_NOT_FOUND` | 缩略图不存在 |

## 10. snapshot result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 快照读取成功 |
| `1401` | `TDX_JSON_RESULT_IMAGES_READ_FAILED` | 图片列表读取失败 |
| `1404` | `TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED` | 快照 JSON 生成失败 |

## 11. ping result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 连通性检查成功 |
| `1405` | `TDX_JSON_RESULT_BLE_MAC_EMPTY` | BLE MAC 尚未获取 |

## 12. delete result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 删除成功 |
| `1501` | `TDX_JSON_RESULT_FILE_NAMES_MISSING` | `fileNames` 缺失 |
| `1502` | `TDX_JSON_RESULT_FILE_NAME_INVALID` | 文件名非法 |
| `1503` | `TDX_JSON_RESULT_DELETE_FAILED` | 删除失败 |

## 13. slideshow result

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

## 14. slideshow_control result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | 轮播控制设置成功 |
| `1004` | `TDX_JSON_RESULT_PARAM_INVALID` | `sw` / `random` / `interval` 参数非法 |
| `1506` | `TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED` | 开启轮播时运行时启动失败 |
| `1507` | `TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID` | 轮播间隔非法 |
| `1509` | `TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED` | 轮播控制状态保存失败 |

## 15. cast result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | cast 成功 |
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

## 16. upload result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | upload 成功 |
| `1601` | `TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING` | multipart boundary 缺失 |
| `1602` | `TDX_JSON_RESULT_UPLOAD_FUNC_MISSING` | multipart `func` 缺失 |
| `1603` | `TDX_JSON_RESULT_UPLOAD_INVALID` | 上传内容格式非法 |
| `1604` | `TDX_JSON_RESULT_UPLOAD_BIN_MISSING` | `bin` 缺失 |
| `1605` | `TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING` | `image` 缺失 |
| `1606` | `TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH` | 声明大小和实际大小不一致 |
| `1607` | `TDX_JSON_RESULT_SAVE_BIN_FAILED` | 保存 bin 失败 |
| `1608` | `TDX_JSON_RESULT_SAVE_IMAGE_FAILED` | 保存 image 失败 |
| `1609` | `TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED` | EPD 显示队列提交失败 |
| `1612` | `TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID` | 上传文件名非法 |
| `1613` | `TDX_JSON_RESULT_UPLOAD_RAW_PATH_MISSING` | raw upload 缺少 path |
| `1614` | `TDX_JSON_RESULT_UPLOAD_RAW_PATH_INVALID` | raw upload path 非法 |
| `1615` | `TDX_JSON_RESULT_UPLOAD_RAW_SAVE_FAILED` | raw upload 保存失败 |

## 17. cast2pic result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | cast2pic 成功 |
| `1601` | `TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING` | multipart boundary 缺失 |
| `1602` | `TDX_JSON_RESULT_UPLOAD_FUNC_MISSING` | multipart `func` 缺失 |
| `1603` | `TDX_JSON_RESULT_UPLOAD_INVALID` | 上传内容格式非法 |
| `1604` | `TDX_JSON_RESULT_UPLOAD_BIN_MISSING` | `bin` 缺失 |
| `1605` | `TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING` | `image` 缺失 |
| `1606` | `TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH` | 声明大小和实际大小不一致 |
| `1607` | `TDX_JSON_RESULT_SAVE_BIN_FAILED` | 保存 bin 失败 |
| `1608` | `TDX_JSON_RESULT_SAVE_IMAGE_FAILED` | 保存 image 失败 |
| `1609` | `TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED` | EPD 显示队列提交失败 |
| `1612` | `TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID` | 上传文件名非法 |
| `1616` | `TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID` | cast2pic `screen` 非法 |
| `1617` | `TDX_JSON_RESULT_CAST2PIC_SCREEN_UNSUPPORTED` | cast2pic `screen` 暂不支持 |

## 18. OTA result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | OTA 成功 |
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

## 19. EPD type result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | EPD 类型读取或设置成功 |
| `1801` | `TDX_JSON_RESULT_EPD_TYPE_INVALID` | 当前或目标 EPD 类型非法 |
| `1802` | `TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED` | EPD 类型保存失败 |

## 20. EPD test result

| result | 名称建议 | 含义 |
|---:|---|---|
| `0` | `TDX_JSON_RESULT_OK` | EPD 测试显示成功 |
| `1801` | `TDX_JSON_RESULT_EPD_TYPE_INVALID` | 当前 EPD 类型非法 |
| `1803` | `TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED` | EPD 测试显示失败 |
