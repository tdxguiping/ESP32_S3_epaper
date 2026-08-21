# README_Test.md

本文件集中保存开发阶段的测试方法和命令。测试前先确认设备已联网，并把示例 IP、文件路径改为实际值。接口协议见 [README_Protocol.md](README_Protocol.md)，预期业务行为见 [README_Fun.md](README_Fun.md)，结果码见 [README_Result_Code.md](README_Result_Code.md)。

持久状态专项检查：启动日志应出现 `persistent image-state ready partition=default namespace=image_state`。完整烧录和应用程序 OTA 前后都必须读取同一默认 NVS 分区；SD 中不得建立或读取轮播配置、轮播控制和 last-cast `.txt`。测试 `start_slideshow` 后重启应恢复同一列表、startIndex 和 RTC anchor；`set_slideshow sw=0/1`、daily、本地浏览以及show=true cast应只更新NVS control；network upload固定show=false/save=true且不修改显示模式。cast成功应打印 `last cast saved`，cast2pic清理时应打印 `last cast state cleared`。启动时 CH583 UART 必须先于轮播模式协调和 USB 入口就绪；control/config generation 不一致或 control 无效必须禁用轮播并把 SLIDESHOW mode 协调为 NORMAL；DAILY 和 LOCAL_IMAGE_BROWSING mode 优先，不能被旧轮播 control 覆盖。独立的 `slide_random` key 不应再读写。NVS 写入、读回、CRC、generation 或容量异常使用 `ESP_LOGE`，状态协调使用必要的 `ESP_LOGW/ESP_LOGI`，不得为正常读取或未变化 control 逐条刷日志。

## 目录 <span id="toc"></span>

- [1. 测试约定](#sec-01)
- [2. PowerShell 测试用例](#sec-02)
  - [2.1 公共测试设置](#sec-02-1)
  - [2.2 cast：投屏业务模块](#sec-02-2)
  - [2.3 cast2pic：投屏转图片缓存 / 显示](#sec-02-3)
  - [2.4 delete：图片 / 缓存文件删除](#sec-02-4)
  - [2.5 net_data：通用网络数据](#sec-02-5)
  - [2.6 ota：设备在线升级](#sec-02-6)
  - [2.7 ping：网络连通检测](#sec-02-7)
  - [2.8 get_saved_images：本地图片列表](#sec-02-8)
  - [2.9 slideshow：图片轮播](#sec-02-9)
  - [2.10 slideshow_control：轮播控制](#sec-02-10)
  - [2.11 snapshot：图片与轮播状态](#sec-02-11)
  - [2.12 upload：图片上传](#sec-02-12)
  - [2.13 wifi_work_time：WiFi 工作时间](#sec-02-13)
  - [2.14 time：RTC 与网络时间](#sec-02-14)
  - [2.15 daily_download_file：每日一图](#sec-02-15)
  - [2.16 wifi_wakeup：首次连接失败后快速恢复](#sec-02-16)
  - [2.17 zlib 压缩文件 EPD 显示测试](#sec-02-17)
  - [2.18 Local Image Browsing 本地图片浏览测试](#sec-02-18)

## 1. 测试约定 <span id="sec-01"></span>

- JSON 接口优先使用 `Invoke-RestMethod`。
- `multipart/form-data` 使用 `curl.exe -F`，避免 Windows PowerShell 5.1 手工拼接 multipart。
- 测试日志只保留判断成功、失败和状态切换所需的关键信息。

## 2. PowerShell 测试用例 <span id="sec-02"></span>

### 2.1 公共测试设置 <span id="sec-02-1"></span>

Powershell 测试公共变量：

```powershell
# 设备 IP，按实际设备 IP 修改。
$esp = "http://192.168.1.104"

# 测试用文件路径，按实际 PC 文件修改。
$bin = "H:\AI2\test\26422.bin"
$jpg = "H:\AI2\test\26422.jpg"

# 计算 multipart 里要带的大小字段。
$binSize = (Get-Item $bin).Length
$jpgSize = (Get-Item $jpg).Length
```

说明：

```text
- JSON 接口优先使用 Invoke-RestMethod。
- multipart/form-data 接口使用 curl.exe -F，避免 Windows PowerShell 5.1 手工拼 multipart。
- 所有测试前先确认设备已经联网，且 $esp 能访问。
```

---

### 2.2 cast：投屏业务模块 <span id="sec-02-2"></span>

Powershell 测试用例：

```powershell
# cast：上传 bin + jpg，并立即显示。
$esp = "http://192.168.1.104"
$bin = "H:\AI2\test\26422.bin"
$jpg = "H:\AI2\test\26422.jpg"
$binSize = (Get-Item $bin).Length
$jpgSize = (Get-Item $jpg).Length

curl.exe -X POST "$esp/dataUP" `
  -F "func=cast" `
  -F "fileName=26422" `
  -F "bin_size=$binSize" `
  -F "image_size=$jpgSize" `
  -F "save=true" `
  -F "show=true" `
  -F "bin=@$bin;type=application/octet-stream" `
  -F "image=@$jpg;type=image/jpeg"
```

预期：`show=true` 时设备返回 `cast_received` 后立即结束 HTTP 响应，EPD 显示和保存由后台继续执行；`save=false` 会返回失败，不作为“只显示不保存”的 cast 用法。

统一任务与内存检查：请求成功后应出现 `image_worker: job start owner=CAST`，最终出现 `job done owner=CAST ... configured=12288`；不得出现 `cast_core: save task started`、`CastSaveTask`、`save_queue_full` 或保存等待semaphore相关日志。显示、保存BIN、保存JPG、写 `last_cast` 和清理旧文件的先后顺序必须与修改前一致。第二个network cast/cast2pic在第一个仍为current或pending时应返回BUSY，不得覆盖第一个body。投屏pending期间即使工作时间到期也不得关机，结束后 `image transfer pending count` 必须回到0。

---

### 2.3 cast2pic：投屏转图片缓存 / 显示 <span id="sec-02-3"></span>

Powershell 测试用例：

```powershell
# 网络 cast2pic：当前源码只支持 screen=a 或 screen=b，不写 screen=ab。
$esp = "http://192.168.1.104"
$bin = "H:\AI2\test\26423.bin"
$jpg = "H:\AI2\test\26423.jpg"
$binSize = (Get-Item $bin).Length
$jpgSize = (Get-Item $jpg).Length
$screen = "a"

curl.exe -X POST "$esp/dataUP" `
  -F "func=cast2pic" `
  -F "screen=$screen" `
  -F "fileName=26423" `
  -F "bin_size=$binSize" `
  -F "image_size=$jpgSize" `
  -F "save=true" `
  -F "show=true" `
  -F "bin=@$bin;type=application/octet-stream" `
  -F "image=@$jpg;type=image/jpeg"
```

预期：完整接收并校验后返回 `{"func":"cast2pic_result","result":0}`；显示和保存结果只写日志。

统一任务检查：请求成功后应出现 `image_worker: job start owner=CAST2PIC`；`screen=a -> EPD2 -> screen_b.bin/.jpg` 和 `screen=b -> EPD1 -> screen_a.bin/.jpg` 的映射不得变化。分别覆盖 `show/save=true/false` 原有组合，确认返回时机、返回码、显示顺序、临时文件rename及旧cast目录清理均不变化。

切换测试：分别让DAILY处于等待/下载、SLIDESHOW处于10秒启动延迟/运行、LOCAL_IMAGE处于pending/EPD已开始，然后发送network cast或cast2pic。pending旧owner应被原子替换；current旧owner只在安全点退出；已经开始的EPD不得强制中断。旧owner结束后只允许一个CAST/CAST2PIC运行，不得出现两个current owner、body重复释放、传输引用计数下溢或WiFi提前断电。

USB降级与网络优先测试：启动日志应出现 `usb_console_worker: inline mode enabled, no worker task or queue`，不得出现 `UsbConsoleWorker`任务创建日志。普通USB命令在USB接收任务当前上下文完成；USB cast/cast2pic分别提交 `owner=USB_CAST/USB_CAST2PIC`。在USB投屏仍为pending时发送network cast，网络owner应替换USB pending，USB cast收到一次 `network_priority` 失败；若USB EPD已经开始，不强制中断，网络请求按现有EPD BUSY规则处理。USB测试失败不得影响随后network cast/cast2pic成功。

---

### 2.4 delete：图片 / 缓存文件删除逻辑 <span id="sec-02-4"></span>

Powershell 测试用例：

```powershell
# delete：删除一张或多张已保存图片。
$esp = "http://192.168.1.104"
$body = @{
  func = "delete"
  fileNames = @("26422", "26423")
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

预期：设备返回 `delete_result`；删除成功后只删除对应 `.bin`、`.jpg`，不会修改 last cast / slideshow 相关配置。

---

### 2.5 net_data：通用网络数据封装 <span id="sec-02-5"></span>

Powershell 测试用例：

```powershell
# net_data：验证 /dataUP 小 JSON 入口能正确分发到 snapshot。
$esp = "http://192.168.1.104"
$body = @{ func = "get_snapshot" } | ConvertTo-Json

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

```powershell
# net_data：验证 /dataUP multipart 入口能正确进入 cast/upload/cast2pic 分发。
$esp = "http://192.168.1.104"
$bin = "H:\AI2\test\26422.bin"
$jpg = "H:\AI2\test\26422.jpg"
$binSize = (Get-Item $bin).Length
$jpgSize = (Get-Item $jpg).Length

curl.exe -X POST "$esp/dataUP" `
  -F "func=upload" `
  -F "fileName=net_data_test" `
  -F "bin_size=$binSize" `
  -F "image_size=$jpgSize" `
  -F "save=true" `
  -F "show=false" `
  -F "bin=@$bin;type=application/octet-stream" `
  -F "image=@$jpg;type=image/jpeg"
```

预期：第一个命令走 small JSON 分发；第二个命令走 multipart 分发。

---

### 2.6 ota：设备在线升级模块 <span id="sec-02-6"></span>

Powershell 测试用例：

```powershell
# ota：上传固件到 /ota；成功后设备固定自动复位。
$esp = "http://192.168.1.104"
$fw = "H:\AI2\ESP32-S3-PhotoPainter-main\01_Example\xiaozhi-esp32\build\xiaozhi.bin"
$size = (Get-Item $fw).Length
$version = "000.001"
$meta = '{"func":"ota","version":"' + $version + '","firmware_size":' + $size + ',"reboot":true}'

curl.exe -X POST "$esp/ota" `
  -F "meta=$meta" `
  -F "firmware=@$fw;type=application/octet-stream"
```

```powershell
# 兼容旧工具入口：/ota_upload。
curl.exe -X POST "$esp/ota_upload" `
  -F "meta=$meta" `
  -F "firmware=@$fw;type=application/octet-stream"
```

版本规则：当前代码不解析 `version` 的数字格式或范围。OTA meta 中 `version` 字段如果存在，只做字符串完全匹配，必须与固件 `app_desc.version` 完全一致，否则返回 `1711/version_mismatch` 并拒绝写入；不确定固件版本字符串时可以省略该字段。

预期：固件大小不能超过 OTA 分区；版本校验和固件校验通过后写 OTA 分区并设置 boot partition。成功响应中 `ota_event/stage=rebooting` 位于 `ota_result/result=0` 之前，`ota_result` 必须是最后一条 JSON；chunked response 正常结束、HTTP handler 返回后，OTA 专用任务按 `SERVER_NETWORK_STA_OTA_RESTART_DELAY_MS`（当前为 500 ms）自动复位。`reboot` 缺失时默认按 `true`；即使请求传入 `reboot=false`，设备也会打印警告并忽略该值，成功 OTA 必须复位。

关键日志应依次包含：

```text
stage=set_boot_ok
stage=write_done
stage=rebooting
func=ota_result result=0
finish ota stream ret=ESP_OK
ota final response complete result_last=1
ota restart pending delay_ms=500
HTTP data handler done
ota restart now
```

`ota restart pending` 由新任务打印，可能与外层的 `HTTP data handler done` 相邻交错，两者不要求固定先后；必须保证 `ota restart now` 位于它们之后。测试判定：`ASSOC_LEAVE` 出现在 `ota restart now` 之后属于设备主动复位，不作为 OTA 写入失败；若出现 `ota final response incomplete`，表示固件已完成写入和 boot partition 设置，但手机端可能没有收到完整的最终 HTTP 响应，应记录其中的 event/result/finish 返回值。

重启等待窗口测试：在 `ota restart pending delay_ms=500` 后立即再次发送 multipart 请求。`/ota` 或 `/ota_upload` 应返回现有 `1713/upload_busy` 且 message 为 `restart_pending`；`/dataUP` multipart 应返回现有 `1007` 且 message 为 `restart_pending`。GET `/ping` 不受该保护影响。

新固件启动确认测试：OTA 后第一次启动必须先出现一条 `net-ota-boot: pending verify`，其中包含 version、partition、addr、state 和 reset_reason；随后出现 `ota pending verify=1`。CH583 UART 本地初始化完成后、500ms时间同步等待及 WiFi连接之前，必须出现 `local image confirmation begin`、`ota pending verify=0` 和 `current image confirmed ... phase=local_ready`。确认耗时目标小于 `USER_OTA_LOCAL_CONFIRM_WARNING_MS=6000ms`，并且从 APP 收到成功 `ota_result` 到确认完成必须小于 `USER_OTA_CH583_POWER_CUT_LIMIT_MS=10000ms`。分别关闭路由器、设置错误密码、延迟DHCP和移除SD卡，确认均不依赖这些模块。若本地确认失败，必须出现 `ESP_LOGE`、pending hold 保持有效，并在 HTTP POST接口注册完成后执行原确认入口重试。

启动状态重试测试：模拟第一次 `esp_ota_get_state_partition()` 读取失败，work-time 初始化后必须在 GPIO test 前再读取一次；第二次读取成功时应正常打印 pending 诊断、设置 hold，并启用 OTA 首次启动容错。两次都失败时只保留两次模块级 `ESP_LOGE`，`main.c` 不重复打印同一错误。

普通启动测试：factory 分区、已确认的 OTA 分区均不得出现 `pending verify` 启动诊断，也不得设置 pending-verify hold；GPIO test、factory reset、PM 和其他业务保持原启动行为。

OTA 首次启动容错测试：分别模拟 `GpioTest_Init()` 和 `FactoryReset_Init()` 失败，应打印一次 `ESP_LOGE` 并继续到网络恢复及镜像确认；同样的失败发生在普通启动时，仍保持原有 `ESP_ERROR_CHECK()` 行为。当前 PM 配置的 `light_sleep_enable` 固定为 false；若镜像尚未确认，启动末尾还应打印一次 `keep light sleep disabled`。

---

### 2.7 ping：网络连通检测 <span id="sec-02-7"></span>

Powershell 测试用例：

```powershell
# ping：GET /ping 检查 HTTP 服务、设备身份和 EPD 忙闲状态。
$esp = "http://192.168.1.104"

function Assert-PingResponse($r) {
  foreach ($name in @("func", "result", "message", "EPD", "Ble_MAC")) {
    if ($null -eq $r.PSObject.Properties[$name]) {
      throw "ping missing field: $name"
    }
  }
  if ($r.func -ne "ping_result") { throw "ping func error: $($r.func)" }
  if (@("BUSY", "IDLE") -notcontains [string]$r.EPD) {
    throw "ping EPD error: $($r.EPD)"
  }

  if ([int]$r.result -eq 0) {
    if ($r.message -ne "ok") { throw "ping message error: $($r.message)" }
    if ([string]$r.Ble_MAC -notmatch '^[0-9A-F]{12}$') {
      throw "ping Ble_MAC error: $($r.Ble_MAC)"
    }
  } elseif ([int]$r.result -eq 1405) {
    if ($r.message -ne "Ble_MAC not ready") {
      throw "ping message error: $($r.message)"
    }
    if (-not [string]::IsNullOrEmpty([string]$r.Ble_MAC)) {
      throw "result=1405 but Ble_MAC is not empty"
    }
  } else {
    throw "ping result error: $($r.result)"
  }
}

$r = Invoke-RestMethod -Uri "$esp/ping" -Method Get
Assert-PingResponse $r
$r | ConvertTo-Json -Depth 5

# query 后缀也必须进入同一个 /ping handler。
$rQuery = Invoke-RestMethod -Uri "$esp/ping?t=123" -Method Get
Assert-PingResponse $rQuery
```

预期：响应固定包含 `func/result/message/EPD/Ble_MAC`。BLE MAC 已获取时返回 `result=0`、`message=ok` 和 12 位大写无冒号 MAC；尚未获取时返回 `result=1405`、`message=Ble_MAC not ready` 和空 `Ble_MAC`。两种情况下 `EPD` 都只能是 `BUSY` 或 `IDLE`。当前固件正式字段名固定为 `Ble_MAC`。

---

### 2.8 get_saved_images：取出本地存储图片 <span id="sec-02-8"></span>

Powershell 测试用例：

```powershell
# get_saved_images：读取 SD 卡中已保存缩略图列表。
$esp = "http://192.168.1.104"
$body = @{ func = "get_saved_images" } | ConvertTo-Json

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

```powershell
# 如果返回了 thumbnailUrl，可以继续读取缩略图。
$thumb = "/thumb/26422.jpg"
Invoke-WebRequest -Uri "$esp$thumb" -OutFile "H:\AI2\test\thumb_26422.jpg"
```

预期：返回 `get_saved_images_result`；`images` 中包含 `fileName` 和 `thumbnailUrl`。如果 `/data/jpg_img` 目录不存在，当前网络实现返回成功空列表：`{"func":"get_saved_images_result","result":0,"images":[]}`。

---

### 2.9 slideshow：图片轮播的文件列表，轮播间隔，是否随机 <span id="sec-02-9"></span>

Powershell 测试用例：

```powershell
# start_slideshow：下发并保存轮播列表。
$esp = "http://192.168.1.104"
$body = @{
  func = "start_slideshow"
  fileNames = @("26422", "26423")
  interval = 60
  random = $false
  timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
  startIndex = 0
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

预期：设备把 APP 最终发送的轮播列表、interval、强制为 false 的 random 和 startIndex 保存到 NVS `slide_cfg`，把标准 RTC control 保存到 NVS `slide_ctl`，两个记录 generation 相同，并按 `timestamp` 从 `fileNames[startIndex]` 建立第 0 个绝对时间槽。最终列表允许重复且最多 150 项；超过 150 项返回 1514，缺少 startIndex 返回 1515，`startIndex >= 最终 fileNames 数量` 返回 1516，任一文件不存在、不是普通文件或为空返回 1508。以上非法指令均不改动 RTC / 系统时间、持久配置、control、显示模式和现有轮播任务。

`random=true` 的 APP 侧测试：APP 将每个原始文件名复制 3 次，对扩展后的最终列表打乱，再把该最终列表发送给设备。设备允许重复、不再次随机，并按最终列表顺序播放；设备保存和 snapshot 返回的 random 仍为 false。

边界测试表：

| 场景 | 预期 |
|---|---|
| 最终列表包含重复名称 | 接受；相同名称的不同索引是不同播放事件 |
| 随机后两个相同名称相邻 | 在相邻两个 RTC 播放点分别调用 EPD |
| 最终列表 150 项，`startIndex=149` | 接受 |
| 最终列表 150 项，`startIndex=150` | 返回 1516，不修改现有状态 |
| 最终列表 151 项 | 返回 1514，不修改现有状态 |
| 任一重复项对应的 bin 不存在、非普通文件或为空 | 返回 1508，不修改现有状态 |
| 重启恢复包含重复名称的列表 | 使用保存的 `order[position]` 索引恢复，不得只按文件名判断当前槽是否已显示 |
| 150 个 16 位文件名且完整 JSON 不超过 4096 字节 | 网络端可接收 |
| 完整 JSON 超过 4096 字节 | 在网络 small JSON 入口拒绝；数量未超过 150 也不能绕过 body 上限 |
| 基础文件名为 1 字节或 16 个安全 ASCII 字节 | 接受并保持原名称，不截断 |
| 任一基础文件名为 17 字节 | 返回 1502；不改 RTC、配置、control、NVS、模式和现有轮播任务 |
| cast/cast2pic/upload 的业务 `fileName` 为 17 字节 | 返回该接口现有文件名非法结果；不显示、不保存 |
| delete 任一名称为 17 字节 | 返回 1502；整批不删除任何文件 |
| SD 中存在升级前的超长名称 | get_saved_images、snapshot和本地浏览跳过该项；不得截断成另一名称；恢复出厂仍应能清理旧文件 |
| legacy multipart fallback 的 `filename` 基础名为 17 字节 | 保存前拒绝；不得在 bin_img/jpg_img 中生成文件 |

运行中首次取得 SNTP 的防重复测试：使用 A、B、C 三个不同文件并设置 `interval=300`，启动时暂时阻止 SNTP、保留 CH583/RTC 可用，让 A 完成一次 EPD 显示，再于完成后 10～30 秒恢复 SNTP。预期切换日志包含 `current_consumed=1 action=wait_next`，A 不再次进入 EPD，B 等待下一个绝对播放点。反向测试应在 A 尚未显示前恢复 SNTP，预期日志包含 `current_consumed=0 action=display_current`，当前绝对槽仍正常显示。

轮播 runtime 内存测试：16 字节基础名规则下，150 项名称数组应为2550字节，runtime约2.9KB并优先使用PSRAM。启动早期只应打印一次 `image_worker: static resources ... stack=12288` 和 `image_worker: started stack=12288`；不得再出现 `slideshow static worker started`、独立daily/local/cast保存/USB worker或 `slide_start_delay` 任务创建日志。轮播、每日一图、轮播启动延迟、本地浏览、投屏及恢复出厂执行分别完成后，应出现对应 `image_worker: job done owner=SLIDESHOW/DAILY/LOCAL_IMAGE/CAST/CAST2PIC/FACTORY_RESET ... min_free=... peak_used=... configured=12288`。Factory Reset原5KB检测任务本阶段仍应存在，且只做GPIO28/PB1检测和统一任务提交。每条完整路径 `min_free` 必须不少于2048字节。连续启动/停止至少100次，确认统一worker始终常驻、没有堆持续下降、没有queue永久BUSY、reservation泄漏、投屏body泄漏或重复释放runtime。新命令runtime失败仍返回1506并回滚控制及模式；开机恢复临时失败仍保留 `slide_ctl.enabled=true`、`epd_mode=SLIDESHOW` 和原进度。

统一worker启动延迟抢占测试：保存SLIDESHOW模式后重启，在 `slideshow startup delay 10000 ms` 出现后的10秒内下发合法daily命令或在EPD IDLE时触发PB2。预期旧轮播generation立即失效，延迟等待被唤醒并打印取消，更新的DAILY或LOCAL_IMAGE命令随后由同一 `image_worker` 执行；10秒到点后不得再启动旧轮播runtime。反向切换时使用非阻塞stop和pending接续，任何时刻不得同时存在两个current owner。

统一worker恢复出厂优先级测试：分别在DAILY等待/下载、SLIDESHOW等待/刷新、LOCAL_IMAGE pending/current、CAST/CAST2PIC pending/current时触发PB1。请求入口不得等待current owner；尚未运行的普通pending命令应被FACTORY_RESET替换并正确执行cancel callback，DAILY/SLIDESHOW/LOCAL_IMAGE应由原stop/generation机制退出，已经开始的EPD或投屏事务安全结束后必须紧接 `job start owner=FACTORY_RESET`。FACTORY_RESET pending/current期间再次提交任一普通图片owner必须返回BUSY且不能覆盖恢复出厂；重复PB1仍只保留一个请求。模拟统一任务提交临时失败，确认状态由RUNNING恢复PENDING、guard保持、只打印一次deferred警告并由300ms检测周期重试。

轮播停止与预加载竞争测试：在 `slideshow preload start` 后立即发送 `set_slideshow sw=0`。允许出现 `preload skipped ... because stop requested` 的普通信息，但不得再出现 `slideshow preload initial/next failed ... ESP_ERR_INVALID_STATE` 警告；真实预加载失败仍必须保留警告。

当前实现：

```text
start_slideshow 是正式轮播列表配置接口；会先完整校验，再依次写入版本化 NVS `slide_cfg` 和标准 RTC `slide_ctl`。两个记录用 generation 防止掉电后混用新旧状态；SD 中不生成任何 control 文件。
网络 JSON 的 `func` 判断支持空格、CRLF 和 PowerShell `ConvertTo-Json` 输出格式。
```

---

### 2.10 slideshow_control：轮播控制模块 <span id="sec-02-10"></span>

Powershell 测试用例：

```powershell
# set_slideshow：开启轮播并设置间隔。
$esp = "http://192.168.1.104"
$body = @{
  func = "set_slideshow"
  sw = 1
  interval = 60
  random = $false
  timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

```powershell
# 关闭轮播。
$body = @{
  func = "set_slideshow"
  sw = 0
  interval = 60
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

预期：`sw=1` 开启，`sw=0` 关闭；配置写入默认 NVS `image_state:slide_ctl`。

当前实现：

```text
set_slideshow 的 sw=1 会更新 NVS `slide_ctl`、同步写 `epd_mode=1(SLIDESHOW)`，并尝试启动轮播。
若旧轮播正在运行，`set_slideshow sw=1` 写入新的 interval / timestamp 后会按 RTC 计算出的 next_epoch 重新同步；如果 EPD 正在显示，不打断本次显示，显示完成后继续按 RTC 下一个播放点等待，等待期间会预加载下一张。
`ServerNetworkStaSlideshow_GetScheduleTiming()` 可读取当前 RTC 轮播的 now_epoch / next_epoch / remain，snapshot 和关机前 WAKE_TIMER 计算优先使用这组值。
轮播省电测试：EPD和`save_slideshow_progress()`成功后必须先建立下一张progress及`next_epoch`，再出现`slideshow power off requested`；下一张SD预读仍须完成。分别制造remain=41和remain=40，前者应进入WAKE_TIMER/POWER_OFF安全链路，后者应打印`one-shot power off skipped because slideshow next is soon`且不得发送POWER_OFF。允许关机时日志应显示`startup_delay=10 extra_advance=15 wake_advance=25`。EPD失败、progress保存失败、stop置位或模式切离SLIDESHOW时不得申请；申请后切换NORMAL/DAILY/LOCAL时必须取消SLIDESHOW owner，不能影响新业务。
存储未就绪、未保存轮播列表、参数非法、interval 非法、NVS control 保存失败、轮播 runtime 启动失败、timestamp 非法、timestamp 写 RTC 失败、SNTP 时间差过大已分别返回 1012、1501、1004、1507、1509、1506、1510、1512、1513。control 保存后若模式保存失败或 runtime 启动失败，必须停止轮播并尝试回滚为 `slide_ctl.enabled=false`、`epd_mode=NORMAL`；回滚异常打印一条 `ESP_LOGE`。
网络 JSON 的 `func` 判断支持空格、CRLF 和 PowerShell `ConvertTo-Json` 输出格式。
```

---

### 2.11 snapshot：读取图片列表和轮播状态 <span id="sec-02-11"></span>

Powershell 测试用例：

```powershell
# get_snapshot：一次读取图片列表和轮播状态。
$esp = "http://192.168.1.104"
$body = @{ func = "get_snapshot" } | ConvertTo-Json

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

预期：返回 `get_snapshot_result`，包含 `images` 和 `slideshow`。

---

### 2.12 upload：PC或手机传文件到ESP32-C5，并存 <span id="sec-02-12"></span>

Powershell 测试用例：

```powershell
# upload：上传图片到 SD 卡，不立即显示。
$esp = "http://192.168.1.104"
$bin = "H:\AI2\test\26424.bin"
$jpg = "H:\AI2\test\26424.jpg"
$binSize = (Get-Item $bin).Length
$jpgSize = (Get-Item $jpg).Length

curl.exe -X POST "$esp/dataUP" `
  -F "func=upload" `
  -F "fileName=26424" `
  -F "bin_size=$binSize" `
  -F "image_size=$jpgSize" `
  -F "save=true" `
  -F "show=false" `
  -F "bin=@$bin;type=application/octet-stream" `
  -F "image=@$jpg;type=image/jpeg"
```

预期：资源空闲时设备返回上传成功结果；network upload只接受 `show=false && save=true`，只保存、不进入EPD显示队列。`show=true`或`save=false`必须返回错误且不得产生文件。

UPLOAD Gate与ping回归：

```text
1. 网络/USB ping必须对同一设备状态返回相同EPD值。
2. NORMAL且无pending、EPD/SD/SPI空闲时返回IDLE。
3. DAILY或SLIDESHOW等待下一时间点且实际资源空闲时允许IDLE，不能因长期current owner永久BUSY。
4. DAILY下载/保存/显示、SLIDESHOW读图/显示、LOCAL_IMAGE、CAST、CAST2PIC、USB投屏、pending命令、EPD队列、Shared SPI、EPD/SD已断电或电源切换、Factory Reset、OTA及另一个UPLOAD预约期间返回BUSY。
5. EPD/SD进入POWER_OFF、PREPARING或RESTORING期间，网络和USB ping必须返回BUSY，不得等待或触发供电恢复；恢复到IDLE/ARMED、WAIT_DECISION或POWER_OFF_COMMITTED后必须返回IDLE。
6. 先ping得到IDLE，再在upload到达前启动图片业务，ESP32最终预约必须返回1007且不保存。
7. POWER_OFF/PREPARING/RESTORING期间直接发送非OTA multipart，必须在读取body前返回1007，不触发供电等待，也不保存。
8. 两个network upload并发时只能一个预约成功，另一个立即1007，不等待。
9. 注入SD满、写入、校验、rename及HTTP断开失败；临时文件按原规则清理，随后ping必须恢复IDLE。
10. 启动日志和任务列表不得再出现dataup_async_worker；内部RAM不再分配其12KB栈和队列。
11. GPIO4 LOW期间连续请求网络`/ping`和`/time`，两者都必须及时响应；`/ping`固定返回`EPD=BUSY`，不得触发GPIO4提前HIGH。
12. GPIO4 LOW期间请求旧文件下载/上传/删除，必须立即返回HTTP 503、`result=1007`、`error=sd_power_busy`、`EPD=BUSY`和`Retry-After: 1`，不得在HTTP handler中等待完整2秒。
13. GPIO4 LOW期间分别发送`get_saved_images/get_snapshot/start_slideshow/set_slideshow/delete/daily_download_file`，必须返回`dataup_result/1007/sd_power_busy/EPD=BUSY`；发送`set_wifi_work_time`必须继续正常处理。
14. GPIO4 LOW期间执行OTA请求和CH583 PING/PONG、DEVICE_INFO、KEY_EVENT、ACK/ERR回归，网络和UART通信必须继续；依赖EPD/SD的后续业务只能BUSY或等待底层恢复，不能访问未供电设备。
15. 从`EPD=BUSY`轮询到`EPD=IDLE`后重试原SD请求，必须成功；日志`actual_off_ms`必须不小于2000，且不得出现请求触发的提前恢复。
```

文件保存 16 KiB stdio 缓冲回归测试：

```powershell
# 连续上传 20 次，文件名保持在 16 字节业务上限内。
1..20 | ForEach-Object {
  $name = "memtest{0:d4}" -f $_
  curl.exe -X POST "$esp/dataUP" `
    -F "func=upload" `
    -F "fileName=$name" `
    -F "bin_size=$binSize" `
    -F "image_size=$jpgSize" `
    -F "save=true" `
    -F "show=false" `
    -F "bin=@$bin;type=application/octet-stream" `
    -F "image=@$jpg;type=image/jpeg"
  Start-Sleep -Seconds 1
}
```

同步验证网络 `cast` 的 `show=true/save=true`、USB multipart upload 和 USB raw upload。每条路径分别使用小文件、实际屏 BIN/zlib 和接近业务上限的文件，并检查：

```text
- 每次保存前后都有 file memory 日志，io_buffer=16384。
- 正常路径没有 setvbuf failed、file io buffer unavailable 或 file write failed。
- 文件大小和原文件一致，可继续显示或轮播，不遗留 .tmp 文件。
- 连续 20 次后 internal/PSRAM free 和 largest block 不持续下降。
- 模拟存储不可写或写入中断时，fwrite/fclose 失败必须返回原有保存失败，临时文件得到清理。
```

---

### 2.13 wifi_work_time：WiFi 省电管理 <span id="sec-02-13"></span>

Powershell 测试用例：

```powershell
# set_wifi_work_time：设置 WiFi 保持工作时长，单位秒。
$esp = "http://192.168.1.104"
$body = @{
  func = "set_wifi_work_time"
  seconds = 300
} | ConvertTo-Json

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

预期：设备保存 WiFi 工作时长配置，并重置工作计时；请求本身更新 HTTP 活动时间，最后一次 HTTP 活动后的 20 秒内不关机；超时后由 wifi_work_time 模块结合 CH583、OTA、EPD、图片保存和轮播保护决定是否发送 CH583 `POWER_OFF`。

EPD 完成低功耗倒计时：

```text
USER_EPD_DONE_LOW_POWER_ENABLE 默认 0。
USER_EPD_DONE_LOW_POWER_DELAY_SECONDS 默认 5 秒。
USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS 默认 40 秒。
```

普通EPD完成自动倒计时默认关闭；开启后只修改RAM计时，不写NVS。每日一图、轮播和本地图片浏览分别使用DAILY、SLIDESHOW和LOCAL_IMAGE专用one-shot，不受该开关控制；owned one-shot在模式切离对应业务后恢复原工作时间并取消。轮播下一播放点剩余时间不大于40秒时也取消本次one-shot并继续运行。

正式EPD/SD独立掉电验收：

```text
1. 本地浏览和每日一图正常关机、轮播remain=41秒时，应出现立即POWER_OFF决定，不能出现mandatory power cycle started；轮播唤醒仍为remain-25秒。
2. 轮播remain=39秒和40秒、owner模式切换、显示后状态保存失败时，应出现stay awake决定和mandatory power cycle started，且不得发送POWER_OFF。
3. 从GPIO4拉低后的started日志到恢复日志，actual_off_ms必须>=2000；分别使用13.3英寸和7.9英寸EPD验证。
4. GPIO4 LOW期间注入HTTP、CH583 BLE_DATA、新EPD请求和普通共享SPI/SD访问，只允许打印一次等待警告，不得提前恢复；阻塞调用在GPIO4恢复、稳定100ms和SD重挂载后继续。
5. 注入POWER_OFF UART写入失败，应撤销立即关机豁免、回滚LED/WAKE_TIMER并执行完整2秒独立掉电；独立掉电完成前不得再次进入关机发送。
6. 注入SD卸载、IO隔离、GPIO4操作或SD重挂载失败，必须使用ESP_LOGE；准备阶段失败要在恢复后重试正式掉电，恢复失败时保持BUSY并定时重试。
```

启动EPD/SD供电与SD快速挂载验收：

```text
1. 冷启动日志必须出现`EPD/SD startup rail enabled stable_ms=100 power_reset_skipped=1`，随后出现`SD mount start wait_ms=10`。
2. 启动期间不得出现`EPD/SD rail IO isolated`、GPIO4 level=0或`startup rail reset completed`；示波器确认ESP32启动代码没有产生GPIO4 LOW脉冲。
3. 连续冷启动至少20次，并使用不同品牌和容量的SD卡重复测试；首次挂载应成功，不应依赖第2/3次重试。
4. 如果10ms设置出现偶发首次挂载失败，只调整挂载等待时间，不得恢复启动断电复位，也不得修改正式2000ms独立掉电规则。
5. 启动`storage ready`之前仍须完成必要目录检查；SD总容量和剩余容量统计不得阻塞首次挂载或本地图片任务。
```

Factory Reset 白屏保持及客户下次开机测试：

```text
1. 通过 USB 或 BLE 保存有效 WiFi，准备 upload、cast、轮播和 daily 数据。
2. 把 epd_mode 设为 SLIDESHOW 或 DAILY，长按 GPIO28 约 5.1 秒。
3. 确认图片、轮播配置、daily_cfg、wifi namespace 和 nvs.net80211 的 sta.ssid/sta.pswd 均被清除。
4. 确认 PhotoPainter:epd_mode 已写入 USER_EPD_DISPLAY_MODE_DEFAULT，并能读回相同值。
5. 确认清理成功后先写入并读回 `PhotoPainter:fr_welcome=1`，再使用 `WHITE=0x11` 显示白屏并等待实际完成。
6. 白屏完成后确认ESP32继续运行并保持白屏，不得出现 `factory reset CH583 power cycle requested`、Factory Reset专用 `WAKE_TIMER ON,10`、`POWER_OFF` 或新的 `app start`。
7. 由客人正常关机后再次开机。启动时只能出现GPIO4置为HIGH并稳定100ms的日志，不得出现IO隔离、GPIO4 LOW或启动电源复位；SD挂载入口等待10ms后完成SD/SPIFFS存储挂载和Factory Reset初始化，再在网络业务启动前读取标志、解压并显示固件内置欢迎图。约40～50秒显示成功后删除 `fr_welcome`，随后继续默认显示模式和未配网启动。正常SD卡首次挂载必须成功；存储未就绪时不得显示欢迎图或清除标志。EPD type、WiFi工作时间、BLE MAC和OTA状态保持不变。
8. 分别使用首次 DEVICE_INFO(KEY_PB1) 和合法 KEY_EVENT(PB1,PRESS) 重复以上测试，确认与 GPIO28 使用同一清理、白屏保持和下次开机欢迎图流程；KEY_EVENT应分别覆盖DEVICE_INFO之前和之后。
9. 客人下次开机的 DEVICE_INFO 按实际硬件唤醒原因上报，Factory Reset不再要求固定为TIMER，也不得因为旧KEY_PB1重复执行恢复出厂。
10. 确认正式执行依次出现 `factory reset worker submitted`、`image_worker: job start owner=FACTORY_RESET`、标志保存/白屏日志和 `job done owner=FACTORY_RESET ... configured=12288`；`min_free` 必须不少于2048字节。客人下次开机还应出现startup welcome开始及标志清除日志。
```

短按GPIO28后松开不得设置Factory Reset guard，也不得影响普通关机。连续低电平达到5秒后，应先设置guard再开始删除；把普通 `wifi_work_time`设置到即将超时时，恢复出厂清理及白屏期间不得出现普通 `WAKE_TIMER` 或 `POWER_OFF`。成功或失败后都必须清除guard，但成功后也不得提交Factory Reset专用关机请求；普通关机不能因guard永久失效。

恢复出厂无论成功或失败都不得提交Factory Reset专用关机请求。模拟文件删除失败时，应继续尝试剩余文件，最终 `file_delete_failed>0`、总 `ret!=ESP_OK`，且不设置新标志、不显示白屏。再分别模拟标志读回不一致、PSRAM不足和白屏显示失败，确认 `welcome_pending_ret`或`white_display_ret`失败且guard仍被清除；标志已保存后发生的失败必须保留标志。客户下一次开机模拟欢迎图尺寸、解压或显示失败时，标志不得删除；显示成功但删除标志失败时，下次启动允许重复显示。文件原本不存在不算失败。普通one-shot、wifi_work_time、OTA、daily和其他既有关机规则不变。

---

### 2.14 time：RTC 默认时间与 SNTP 网络校时 <span id="sec-02-14"></span>

Powershell 测试用例：

```powershell
$esp = "http://192.168.1.104"
Invoke-RestMethod -Uri "$esp/time" -Method Get
Invoke-RestMethod -Uri "$esp/time?t=123" -Method Get
```

预期：SNTP 已同步返回 `result=0`；系统时间有效但本次启动尚未完成 SNTP 时返回 `result=1`；时间无效返回 `result=2`。这三个值是 `/time` 当前代码的状态值，不使用通用错误码区间。

---

### 2.15 daily_download_file：每日一图 <span id="sec-02-15"></span>

```powershell
# 当前 EPD 必须是 1600x1200；其他屏型要改成对应 width/height。
$esp = "http://192.168.1.104"
$future = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 300
$body = @{
  func = "daily_download_file"
  imageHeight = 1600
  imageWidth = 1200
  orientation = 0
  api_url = "https://www.esmart-link.com/digitalPhotoFrameInternal/dailyImage/dailyImageSelect"
  timestamp = $future
  sw = 1
} | ConvertTo-Json

Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

前提：本次启动必须已经完成 SNTP，且 `timestamp` 必须严格大于设备当前网络时间。成功返回只表示配置保存、停止轮播、切换 DAILY 模式和提交 worker 成功；首次下载与显示立即在后台执行。

边界测试：将 `$future` 改为当前时间加100秒。预期首次仍立即执行；该timestamp对应的正式时间槽因距离首次EPD调用不足300秒，被推迟到首次EPD调用满300秒后执行。失败后的重试时间仍为1小时。

关机回归：成功显示并保存完成状态后、下载或EPD失败并保存一小时重试后，都应通过DAILY专用入口申请one-shot；每20秒重复检查不得重置已激活的1秒截止时间。one-shot等待期间切换SLIDESHOW、LOCAL_IMAGE或NORMAL，必须打印DAILY owner取消日志并恢复原工作时间，不得由旧DAILY请求关闭新业务。

内存诊断：确认生成配置中 `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=98304`、`CONFIG_MBEDTLS_AES_C=y`，并且 `CONFIG_MBEDTLS_HARDWARE_AES` 与 `CONFIG_MBEDTLS_AES_USE_INTERRUPT` 均未设置；硬件SHA、MPI和ECC仍为开启。启动早期应打印一次 `image_worker: static resources`，确认 `stack=12288`，并列出统一TCB、单pending命令槽、状态mutex和内部RAM；随后打印 `image_worker: started stack=12288` 和 `daily_image: base initialized ... shared_worker=resident`。不得再有8KB `cast_save` 或8KB `UsbConsoleWorker`任务资源。每次DAILY HTTPS POST/GET前的原有 `TLS heap before` 日志必须包含 `internal_free/internal_largest`、`dma_free/dma_largest`、`psram_free/psram_largest` 和 `aes=software`。先完成一次“LOCAL_IMAGE明确保持开机→EPD/SD正式独立掉电→SD重新挂载→立即DAILY”的复现顺序，再至少连续执行20次完整DAILY下载；即使 `dma_largest` 降到3968字节附近，也不得出现 `esp-aes: Failed to allocate memory`、普通malloc失败、WiFi异常或PSRAM持续下降。另需回归CAST、CAST2PIC、network upload、OTA HTTPS和WiFi重连，并记录软件AES下的下载时间及CPU看门狗状态。所有owner完整周期的 `min_free>=2048` 才可保留12KB；低于2048时继续增加统一栈，不得先压缩任务栈。

关闭请求：

```powershell
$body = @{ func = "daily_download_file"; sw = 0 } | ConvertTo-Json
Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

关闭成功后 `epd_mode=0(NORMAL)`，旧 `daily_cfg` 保留，daily命令被取消；12KB统一静态worker永久常驻并重新进入空闲等待。

---

### 2.16 wifi_wakeup：首次连接失败后快速恢复 <span id="sec-02-16"></span>

测试目标：复现第一次 `NO_AP_FOUND`、底层 manager 随后重试并很快 READY 的场景，确认手机不会先收到错误的 `1307`。

步骤：

```text
1. 保留有效的已保存 WiFi 配置。
2. 发送 {"func":"wifi_wakeup"}。
3. 让第一次连接产生 reason=201(NO_AP_FOUND)，随后恢复 AP，使下一次重试成功。
4. 持续记录 CH583/BLE JSON 和 server_network_sta 日志。
```

预期关键日志：

```text
wifi_wakeup_result/result=0/stage=connecting
WiFi disconnected reason=201(NO_AP_FOUND)
wifi_wakeup early 1307 suppressed grace_ms=10000 state=RETRY_WAIT
WiFi GOT_IP ip=<IPv4>
Network READY ip=<IPv4>
wifi_wakeup recovered before notify timeout
wifi_info_result
```

判定：

- 从命令受理到 READY 之间不得发送 `wifi_wakeup_result/result=1307`。
- 过早 1307 后 10 秒内 READY 时，最终通知必须是 `wifi_info_result`。
- AP 持续不可用且 10 秒宽限到期时，仍应只发送一次原 `wifi_wakeup_result/result=1307`。
- 普通 `wifi` 配网的 `wifi_result` 行为必须保持不变。

立即重新配网测试：在 `wifi_wakeup` 的 CONNECTING、RETRY_WAIT 和10秒宽限阶段分别发送新的 `{"func":"wifi","ssid":"...","key":"..."}`。预期不返回BUSY，而是返回 `wifi_result/result=0`、`message=WiFi config saved and queued`；旧wakeup不再发送尚未产生的1307或成功通知，worker随后只应用最新保存的配置。通过测试钩子延长NVS保存时间，使wakeup worker恰好在保存期间完成，预期worker等待SAVING发布为READY，不得出现“配置已保存但返回BUSY”。普通 `wifi` worker自身忙时仍应返回原BUSY。

并发提交测试：让BLE和CH583在同一调度窗口分别提交WiFi请求，只允许一方占用 `submit_in_progress` 并创建worker；另一方返回BUSY且不得清除成功请求的WiFi connect guard。日志中只应出现一条对应受理请求的guard启动记录。

关机保护测试：冷启动时保持AP不可用并把工作时间设为20秒，确认同步联网尚在45秒窗口内时不得出现 `POWER_OFF`；快速READY或明确终止应清除启动guard。再让手机 `wifi` 或 `wifi_wakeup` 在 `app_main()` 自动联网前先建立guard，预期冷启动只打印 `power guard reused remaining_ms=...`，不得再次打印新的45秒started日志，最终deadline仍从手机请求开始计算。对BLE/CH583请求同样让工作时间先达到20秒，45秒绝对窗口到期前不得出现 `POWER_OFF`，应只打印一次 `power off postponed by WiFi connect guard`；worker路径在READY或明确终止时应立即清除guard。还要先让manager进入CONNECTING/RETRY_WAIT，再发送 `wifi_wakeup`，确认未创建新连接请求但同样设置最多45秒的自动到期guard。持续失败时，guard从单次请求接收起不得超过45秒。还需分别在初始关机判断、LED关机准备和SPI锁前制造并发请求，验证三层guard都能阻止或取消关机。

凭据日志测试：当前开发配置 `SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT=1`，分别从 `wifi` namespace和 `nvs.net80211` fallback读取有效配置，日志应出现 `WiFi credential loaded ssid=<SSID> password=<PASSWORD>`。把宏改为0后，日志只能输出SSID。正式发布检查必须确认宏为0。

### 2.17 zlib 压缩文件 EPD 显示测试 <span id="sec-02-17"></span>

准备：

1. 确认存储类型是 SD 卡，不是 SPIFFS fallback。
2. 确认 SD 卡存在之前生成的 `/bin_img/2486aad8763e9822.bin.zlib`。
3. 确认 `USER_EPD_DISPLAY_DATA_ZLIB_ENABLE=1`。
4. 将 `main.c` 中包围 `TdxZlibEpdTest_Run(base_path)` 的局部 `#if 0` 临时改为 `#if 1`，启动设备并查看串口日志。测试完成后恢复为 `#if 0`。

成功时关键日志应包含：

```text
I zlib-epd-test: compressed EPD test start path=/data/bin_img/2486aad8763e9822.bin.zlib size=<压缩长度>
I epd_display: zlib display decode passed input=<压缩长度> output=<屏幕原始长度> elapsed_ms=<解压耗时>
I epd_display: EPD queued wait target=1 size=<屏幕原始长度>
I epd_display: EPD done ... result=ESP_OK ...
I zlib-epd-test: compressed EPD test passed input=<压缩长度> elapsed_ms=<解压和显示总耗时>
```

判定：屏幕正确显示原图且上述日志全部成功。若解压输出不是当前EPD类型要求的 `display_size`，必须使用 `ESP_LOGE` 拒绝入队。测试函数读取SD文件后先释放共享SPI锁，再同步等待EPD任务，测试期间不得出现共享SPI死锁。

当前没有增加临时测试宏。`main.c` 完整保留 `TdxZlibEpdTest_Run(base_path)` 的启动调用代码，并使用局部 `#if 0` 关闭；如需重新验证，临时改为 `#if 1`，测试完成后恢复为 `#if 0`。正式功能宏保持不变。

接收长度测试：分别通过cast、cast2pic和upload发送压缩后长度不同的合法zlib BIN，`bin_size`填写各自压缩后的实际长度。设备不得因为该长度不等于屏幕原始 `display_size` 而拒绝；把 `bin_size` 故意改成与实际 `bin` part长度不同，设备仍必须返回原有大小不匹配错误。将正式宏改为 `0` 后，使用未压缩BIN复测旧流程。

### 2.18 Local Image Browsing 本地图片浏览测试 <span id="sec-02-18"></span>

准备：在 `/data/bin_img` 放入三个可正常显示的非空 zlib BIN，例如 `A.bin/B.bin/C.bin`，同时放入 `.jpg`、`.BIN`、`.bin.zlib`、空 `.bin` 和子目录作为过滤样本。确认正式模式值为 `3`，不启用临时测试宏。

DEVICE_INFO按键测试：发送严格五字段帧 `ARG=AABBCCDDEEFF,100,d,40,KEY_PB2`，确认先返回匹配ACK，再出现本地浏览accepted/selected/display completed日志；冷启动发送 `KEY_PB1`，确认ACK后只生成一个Factory Reset pending请求并执行现有恢复出厂。分别发送BOOT、USB、BLE_CONNECT、BLE_WRITE、NFC、TIMER、UNKNOWN，必须正常ACK但不执行按键业务。旧四字段、未知wake_reason、KEY_PB3/KEY_PB4、空字段或第六字段必须返回BAD_ARG。

DEVICE_INFO重发测试：分别重发同一上电会话的 `DEVICE_INFO(...,KEY_PB2)` 和 `DEVICE_INFO(...,KEY_PB1)`，必须继续回复ACK，但不得再次换图或再次执行恢复出厂；断电重新供电后首次DEVICE_INFO允许重新执行业务。

KEY_EVENT测试：无论DEVICE_INFO是否完成，分别发送 `PB2,PRESS` 和 `PB1,PRESS`，前者ACK后只尝试一次换图，后者ACK后只提交一次恢复出厂。相同SEQ重发必须继续ACK但不重复业务；`PB3,PRESS`、`PB4,PRESS`、`PB1,RELEASE`、仅`PB1`、错误PART/TOTAL或LEN均不得执行。

模式互斥测试：分别从NORMAL、SLIDESHOW和DAILY触发PB2。EPD IDLE时最终 `epd_mode=3` 并读回一致；SLIDESHOW任务退出、show_control sw=0，DAILY generation失效且不再提交EPD。EPD BUSY时触发不得停止旧模式或保存模式3。

统一worker切换测试：PB2成功预约EPD后应出现 `image_worker: job start owner=LOCAL_IMAGE`，不得出现 `local_img`任务创建日志。PB2切换到LOCAL_IMAGE的请求入口不得调用 `StopAndWait()`；反向切换不得等待LOCAL_IMAGE owner或当前EPD刷新，旧owner通过generation/stop退出，统一worker随后串行执行pending新owner，任何时刻不得同时出现两个current owner。轮播内部原有的DAILY/SLIDESHOW重启保护不属于LOCAL_IMAGE等待，不在本次改动范围内。

启动交界测试：在CH583 UART已经启动、LocalImageBrowsing初始化尚未完成时发送合法KEY_PB2 DEVICE_INFO，确认日志显示deferred，初始化后尝试一次；不得出现永久pending。相同DEVICE_INFO重发不得在启动FIFO中增加第二项。

恢复出厂互斥测试：PB2显示期间发送PB1恢复请求，确认PB1协议帧先ACK、请求保持PENDING，EPD空闲后Factory Reset取得预约并执行；不能与BIN文件读取、显示或本地浏览NVS更新并行。清理期间重复PB1必须合并。

循环测试：连续在每次EPD恢复IDLE后触发PB2，预期按稳定顺序显示A、B、C、A。每次成功显示后读取 `PhotoPainter:local_img_state`，确认transaction=IDLE、last/next和成功次数同步推进。

缺失文件测试：让扫描选中B后，在扫描与读取之间删除B，预期重新扫描、打印skip并在同一次请求中显示C；连续删除多个候选时继续跳过，最多尝试首次扫描的有效文件数量。全部候选消失时打印一次错误并结束，不死循环。内存不足、SPI错误、读取不完整或解压失败不得作为NOT_FOUND批量跳图。

断电恢复测试：A成功后断电，重启确认模式3恢复但不自动刷新；下一次PB2显示B。制造PREPARED=B后在EPD完成前断电，重启后下一次PB2重试B。破坏state版本、长度或CRC，预期ESP_LOGE并从安全默认游标恢复。

本地浏览省电测试：A显示成功、`local_img_state`变为IDLE并推进next、EPD reservation释放后，应出现`local image power off requested`并进入公共安全关机链路。LOCAL_IMAGE的`target=1`必须允许在`elapsed=1`进入超时判断，final guard和locked guard在没有其它活动时也不得以`timer=1`取消本次关机；DAILY、SLIDESHOW和普通工作时间仍保持原严格大于规则。分别注入EPD失败、状态保存失败、generation替换和模式切换，均不得由旧LOCAL请求关机；申请后切换其他模式时必须取消LOCAL_IMAGE owner。确认模块内没有直接调用`ch583_wifi_uart_send_power_off()`，HTTP、CH583、OTA、EPD、保存和最终SPI guard全部保留。

启动早期测试：让 `DEVICE_INFO(KEY_PB2)` 在LocalImageBrowsing_Init之前到达并成功ACK，确认进入启动FIFO并在模块初始化后执行一次。让 `DEVICE_INFO(KEY_PB1)` 在FactoryReset_Init之前到达，确认只保存一个RAM pending请求并在任务就绪后执行。

本地图片响应计时测试：正常请求必须输出`local image prepare completed`，包含`scan_ms/state_ms/read_ms/size/read_kib_s/key_to_epd_call_ms`。分别记录已开机按键和冷启动延后按键至少20次，确认压缩文件直接读入现有PSRAM缓冲，不申请额外内部RAM中转缓冲，并保持完整长度、PREPARED事务、zlib解压、EPD预约和关机规则；统一worker的`min_free`仍须不小于2048字节。

独立重启兼容测试：本轮不发送DEVICE_INFO，分别发送 `PB2,PRESS` 和 `PB1,PRESS`，必须正常ACK并分别执行一次本地浏览和恢复出厂，不得返回 `DEVICE_INFO_REQUIRED`。再在DEVICE_INFO完成后重复两条新SEQ事件，确认处理规则一致；BLE_DATA、PING等既有专项测试保持各自规则。详细协议预期见 [README_Protocol.md](README_Protocol.md#sec-13-local-image)。

常量内存目录扫描测试：分别在 `/data/bin_img` 放入1个、150个以及超过150个合法文件后发送 `KEY_EVENT ARG=PB2,PRESS`，确认出现request accepted、BIN scan completed、selected及显示结果日志，顺序稳定并可从末项循环到首项。目录项数量增加不得重新引入名称数组或导致ESP32重启；检查 `image_worker: job done owner=LOCAL_IMAGE`，要求 `min_free>=2048`，不足时先把统一栈增加到10KB并检查大型局部变量。

reservation释放测试：制造LOCAL_IMAGE pending后分别用DAILY、SLIDESHOW和cast替换，确认打印一次pending取消/替换，旧LOCAL_IMAGE不得执行，下一次EPD预约必须成功。提交失败、pending取消、run失败和run成功四条路径均不得出现reservation永久BUSY或重复释放。

目录限制和Factory Reset测试：确认功能从不读取 `/data/cast_img`、`/data/jpg_img` 或子目录。执行Factory Reset后确认local_img_state清除、epd_mode恢复默认，重启不进入本地浏览。

SPI DMA回归测试：启动时应只出现一次 `EPD static DMA TX ready bytes=1024 shared_spi_max=1024`。分别用已支持的800x480、1024x600、1600x1200 7.9/13.3、1360x480及4色/DKE/mofang屏型显示PSRAM数据，确认所有大块发送经过固定1024字节静态DMA缓冲，不再出现 `chunk=3072/4092` 或运行期DMA TX分配；正常场景不得出现 `ESP_ERR_NO_MEM`。注入首个SPI发送失败时，必须出现包含错误码和DMA余量的关键日志，EPD最终结果必须非ESP_OK，不得被误报为成功。由本地图片浏览触发时不得出现display completed，NVS保持PREPARED且游标不前进，下次PB2重试同一文件。

SPI命令错误测试：注入一次 `spiTransmitCommand()` 失败，确认只打印包含command和ret的关键ESP_LOGE，不触发assert、Guru Meditation或自动重启；EPD最终结果必须非ESP_OK，本地图片浏览不得推进游标。将 `USER_EPD_SPI_SAFE_DMA_TX_CHUNK` 临时设为0，或将 `USER_SHARED_SPI_MAX_TRANSFER_SIZE` 设为小于516时，构建必须被编译期检查拒绝；正式值保持1024。

SD共享SPI回归测试：EPD启用的正常启动必须出现`SDSPI shared bus reused host=1 owner=EPD`，不得再出现`spi_bus_initialize ... SPI bus already initialized`；EPD关闭配置仍须由SD成功初始化总线。确认SDSPI mount和掉电测试remount正常，连续执行大文件读、写、删除、轮播读取及本地图片读取；同时让EPD在BUSY安全点释放共享锁给SD。不得出现大于1024字节的SPI事务、`ESP_ERR_NO_MEM`、文件损坏、锁超时或EPD/SD同时选中。SDSPI仍使用ESP-IDF原生512字节数据块和516字节常驻DMA块，不增加项目自定义SD协议。
