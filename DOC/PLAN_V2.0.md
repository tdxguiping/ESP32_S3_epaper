# CH583/CH585 与 WiFi 模组 UART 通讯协议 V2.2

## Summary
本协议用于 CH583/CH585 与 WiFi 模组之间的 UART 通讯。CH583/CH585 负责 BLE 低功耗连接、唤醒 WiFi、转发前端数据、接收 WiFi 控制命令，并在 WiFi 需要时把配网结果/IP 原文 notify 给前端。V2.2 沿用 V2.1 的 DEVICE_INFO 必达握手和 wake_reason 字段，并新增 PB1 长按按键唤醒渠道；WIFI_VER 保留为 WiFi 版本上报命令，组合版本信息继续通过 BLE 私有广播字段发布。

## 1. 通讯基础

```text
UART：UART1
波特率：115200
数据位：8
停止位：1
校验位：无
硬件流控：无
CH583 UART1 RX：PA8
CH583 UART1 TX：PA9
WiFi 电源/唤醒控制：CH585 当前方案为 PA6；旧 CH583 方案为 PB8，以实际硬件为准
```

所有协议帧都使用固定帧头和帧尾：

```text
@# ... ^&
```

CH583 只解析 `@#` 和 `^&` 包起来的数据。普通日志如果不在该格式内，CH583 忽略。

## 2. 标准帧格式

```text
@#V1|SEQ=<seq>|CMD=<cmd>|LEN=<len>|PART=<part>|TOTAL=<total>|ARG=<arg>|CRC=<crc>^&
```

字段说明：

```text
V1      协议版本，当前固定 V1
SEQ     帧序号，0~65535，每发一帧递增
CMD     命令类型
LEN     ARG 字段实际字节长度
PART    当前分包序号，从 1 开始
TOTAL   当前消息总包数
ARG     命令参数，无参数时为空
CRC     CRC16 校验值，4 位大写十六进制
```

普通命令固定：

```text
PART=1
TOTAL=1
```

## 3. CRC / LEN / PART 校验

CRC 算法：

```text
CRC16-CCITT-FALSE
多项式：0x1021
初始值：0xFFFF
输入反转：否
输出反转：否
结果异或：0x0000
输出格式：4 位大写十六进制
```

CRC 计算内容不包含帧头、CRC 字段、帧尾。

示例：

```text
@#V1|SEQ=3|CMD=GPIO|LEN=13|PART=1|TOTAL=1|ARG=PA,3,OUT,HIGH|CRC=XXXX^&
```

CRC 计算字符串：

```text
V1|SEQ=3|CMD=GPIO|LEN=13|PART=1|TOTAL=1|ARG=PA,3,OUT,HIGH
```

接收方必须检查：

```text
CRC 正确
LEN == ARG 实际字节长度
PART >= 1
TOTAL >= 1
PART <= TOTAL
普通命令 PART=1,TOTAL=1
```

## 4. ACK 与 ERR

ACK 表示“收到并执行成功”。

```text
@#V1|SEQ=<seq>|CMD=ACK|LEN=<len>|PART=1|TOTAL=1|ARG=<received_seq>|CRC=<crc>^&
```

ERR 表示“收到但执行失败”。

```text
@#V1|SEQ=<seq>|CMD=ERR|LEN=<len>|PART=1|TOTAL=1|ARG=<received_seq>,<reason>|CRC=<crc>^&
```

常见错误原因：

```text
BAD_CRC              CRC 校验失败
BAD_LEN              LEN 与 ARG 实际长度不一致，或数据超过限制
BAD_PART             PART/TOTAL 错误
BAD_FORMAT           帧格式错误
BAD_CMD              不支持的命令
BAD_ARG              参数错误
BAD_PORT             端口错误
BAD_PIN              引脚错误
BAD_MODE             GPIO 模式错误
BAD_LEVEL            GPIO 电平错误
BAD_TIME             WAKE_TIMER / TIME_SET 时间非法或超范围
NFC_BUSY             NFC 当前处于读写/会话忙状态，暂不能修改内容
DENY_GPIO            禁止操作该 GPIO
BLE_NOT_CONNECTED    BLE 当前未连接，无法回传前端
BLE_NOTIFY_DISABLED  前端未开启 notify
BLE_NOTIFY_FAIL      notify 发送失败
DEVICE_INFO_REQUIRED DEVICE_INFO 未 ACK 前，不接受会改变状态或进入业务流程的命令
```

## 5. DEVICE_INFO 必达握手

BLE 连接 CH583/CH585 后，CH583/CH585 拉高 WiFi 电源/唤醒控制脚唤醒 WiFi。

WiFi 唤醒延时结束后，CH583/CH585 先等待一个很短的 DEVICE_INFO 首发延时，再发送 DEVICE_INFO 握手帧，避免 WiFi 刚启动时 RX 协议任务尚未准备完成。不立即进入普通心跳，也不释放 pending BLE_DATA。

DEVICE_INFO 帧格式：

```text
@#V1|SEQ=<seq>|CMD=DEVICE_INFO|LEN=<len>|PART=1|TOTAL=1|ARG=<mac>,<ble_ver_dec>,<screen_type>,<board_info_hex>,<wake_reason>|CRC=<crc>^&
```

示例：

```text
@#V1|SEQ=20|CMD=DEVICE_INFO|LEN=29|PART=1|TOTAL=1|ARG=AABBCCDDEEFF,100,d,40,KEY_PB2|CRC=XXXX^&
@#V1|SEQ=21|CMD=DEVICE_INFO|LEN=29|PART=1|TOTAL=1|ARG=AABBCCDDEEFF,100,d,40,KEY_PB1|CRC=XXXX^&
```

参数说明：

```text
mac             CH583/CH585 自身 BLE MAC，12 位大写 HEX，不带冒号
ble_ver_dec     BLE/CH583/CH585 固件版本，十进制文本，来自固件宏 VER，范围 0..255
screen_type     屏幕类型字符，来自 EPD_GetScreenType()，对应蓝牙名第 2 位
board_info_hex  板卡信息字节，两位大写 HEX，来自 EPD_GetBoardInfo()，对应蓝牙名第 18 位
wake_reason     本轮唤醒 WiFi 的原因，固定 ASCII 枚举
```

`wake_reason` 枚举值：

```text
BOOT         CH583/CH585 开机或复位后主动唤醒 WiFi
USB          USB 插入检测唤醒 WiFi
KEY_PB1      PB1 长按按键唤醒 WiFi
KEY_PB2      PB2 按键唤醒 WiFi
BLE_CONNECT  BLE 连接后唤醒 WiFi
BLE_WRITE    BLE 写入数据时唤醒 WiFi
NFC          NFC 授权后唤醒 WiFi
TIMER        WAKE_TIMER 定时到期唤醒 WiFi
UNKNOWN      未识别或默认唤醒原因
```

`mac` 字节顺序使用广播显示顺序：

```text
Mac[5] Mac[4] Mac[3] Mac[2] Mac[1] Mac[0]
```

WiFi 收到 `DEVICE_INFO` 后必须完成字段解析和合法性检查，解析成功后回复 ACK。V2.2 按 5 个字段解析 `mac,ble_ver_dec,screen_type,board_info_hex,wake_reason`；如需兼容旧 V2.0 固件，可允许 4 字段 DEVICE_INFO，并把缺失的 `wake_reason` 按 `UNKNOWN` 处理。

```text
@#V1|SEQ=<seq>|CMD=ACK|LEN=<len>|PART=1|TOTAL=1|ARG=<device_info_seq>|CRC=<crc>^&
```

CH583/CH585 收到 ACK 且 `ARG` 等于最近一次 `DEVICE_INFO` 的 SEQ 后：

```text
停止发送 DEVICE_INFO
开始普通 PING/PONG 心跳
允许发送 pending BLE_DATA
允许处理后续 WiFi 业务
```

如果未收到匹配 ACK：

```text
CH583/CH585 按 DEVICE_INFO 重试周期重发 DEVICE_INFO，重试周期可短于普通 PING/PONG heartbeat 周期
当前 DEVICE_INFO 重试周期为 1 秒，连续 10 次未收到匹配 ACK 后按超时处理
不发送 PING
不发送 BLE_DATA
不执行 pending BLE_DATA 释放
允许 ACK / ERR / TIME_GET / GPIO_READ / LED_BLINK / LED_BLINK_STOP 这类无害查询或提示命令
限制 BLE_DATA / WIFI_PROVISION / NFC_SET / TIME_SET / WAKE_TIMER / POWER_OFF / LOWPOWER 等会改变状态或进入业务流程的命令，并返回 ERR,DEVICE_INFO_REQUIRED
连续超时后的关电策略沿用现有逻辑
USB 供电场景沿用现有逻辑继续等待
```

WiFi 必须等 DEVICE_INFO ACK 流程完成后，再执行后续业务。

### 5.1 屏幕类型编码

屏幕类型是 1 个可见 ASCII 字符，由 `EPD_GetScreenType()` 返回，同时放入 BLE 名第 2 位和 `DEVICE_INFO.screen_type` 字段。

当前已确定的屏幕类型：

```text
d = 13.3 寸 HD 6 色屏
e = 7.09 寸 HD 6 色屏
```

说明：

```text
屏幕类型只描述屏幕规格/类型，不描述板卡厂家
前端需要结合 screen_type 和 board_info_hex 才能准确选择图片处理算法
后续新增屏幕类型时，继续按 EPD_GetScreenType() 的字符表维护
```

### 5.2 板卡信息编码

板卡信息是 1 个 byte，同时要求落在可见 ASCII 范围，便于放入 BLE 广播名和串口日志。

位定义：

```text
bit7-bit5 固定为 010，保证落在可见 ASCII 区间
bit4      保留旧分组兼容位
bit3-bit0 表示厂家 ID，从 0 开始递增
```

第一组厂家 ID：

```text
0x40 / '@' = 第一组，厂家 ID 0，XT 兴泰
0x41 / 'A' = 第一组，厂家 ID 1，DKE
0x42 / 'B' = 第一组，厂家 ID 2，预留
0x43 / 'C' = 第一组，厂家 ID 3，预留
```

第二组厂家 ID：

```text
0x50 / 'P' = 第二组，厂家 ID 0，预留
0x51 / 'Q' = 第二组，厂家 ID 1，预留
0x52 / 'R' = 第二组，厂家 ID 2，预留
0x53 / 'S' = 第二组，厂家 ID 3，预留
```

说明：

```text
已确定的厂家 ID 直接按上表绑定，未确定的厂家 ID 保留为预留编号
厂家 ID 表后续确定后，只更新对应 ID 的厂家名称和驱动返回值
EPD_GetBoardInfo() 放在各屏驱动文件中维护，参考 EPD_GetScreenType()
如果一个驱动文件共用多个厂家版本，优先用现有屏幕/厂家编译宏在 EPD_GetBoardInfo() 内区分
只有现有宏无法区分真实厂家时，才新增明确厂家宏；不因为板卡信息拆分共用驱动
```

## 6. 普通心跳

DEVICE_INFO 握手成功后，CH583/CH585 才开始普通 PING/PONG 心跳。CH583/CH585 每 10 秒发送一次：

```text
@#V1|SEQ=<seq>|CMD=PING|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
```

WiFi 回复：

```text
@#V1|SEQ=<seq>|CMD=PONG|LEN=<len>|PART=1|TOTAL=1|ARG=<ping_seq>|CRC=<crc>^&
```

合法 PONG 必须满足：

```text
CRC 正确
LEN 正确
PART=1
TOTAL=1
ARG 等于对应 PING 的 SEQ
```

当前普通心跳周期为 10 秒，连续 6 次未收到匹配 PONG 后按超时处理。

连续超时后 CH583 会拉低 WiFi 电源/唤醒控制脚，关闭 WiFi，并进入低功耗。

## 7. 前端到 WiFi 透传

前端通过 BLE 写给 CH583 的数据，CH583 不解析业务含义，只封装成 UART 协议帧发给 WiFi。

```text
CMD=BLE_DATA
```

单包：

```text
@#V1|SEQ=<seq>|CMD=BLE_DATA|LEN=<len>|PART=1|TOTAL=1|ARG=<frontend_data>|CRC=<crc>^&
```

如果前端一次 BLE write 超过 300 字节，CH583 按 300 字节自动分包：

```text
@#V1|SEQ=300|CMD=BLE_DATA|LEN=300|PART=1|TOTAL=3|ARG=<前300字节>|CRC=XXXX^&
@#V1|SEQ=301|CMD=BLE_DATA|LEN=300|PART=2|TOTAL=3|ARG=<中间300字节>|CRC=XXXX^&
@#V1|SEQ=302|CMD=BLE_DATA|LEN=150|PART=3|TOTAL=3|ARG=<最后150字节>|CRC=XXXX^&
```

WiFi 端按 `PART/TOTAL` 顺序处理。

## 8. WiFi 到前端透传

WiFi 可以通过 CH583 把配网结果、IP 地址等消息回传给前端。

```text
CMD=WIFI_DATA
```

格式：

```text
@#V1|SEQ=<seq>|CMD=WIFI_DATA|LEN=<len>|PART=1|TOTAL=1|ARG=<message>|CRC=<crc>^&
```

限制：

```text
LEN <= 256
PART=1
TOTAL=1
不支持分包
```

示例：

```text
@#V1|SEQ=220|CMD=WIFI_DATA|LEN=57|PART=1|TOTAL=1|ARG={"result":0,"message":"Find wifi","stage":"MERCURY_A662"}|CRC=XXXX^&
```

CH583 行为：

```text
校验通过后，只把 ARG 原文 notify 给前端
不会把 @#、SEQ、CMD、LEN、PART、TOTAL、CRC 转给前端
```

前端收到的是：

```json
{"result":0,"message":"Find wifi","stage":"MERCURY_A662"}
```

当前 notify 策略：

```text
只发送一次 notify
前端不需要回复 ACK
前端收到 IP/成功消息后可主动断开 BLE
```

CH583 回复 WiFi：

```text
notify 成功：ACK
BLE 未连接：ERR,BLE_NOT_CONNECTED
notify 未开启：ERR,BLE_NOTIFY_DISABLED
notify 失败：ERR,BLE_NOTIFY_FAIL
LEN > 256：ERR,BAD_LEN
PART/TOTAL 不是 1/1：ERR,BAD_PART
```

## 9. WiFi 配网状态上报与广播复合状态位

该命令用于 WiFi 在启动或配网成功后，向 CH583/CH585 上报当前配网状态。CH583/CH585 收到合法状态后先进入 pending 队列并立即 ACK，随后异步保存到 DataFlash，并刷新 TDX 蓝牙广播名第 21 位中的复合状态。BOE 广播名不受影响。

```text
CMD=WIFI_PROVISION
```

参数：

```text
ARG=<status_hex>
status_hex 为 2 位十六进制文本，表示 1 个复合状态 byte
第 1 位十六进制字符 = 高 4bit，表示 WiFi 配网状态；4 表示未配网，5 表示已配网
第 2 位十六进制字符 = 低 4bit，表示相框工作模式
```

格式：

```text
@#V1|SEQ=<seq>|CMD=WIFI_PROVISION|LEN=2|PART=1|TOTAL=1|ARG=<status_hex>|CRC=<crc>^&
```

示例：

```text
@#V1|SEQ=230|CMD=WIFI_PROVISION|LEN=2|PART=1|TOTAL=1|ARG=50|CRC=XXXX^&
@#V1|SEQ=231|CMD=WIFI_PROVISION|LEN=2|PART=1|TOTAL=1|ARG=51|CRC=XXXX^&
@#V1|SEQ=232|CMD=WIFI_PROVISION|LEN=2|PART=1|TOTAL=1|ARG=52|CRC=XXXX^&
```

CH583/CH585 行为：

```text
校验 CRC/LEN/PART/TOTAL
只接受 2 位十六进制 ARG
高 4bit 只接受 4 或 5
低 4bit 当前只接受 0、1、2
合法配网状态进入 pending 队列
合法工作模式进入 pending 队列
立即回复 ACK，避免阻塞后续 UART 收帧
约 200ms 后异步保存配网状态和工作模式到 DataFlash
约 200ms 后异步刷新 TDX scan response 中的 scanRspData[22]
WIFI_PROVISION 会同时更新复合状态 byte 的高 4bit 和低 4bit
未保存过、读到 0xFF 或非法保存值时，配网状态默认 0，工作模式默认 0
```

CH583/CH585 回复 WiFi：

```text
参数合法并进入 pending 队列：ACK
ARG 不是 2 位十六进制，或 bit 值超出当前定义范围：ERR,BAD_ARG
LEN 不是 2：ERR,BAD_LEN
PART/TOTAL 不是 1/1：ERR,BAD_PART
CRC 错误：ERR,BAD_CRC
```

广播名第 21 位复合状态规则：

```text
只修改 TDX 广播名
BOE 广播名不修改
第 21 位对应代码中的 scanRspData[22]
scanRspData[22] 是原始 byte，同时保证落在可见 ASCII 字符范围内
scanRspData[22] = (provision_name_nibble << 4) | frame_work_mode
```

高 4bit 表示 WiFi 配网状态：

```text
0100 = 未配网，可见 ASCII 范围 0x40~0x4F
0101 = 已配网，可见 ASCII 范围 0x50~0x5F
```

低 4bit 表示相框工作模式：

```text
0000 = 普通模式
0001 = 轮播模式
0010 = 每日更新模式
0011..1111 = 保留
```

示例：

```text
0x40 = 未配网 + 普通模式，ASCII '@'
0x50 = 已配网 + 普通模式，ASCII 'P'
0x51 = 已配网 + 轮播模式，ASCII 'Q'
0x52 = 已配网 + 每日更新模式，ASCII 'R'
0x42 = 未配网 + 每日更新模式，ASCII 'B'
```

扩展示例：

```text
40~4F = 未配网 + 16 种工作模式，均为可见 ASCII
50~5F = 已配网 + 16 种工作模式，均为可见 ASCII

0x53 = 已配网 + 第 4 种工作模式，ASCII 'S'
0x54 = 已配网 + 第 5 种工作模式，ASCII 'T'
0x5F = 已配网 + 第 16 种工作模式，ASCII '_'
0x43 = 未配网 + 第 4 种工作模式，ASCII 'C'
0x4F = 未配网 + 第 16 种工作模式，ASCII 'O'
```

说明：低 4bit 取值范围为 `0x0~0xF`，因此工作模式最多 16 种；`0x5F` 表示“已配网 + 第 16 种工作模式”，不是第 18 种工作模式。高 4bit 固定使用 `4/5`，是为了让该字节落在可见 ASCII 字符范围内。

刷新与恢复规则：

```text
每次收到合法 WIFI_PROVISION 后立即 ACK，并在约 200ms 后异步刷新 scanRspData[22]
工作模式变化后立即刷新 scanRspData[22]
复位后恢复 DataFlash 中最后一次保存的配网状态和工作模式
未保存过、读到 0xFF 或非法保存值时，配网默认 0，工作模式默认 0
scanRspData[20] 保持原有逻辑，不因该复合状态规则改变
```

## 10. 版本交换与私有广播版本字段

该功能用于 CH583/CH585 与 WiFi 模组互相同步固件版本。BLE 固件版本由 DEVICE_INFO 第一时间携带上报，不再使用独立命令；WiFi 固件版本仍由 WiFi 后续通过 WIFI_VER 上报。

版本组成：

```text
组合版本共 3 字节
byte0：BLE/CH583/CH585 版本，取当前固件 VER，范围 0..255
byte1：WiFi 版本高字节
byte2：WiFi 版本低字节
WiFi 版本范围 0..65535
```

### 10.1 BLE 版本上报规则

BLE 版本上报规则：

```text
BLE 版本由 DEVICE_INFO 第一时间携带上报
BLE 版本字段为 DEVICE_INFO 的 ble_ver_dec
BLE 版本字段来自本机固件宏 VER
BLE 版本字段范围为 0..255
BLE 版本不再使用独立上报命令
WiFi 收到 DEVICE_INFO 并 ACK 后，即可认为 BLE MAC、BLE 版本、屏幕类型、板卡信息、唤醒原因都已可靠接收
```

DEVICE_INFO 与后续透传关系：

```text
前端 BLE 写入发生在 DEVICE_INFO ACK 前：CH583/CH585 继续缓存到 pending BLE_DATA
DEVICE_INFO ACK 后：pending BLE_DATA 才允许发送给 WiFi
DEVICE_INFO ACK 后：普通 PING/PONG 心跳才允许启动
CH583/CH585 不因为 WIFI_VER 未返回而继续积压前端数据
WIFI_VER 晚到也可以正常处理和刷新广播
保留现有 pending 溢出保护，版本交换不能扩大 BLE_DATA 溢出风险
```

### 10.2 WiFi 上报 WiFi 版本

WiFi 可以在 DEVICE_INFO ACK 完成后，或后续任意合适时机，上报自己的版本：

```text
CMD=WIFI_VER
```

格式：

```text
@#V1|SEQ=<seq>|CMD=WIFI_VER|LEN=<len>|PART=1|TOTAL=1|ARG=<wifi_ver_dec>|CRC=<crc>^&
```

参数：

```text
ARG=<wifi_ver_dec>
wifi_ver_dec 为十进制文本
范围 0..65535
```

示例：

```text
@#V1|SEQ=240|CMD=WIFI_VER|LEN=1|PART=1|TOTAL=1|ARG=0|CRC=XXXX^&
@#V1|SEQ=241|CMD=WIFI_VER|LEN=5|PART=1|TOTAL=1|ARG=65535|CRC=XXXX^&
```

CH583/CH585 行为：

```text
校验 CRC/LEN/PART/TOTAL
只接受单包 PART=1,TOTAL=1
只接受十进制 WiFi 版本
合法范围为 0..65535
收到合法 WIFI_VER 后立即回复 ACK
组合版本为 {VER, WIFI_VER_H, WIFI_VER_L}
立即刷新 BLE 私有广播版本字段
设置版本 dirty 标记
不在收到 WIFI_VER 时立即写 DataFlash
等待 POWER_OFF / LOWPOWER / WiFi 会话结束收尾时统一写 DataFlash
复位后读取 DataFlash 中保存的 WiFi 版本高低字节，但 BLE 字节始终使用当前固件 VER
```

CH583/CH585 回复 WiFi：

```text
参数合法并已刷新 RAM/广播：ACK
ARG 为空、不是十进制、或超过 65535：ERR,BAD_ARG
PART/TOTAL 不是 1/1：ERR,BAD_PART
CRC 错误：ERR,BAD_CRC
LEN 错误：ERR,BAD_LEN
```

广播字段规则：

```text
只修改 TDX BLE advertising data 中的私有版本字段
不再把该 3 字节组合版本放入可见广播名
字段类型使用 Manufacturer Specific Data
Manufacturer Specific Data 前 2 字节为 Company ID，当前临时使用 0xFFFF，低字节在前
Company ID 后面紧跟 3 字节版本内容
byte0 = 当前 BLE/CH583/CH585 VER
byte1 = WiFi 版本高字节
byte2 = WiFi 版本低字节
```

边界示例：

```text
Raw AD 示例：06 FF FF FF 64 00 00 表示 Company ID=0xFFFF，BLE版本=100，WIFI_VER=0
Raw AD 示例：06 FF FF FF 64 FF FF 表示 Company ID=0xFFFF，BLE版本=100，WIFI_VER=65535
WIFI_VER=65536          => ERR,BAD_ARG，不更新广播，不写 DataFlash
```

推荐 WiFi 启动顺序：

```text
1. WiFi 启动并准备接收 UART 协议
2. CH583/CH585 发送 DEVICE_INFO
3. WiFi ACK DEVICE_INFO
4. CH583/CH585 开始发送 pending BLE_DATA 或 PING
5. WiFi 在方便时发送 WIFI_VER
6. WiFi 业务完成后发送 POWER_OFF 或 LOWPOWER
```

## 11. GPIO 控制

```text
@#V1|SEQ=<seq>|CMD=GPIO|LEN=<len>|PART=1|TOTAL=1|ARG=<port>,<pin>,<mode>,<level>|CRC=<crc>^&
```

字段：

```text
port   PA 或 PB
pin    0~31
mode   OUT / IN_PU / IN_PD / IN_FLOAT
level  HIGH / LOW / KEEP
```

规则：

```text
mode=OUT 时，level 必须是 HIGH 或 LOW
mode=IN_PU / IN_PD / IN_FLOAT 时，level 必须是 KEEP
```

示例：

```text
@#V1|SEQ=100|CMD=GPIO|LEN=13|PART=1|TOTAL=1|ARG=PA,3,OUT,HIGH|CRC=XXXX^&
```

## 12. GPIO 读取

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=GPIO_READ|LEN=<len>|PART=1|TOTAL=1|ARG=<port>,<pin>|CRC=<crc>^&
```

CH583 回复：

```text
@#V1|SEQ=<seq>|CMD=GPIO_VALUE|LEN=<len>|PART=1|TOTAL=1|ARG=<read_seq>,<port>,<pin>,<level>|CRC=<crc>^&
```

示例：

```text
@#V1|SEQ=31|CMD=GPIO_VALUE|LEN=13|PART=1|TOTAL=1|ARG=130,PA,3,HIGH|CRC=XXXX^&
```

## 13. LED 闪烁控制

该命令用于让 CH583 本地控制红绿 LED 闪烁，避免 WiFi 为了闪烁而高频发送 GPIO 指令。

硬件定义：

```text
RED    PB5，低电平点亮，高电平关闭
GREEN  PB6，低电平点亮，高电平关闭
```

### 13.1 开始闪烁

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=LED_BLINK|LEN=<len>|PART=1|TOTAL=1|ARG=<led>,<interval_ms>|CRC=<crc>^&
```

字段：

```text
led          RED / GREEN
interval_ms  1~10000，单位 ms
```

规则：

```text
RED 和 GREEN 独立控制，不能使用 BOTH
LED_BLINK 收到后，CH583 先点亮目标 LED，再按 interval_ms 间隔翻转电平
同一个 LED 再次收到 LED_BLINK 时，更新该 LED 的闪烁间隔，并重新从点亮状态开始
红灯和绿灯使用独立定时事件，两个 LED 可以使用不同闪烁间隔
interval_ms 表示翻转间隔，不是完整亮灭周期；例如 1000 表示每 1000ms 翻转一次
```

示例：

```text
@#V1|SEQ=120|CMD=LED_BLINK|LEN=8|PART=1|TOTAL=1|ARG=RED,1000|CRC=XXXX^&
@#V1|SEQ=121|CMD=LED_BLINK|LEN=9|PART=1|TOTAL=1|ARG=GREEN,200|CRC=XXXX^&
```

CH583 回复：

```text
参数合法并启动成功：ACK
未知 LED、BOTH、interval_ms 小于 1 或大于 10000、参数数量错误：ERR,BAD_ARG
```

### 13.2 停止闪烁并关闭 LED

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=LED_BLINK_STOP|LEN=<len>|PART=1|TOTAL=1|ARG=<led>|CRC=<crc>^&
```

字段：

```text
led  RED / GREEN
```

规则：

```text
LED_BLINK_STOP 只停止指定 LED，不影响另一个 LED
停止后 CH583 将该 LED 关闭，即 PB5/PB6 输出高电平
```

示例：

```text
@#V1|SEQ=122|CMD=LED_BLINK_STOP|LEN=3|PART=1|TOTAL=1|ARG=RED|CRC=XXXX^&
@#V1|SEQ=123|CMD=LED_BLINK_STOP|LEN=5|PART=1|TOTAL=1|ARG=GREEN|CRC=XXXX^&
```

CH583 回复：

```text
参数合法并停止成功：ACK
未知 LED、BOTH、参数数量错误：ERR,BAD_ARG
```

### 13.3 与 GPIO / 低功耗的关系

```text
如果普通 GPIO 命令操作 PB5 或 PB6，CH583 会先停止对应 LED 的闪烁，再执行 GPIO 命令
进入低功耗或 WiFi 主动关电流程时，CH583 会停止红绿两个 LED 闪烁，并关闭两个 LED
GPIO_READ 只读取当前引脚电平，不改变 LED 闪烁状态
```

## 14. 禁止操作的 GPIO

禁止列表：

```text
PA6   WiFi 电源/唤醒控制
PA8   UART1 RX
PA9   UART1 TX
PB4   UART0 RX 调试
PB7   UART0 TX 调试
PB13  充电检测/CHARGE_LED
```

如果 WiFi 操作禁止 GPIO，CH583 不执行，并回复：

```text
ERR,<received_seq>,DENY_GPIO
```

## 15. WiFi 网络时间同步与备份时间

该功能用于 WiFi 在有网络时间时，把北京时间同步给 CH583/CH585。CH583/CH585 不直接重设硬件 RTC，而是保存“WiFi 网络时间”和“当前 RTC 时间”的映射关系，用于后续 `TIME_GET` 查询和定时唤醒到期判断。

内部时间模型：

```text
base_wall_time = WiFi 下发的北京时间
base_rtc_time  = 收到 TIME_SET 时 CH583/CH585 当前 RTC 时间
backup_time    = base_wall_time + (rtc_now - base_rtc_time)
```

说明：

```text
WiFi 下发时间按北京时间解释，不是 UTC
CH583/CH585 不调用 RTC_InitTime 校准硬件 RTC
TIME_SET 只更新 RAM 并立即 ACK，不立即写 DataFlash
DataFlash 保存发生在 WiFi POWER_OFF / LOWPOWER 收尾阶段
复位后如果只能读取到 DataFlash 中最后保存的时间，TIME_GET 返回 STALE
从未成功 TIME_SET 且没有可用保存值时，TIME_GET 返回 INVALID
```

### 15.1 设置网络时间

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=TIME_SET|LEN=19|PART=1|TOTAL=1|ARG=YYYY-MM-DD,HH:MM:SS|CRC=<crc>^&
```

参数：

```text
YYYY-MM-DD,HH:MM:SS  固定 19 字节，北京时间
年份范围：2020..2064
月、日、时、分、秒必须是合法日期时间
```

示例：

```text
@#V1|SEQ=240|CMD=TIME_SET|LEN=19|PART=1|TOTAL=1|ARG=2026-07-07,12:00:00|CRC=XXXX^&
```

CH583/CH585 行为：

```text
校验 CRC/LEN/PART/TOTAL
校验日期时间是否合法
读取当前 RTC 时间
更新 RAM 中的时间锚点
标记 time_dirty
立即回复 ACK
不在 TIME_SET 中写 DataFlash，避免阻塞 UART 后续收帧
```

CH583/CH585 回复 WiFi：

```text
参数合法并更新 RAM 成功：ACK
LEN 不是 19：ERR,BAD_LEN
日期格式错误或日期非法：ERR,BAD_TIME
PART/TOTAL 不是 1/1：ERR,BAD_PART
CRC 错误：ERR,BAD_CRC
```

非法示例：

```text
2026-02-30,12:00:00  ERR,<received_seq>,BAD_TIME
2026-13-01,00:00:00  ERR,<received_seq>,BAD_TIME
2026-07-07,24:00:00  ERR,<received_seq>,BAD_TIME
```

### 15.2 查询备份时间

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=TIME_GET|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
```

CH583/CH585 回复：

```text
@#V1|SEQ=<seq>|CMD=TIME_STATUS|LEN=<len>|PART=1|TOTAL=1|ARG=<status>|CRC=<crc>^&
```

`ARG=<status>` 格式：

```text
VALID,YYYY-MM-DD,HH:MM:SS
STALE,YYYY-MM-DD,HH:MM:SS
INVALID
```

含义：

```text
VALID   本轮 RTC 未复位，时间由 RTC 差值实时推算，可信
STALE   复位后只能返回 DataFlash 中最后保存的备份时间，不保证继续走过离线时长
INVALID 从未成功 TIME_SET，且没有可用备份时间
```

示例：

```text
@#V1|SEQ=241|CMD=TIME_GET|LEN=0|PART=1|TOTAL=1|ARG=|CRC=XXXX^&
@#V1|SEQ=18|CMD=TIME_STATUS|LEN=25|PART=1|TOTAL=1|ARG=VALID,2026-07-07,12:00:05|CRC=XXXX^&
@#V1|SEQ=19|CMD=TIME_STATUS|LEN=25|PART=1|TOTAL=1|ARG=STALE,2026-07-07,12:00:05|CRC=XXXX^&
@#V1|SEQ=20|CMD=TIME_STATUS|LEN=7|PART=1|TOTAL=1|ARG=INVALID|CRC=XXXX^&
```

### 15.3 DataFlash 保存策略

```text
TIME_SET 收到后只更新 RAM，不立即写 DataFlash
WiFi 发送 POWER_OFF / LOWPOWER 并收到 ACK 后，CH583/CH585 在关闭 WiFi 前保存时间锚点
如果 time_dirty=Yes，保存最新 TIME_SET 锚点
如果 time_valid=Yes，同时保存当前推算出的 last_saved_time
4 小时分段检查过程中不写 DataFlash
真正定时到期、即将唤醒 WiFi 前，会额外保存一次当前推算时间快照
```

推荐 WiFi 使用流程：

```text
1. WiFi 有网络时间时发送 TIME_SET
2. 等 CH583/CH585 回复 ACK
3. WiFi 后续可以发送 TIME_GET 校验备份时间
4. WiFi 完成本轮业务后发送 WAKE_TIMER ON,<seconds> 或 WAKE_TIMER OFF,0
5. WiFi 发送 POWER_OFF 或 LOWPOWER
6. CH583/CH585 在关闭 WiFi 前统一保存时间和运行状态
```

## 16. WiFi 定时唤醒配置

该命令用于让 WiFi 在进入低功耗前，告诉 CH583/CH585 下一次需要唤醒 WiFi 的时间。

CH583/CH585 收到合法配置后先更新本轮 WiFi 会话的 RAM 状态并立即 ACK，不在 `WAKE_TIMER` 中立即写 DataFlash。WiFi 后续发送 `POWER_OFF` / `LOWPOWER` 后，CH583/CH585 根据本轮是否收到合法 `WAKE_TIMER` 决定是否启动定时器。定时时间到后，CH583/CH585 拉高 WiFi 唤醒脚唤醒 WiFi。

当前 CH585 方案使用 `PA6` 唤醒 WiFi；旧 CH583 方案如仍使用 `PB8`，以硬件版本为准。

### 16.1 开启定时唤醒

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=WAKE_TIMER|LEN=<len>|PART=1|TOTAL=1|ARG=ON,<seconds>|CRC=<crc>^&
```

参数：

```text
seconds 单位：秒
允许范围：0..604800
```

示例：

```text
@#V1|SEQ=10|CMD=WAKE_TIMER|LEN=7|PART=1|TOTAL=1|ARG=ON,3600|CRC=XXXX^&
```

含义：WiFi 进入低功耗后，CH583/CH585 在 3600 秒后唤醒 WiFi。

特殊值：

```text
seconds=0 表示 WiFi 进入低功耗流程后立即触发一次定时唤醒
```

CH583/CH585 校验并更新本轮 RAM 状态成功后回复：

```text
ACK,<received_seq>
```

### 16.2 关闭定时唤醒

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=WAKE_TIMER|LEN=5|PART=1|TOTAL=1|ARG=OFF,0|CRC=<crc>^&
```

CH583/CH585 校验并更新本轮 RAM 状态成功后回复：

```text
ACK,<received_seq>
```

### 16.3 错误返回

```text
ON,-1           ERR,<received_seq>,BAD_TIME 或 BAD_ARG
ON,604801       ERR,<received_seq>,BAD_TIME
ON,abc          ERR,<received_seq>,BAD_TIME
OFF,1           ERR,<received_seq>,BAD_TIME
参数格式错误     ERR,<received_seq>,BAD_ARG
PART/TOTAL 错误 ERR,<received_seq>,BAD_PART
LEN 错误         ERR,<received_seq>,BAD_LEN
```

### 16.4 分段定时规则

为避免长时间定时直接换算成 TMOS tick 后溢出，CH583/CH585 内部会把长定时拆成多段。

```text
每段最长：14400 秒，即 4 小时
3 小时  -> 1 段：10800
6 小时  -> 2 段：14400 + 7200
10 小时 -> 3 段：14400 + 14400 + 7200
60 小时 -> 15 段：14400 * 15
7 天    -> 42 段：14400 * 42
```

如果本轮已经成功 `TIME_SET`，CH583/CH585 使用 RTC 备份时间计算绝对到期点：

```text
wake_due_time = current_backup_time + seconds
```

每段事件触发时：

```text
如果 current_backup_time >= wake_due_time，保存当前推算时间快照并唤醒 WiFi
否则继续挂下一段
```

如果本轮没有有效 `TIME_SET` / 备份时间，CH583/CH585 使用相对剩余秒数倒计时：

```text
relative_remaining_seconds = seconds
每段到期后扣减本段秒数
扣到 0 后唤醒 WiFi
```

说明：

```text
没有网络时间时，长定时仍然可用，不会出现 60 小时 tick 溢出提前唤醒
没有网络时间时，如果 CH583/CH585 中途复位或断电，相对倒计时无法可靠恢复
4 小时分段事件只用于内部检查，不会拉高 WiFi 唤醒脚，也不会写 DataFlash
```

### 16.5 WiFi 推荐低功耗流程

需要定时唤醒时：

```text
1. WiFi 发送 WAKE_TIMER ON,<seconds>
2. 等 CH583/CH585 回复 ACK
3. WiFi 发送 POWER_OFF 或 LOWPOWER
4. 等 CH583/CH585 回复 ACK
5. WiFi 进入低功耗
```

不需要定时唤醒时：

```text
1. WiFi 发送 WAKE_TIMER OFF,0
2. 等 CH583/CH585 回复 ACK
3. WiFi 发送 POWER_OFF 或 LOWPOWER
4. 等 CH583/CH585 回复 ACK
5. WiFi 进入低功耗
```

重要规则：

```text
WiFi 每次被唤醒后，都需要重新发送新的 WAKE_TIMER。
如果本轮 WiFi 会话没有成功发送合法 WAKE_TIMER，就直接 POWER_OFF/LOWPOWER，
CH583/CH585 会关闭定时唤醒，避免沿用旧时间。
WiFi 只有收到 ACK，才能认为 WAKE_TIMER 配置成功。
```

建议重发策略：

```text
WAKE_TIMER 发出后未收到 ACK，最多重发 5 次。
仍无 ACK 时，WiFi 可以直接发送 POWER_OFF/LOWPOWER。
```

## 17. NFC 内容管理

该功能用于 WiFi 在被唤醒后，把需要给手机 NFC 读取的设备信息写入 CH585。CH585 保存该内容，并在手机靠近触发 NFC-only 会话时，以普通 NDEF Text JSON 形式提供给手机读取。

重要原则：

```text
真实 NFC 展示数据只允许 WiFi 通过 UART 协议修改
手机端普通 NDEF 写入只作为授权唤醒命令，不作为真实展示数据保存
NFC 展示数据会保存到 CH585 DataFlash，软复位/断电重启后仍可恢复
手机读取 NFC 时读取的是 CH585 RAM 中模拟的 Type2 Tag/NDEF 缓存，不是每次直接读 Flash
```

### 17.1 写入 NFC 展示 JSON

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=NFC_SET|LEN=<len>|PART=1|TOTAL=1|ARG=<base64url_json>|CRC=<crc>^&
```

参数：

```text
ARG 为 UTF-8 JSON 的 base64url 编码结果
base64url 使用 URL 安全字符：A-Z a-z 0-9 - _
可省略尾部 = padding
JSON 解码后最大长度：220 字节
```

CH585 行为：

```text
校验 UART 帧 CRC/LEN/PART
base64url 解码 ARG
检查 JSON 长度 <= 220
更新 RAM 中的 NFC NDEF Text JSON
保存 JSON 到 DataFlash
回复 ACK
```

示例 JSON：

```json
{"mac":"D00C5E140647","wifi":"sleep","nfc":"ready","msg":"hello"}
```

对应 WiFi 发送时，ARG 必须放这个 JSON 的 base64url 编码，不直接放原始 JSON。

成功回复：

```text
ACK,<received_seq>
```

失败回复：

```text
base64url 解码失败：ERR,<received_seq>,BAD_ARG
JSON 长度为 0 或超过 220 字节：ERR,<received_seq>,BAD_LEN
NFC 正在被手机读写或处于忙状态：ERR,<received_seq>,NFC_BUSY
PART/TOTAL 错误：ERR,<received_seq>,BAD_PART
LEN 错误：ERR,<received_seq>,BAD_LEN
CRC 错误：ERR,<received_seq>,BAD_CRC
```

### 17.2 清空 NFC 展示内容

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=NFC_CLEAR|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
```

CH585 行为：

```text
清除 DataFlash 中保存的 NFC 展示 JSON
恢复默认 NFC 展示 JSON
更新 RAM 中的 NDEF 缓存
回复 ACK
```

默认 JSON 由 CH585 生成，至少包含设备 MAC、WiFi 状态和 NFC 状态，例如：

```json
{"mac":"4706145E0CD0","wifi":"sleep","nfc":"ready"}
```

### 17.3 查询 NFC 状态

WiFi 发送：

```text
@#V1|SEQ=<seq>|CMD=NFC_STATUS|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
```

CH585 回复：

```text
@#V1|SEQ=<seq>|CMD=NFC_STATUS|LEN=<len>|PART=1|TOTAL=1|ARG=<state>,<payload_len>,<last_auth_result>|CRC=<crc>^&
```

字段：

```text
state             IDLE / READY，当前 NFC/PICC 状态
payload_len       当前 NFC 展示 JSON 的 UTF-8 字节长度
last_auth_result  NONE / OK / BAD_FORMAT / BAD_TOKEN
```

示例：

```text
@#V1|SEQ=88|CMD=NFC_STATUS|LEN=12|PART=1|TOTAL=1|ARG=IDLE,51,NONE|CRC=XXXX^&
```

### 17.4 与手机 NFC 授权写入的关系

手机端授权唤醒 WiFi 使用普通 NDEF 写入，推荐写入 NDEF Text JSON：

```json
{"cmd":"NWK1","token":"<TOKEN16>"}
```

`TOKEN16` 是按设备 MAC 派生的固定授权口令，算法如下：

```text
master_secret = 4E 46 43 2D 54 44 58 31 2D 43 48 35 38 35 21 01
plain[0..5]   = CH585 内部 MAC，使用固件日志中的 Mac[0]..Mac[5] 顺序
plain[6..12]  = ASCII "NFCWAKE"
plain[13]     = 0x01
plain[14..15] = 0x00 0x00
cipher        = AES-128-ECB-Encrypt(master_secret, plain[16])
TOKEN16       = cipher[0..7] 转 16 字节大写 HEX 字符串
```

说明：

```text
TOKEN16 长度固定为 16 个大写 HEX 字符
前端写入时不带 0x，不带空格，不带冒号
BLE 广播显示 MAC 可能是反序，前端计算 token 时必须使用 CH585/后端约定的内部 MAC 顺序
建议后端或设备管理平台下发 token，前端不要硬编码 master_secret
```

示例：

```text
BLE 显示 MAC：D00C5E140647
CH585 内部 MAC 顺序：47 06 14 5E 0C D0
plain = 47 06 14 5E 0C D0 4E 46 43 57 41 4B 45 01 00 00
TOKEN16 = A41E04DC2D96E229
```

对应手机 NDEF Text JSON：

```json
{"cmd":"NWK1","token":"A41E04DC2D96E229"}
```

CH585 处理规则：

```text
手机写入的授权 NDEF 只作为一次性命令
CH585 校验 token 后立即从 RAM 备份恢复原 NFC 展示 JSON
手机写入内容不保存到 DataFlash
授权成功后 CH585 退出 NFC-only，会回到 BLE 模式后再唤醒 WiFi
错误 token 不唤醒 WiFi，并记录 BAD_TOKEN
```

因此，WiFi 通过 NFC_SET 写入的真实展示数据不会被手机授权写入长期覆盖。

### 17.5 推荐 WiFi 使用流程

WiFi 被唤醒后，如果需要更新 NFC 展示内容：

```text
1. WiFi 生成要给手机读取的 JSON
2. 将 JSON 做 UTF-8 编码
3. 将 UTF-8 JSON 做 base64url 编码
4. 发送 NFC_SET
5. 等 CH585 回复 ACK
6. 后续再执行其他业务或发送 POWER_OFF/LOWPOWER
```

如果 NFC_SET 返回 NFC_BUSY：

```text
等待 1 秒后重试
最多重试 3 次
仍失败时，本轮可以跳过 NFC 内容更新，避免影响 WiFi 低功耗流程
```

## 18. WiFi 主动关电 / 低功耗

WiFi 任务完成后，如果允许 CH583/CH585 关闭 WiFi 电源，发送 `POWER_OFF` 或 `LOWPOWER`：

```text
@#V1|SEQ=<seq>|CMD=POWER_OFF|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
@#V1|SEQ=<seq>|CMD=LOWPOWER|LEN=0|PART=1|TOTAL=1|ARG=|CRC=<crc>^&
```

CH583/CH585 收到并校验通过后：

```text
回复 ACK
保存本轮需要落盘的 WiFi 配网状态、工作模式、备份时间、WiFi 版本
根据本轮 WAKE_TIMER 状态启动或关闭定时唤醒
拉低 WiFi 电源/唤醒控制脚
关闭 WiFi 电源
CH583/CH585 进入低功耗
```

## 19. PB1/PB2 按键事件上报

该命令用于 CH583/CH585 在 WiFi 已经处于唤醒会话中时，主动通知 WiFi：PB1 或 PB2 按键发生了一次有效按下。PB1 固件侧由长按触发，但协议事件名仍使用 `PB1,PRESS`，恢复出厂语义由 WiFi 端根据 PB1 渠道处理。

```text
CMD=KEY_EVENT
```

CH583/CH585 发送：

```text
@#V1|SEQ=<seq>|CMD=KEY_EVENT|LEN=<len>|PART=1|TOTAL=1|ARG=<key>,<action>|CRC=<crc>^&
```

当前定义：

```text
key=PB1
action=PRESS

key=PB2
action=PRESS
```

示例：

```text
@#V1|SEQ=87|CMD=KEY_EVENT|LEN=9|PART=1|TOTAL=1|ARG=PB1,PRESS|CRC=XXXX^&
@#V1|SEQ=88|CMD=KEY_EVENT|LEN=9|PART=1|TOTAL=1|ARG=PB2,PRESS|CRC=XXXX^&
```

WiFi 收到后按普通协议帧规则校验，成功后可以回复 ACK：

```text
@#V1|SEQ=<seq>|CMD=ACK|LEN=<len>|PART=1|TOTAL=1|ARG=<key_event_seq>|CRC=<crc>^&
```

与 `DEVICE_INFO.wake_reason` 的关系：

```text
DEVICE_INFO.wake_reason=KEY_PB1 表示 PB1 长按把睡眠中的 WiFi 唤醒
DEVICE_INFO.wake_reason=KEY_PB2 表示 PB2 把睡眠中的 WiFi 唤醒
KEY_EVENT ARG=PB1,PRESS 表示 WiFi 已醒期间又发生了一次 PB1 长按触发
KEY_EVENT ARG=PB2,PRESS 表示 WiFi 已醒期间又发生了一次 PB2 按下
WiFi 未完成 DEVICE_INFO ACK 前发生 PB1/PB2 按键事件时，CH583/CH585 会在 ACK 后补发一次 KEY_EVENT
WiFi 睡眠时由 PB1/PB2 唤醒，只通过 DEVICE_INFO.wake_reason 上报，不额外发送 KEY_EVENT
```

## 20. WiFi 发送建议

由于 UART 无硬件流控，WiFi 发送重要协议帧时建议：

```text
发送协议帧前 200ms 不输出普通日志
发送协议帧
等待 UART TX 完成
发送协议帧后 200ms 再恢复普通日志
```

## 21. V2.2 必须实现的命令

```text
DEVICE_INFO
PING
PONG
ACK
ERR
GPIO
GPIO_READ
GPIO_VALUE
LED_BLINK
LED_BLINK_STOP
TIME_SET
TIME_GET
TIME_STATUS
WAKE_TIMER
POWER_OFF
LOWPOWER
NFC_SET
NFC_CLEAR
NFC_STATUS
BLE_DATA
WIFI_DATA
WIFI_PROVISION
WIFI_VER
KEY_EVENT
```

## 22. 接收方处理原则

```text
没有 @# 和 ^&：忽略
格式不完整：忽略或返回 BAD_FORMAT
CRC 错误：不执行，返回 BAD_CRC
LEN 错误：不执行，返回 BAD_LEN
PART/TOTAL 错误：不执行，返回 BAD_PART
CMD 不支持：不执行，返回 BAD_CMD
参数错误：不执行，返回 BAD_ARG
校验全部通过：执行命令
执行成功：返回 ACK 或对应响应
```
