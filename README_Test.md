# README_Test.md

本文件集中保存开发阶段的测试方法和命令。测试前先确认设备已联网，并把示例 IP、文件路径改为实际值。接口协议见 [README_Protocol.md](README_Protocol.md)，预期业务行为见 [README_Fun.md](README_Fun.md)，结果码见 [README_Result_Code.md](README_Result_Code.md)。

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

预期：固件大小不能超过 OTA 分区；版本校验和固件校验通过后写 OTA 分区、设置 boot partition、发送成功结果，并在约 1 秒后自动复位。`reboot` 缺失时默认按 `true`；即使请求传入 `reboot=false`，设备也会打印警告并忽略该值，成功 OTA 必须复位。

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

预期：设备保存 APP 最终发送的轮播列表、interval、强制为 false 的 random 和 startIndex，从 `fileNames[startIndex]` 建立第 0 个绝对时间槽，重写 `show_control.txt` 为标准 RTC control，并按 `timestamp` 启动 RTC 轮播。最终列表允许重复且最多 150 项；超过 150 项返回 1514，缺少 startIndex 返回 1515，`startIndex >= 最终 fileNames 数量` 返回 1516，任一文件不存在、不是普通文件或为空返回 1508。以上非法指令均不改动 RTC / 系统时间、轮播配置、control、NVS、显示模式和现有轮播任务。

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

当前实现：

```text
start_slideshow 是正式轮播列表配置接口；会重写 `show_control.txt` 为标准 RTC control，不再写缺少 timestamp/anchor_epoch 的旧 control。
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

预期：`sw=1` 开启，`sw=0` 关闭；配置写入 slideshow control 文件。

当前实现：

```text
set_slideshow 的 sw=1 会更新控制文件、同步写 `epd_mode=1(SLIDESHOW)`，并尝试启动轮播。
若旧轮播正在运行，`set_slideshow sw=1` 写入新的 interval / timestamp 后会按 RTC 计算出的 next_epoch 重新同步；如果 EPD 正在显示，不打断本次显示，显示完成后继续按 RTC 下一个播放点等待，等待期间会预加载下一张。
`ServerNetworkStaSlideshow_GetScheduleTiming()` 可读取当前 RTC 轮播的 now_epoch / next_epoch / remain，snapshot 和关机前 WAKE_TIMER 计算优先使用这组值。
存储未就绪、未保存轮播列表、参数非法、interval 非法、控制文件/NVS 保存失败、轮播 runtime 启动失败、timestamp 非法、timestamp 写 RTC 失败、SNTP 时间差过大已分别返回 1012、1501、1004、1507、1509、1506、1510、1512、1513。
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

预期：设备返回上传成功结果；`show=false` 时只保存，不进入 EPD 显示队列。

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
USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS 默认 60 秒。
```

普通EPD完成自动倒计时默认关闭；开启后只修改RAM计时，不写NVS。每日一图独立请求one-shot，不受该开关控制；DAILY创建的one-shot在模式切离DAILY后恢复原工作时间并取消。

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

关闭请求：

```powershell
$body = @{ func = "daily_download_file"; sw = 0 } | ConvertTo-Json
Invoke-RestMethod -Uri "$esp/dataUP" `
  -Method Post `
  -ContentType "application/json" `
  -Body $body
```

关闭成功后 `epd_mode=0(NORMAL)`，旧 `daily_cfg` 保留，但 daily worker 已取消。
