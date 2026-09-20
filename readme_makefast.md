<a id="top"></a>
# CH585 图像路径、速度优化与内存

本文只记录当前 800×480 六色 SPD1657 项目中，大文件/小文件图像路径、为缩短耗时所做的修改、SPI Flash 路径，以及相关内存分配。

<a id="toc"></a>

## 目录

- [1. 大文件格式与小文件格式](#sec-1)
  - [1.1 如何切换](#sec-1-1)
- [2. 小文件格式调用时序](#sec-2)
- [3. 大文件格式调用时序](#sec-3)
- [4. 为缩短耗时做的修改](#sec-4)
  - [4.1 解压输出和写屏](#sec-4-1)
  - [4.2 EPD 驱动和初始化](#sec-4-2)
  - [4.3 大小文件统一成功通知](#sec-4-3)
  - [4.4 AB 同图与 AB 异图时序](#sec-4-4)
  - [4.5 SPI Flash](#sec-4-5)
- [5. 内存分配](#sec-5)
- [6. 当前小文件耗时参考](#sec-6)

> 使用说明：点击目录项可直接跳到对应正文；正文每个章节末尾均提供 **[↩ 返回目录](#toc)** 链接。

<a id="sec-1"></a>
## 大文件格式与小文件格式

当前工程编译时启用 `TDX_SMALL_STREAM_ENABLE=1`，同时保留两种图像接收路径。路径选择在一张图的首包读取并固定到该次会话。

- `TDX_IMAGE_SMALL_STREAM = 0`：小文件格式，压缩数据进入 RAM 环形 FIFO，边接收边解压并写屏，不把图像压缩数据存入 SPI Flash。
- `TDX_IMAGE_LARGE_FLASH = 1`：大文件格式，压缩数据先存入 SPI Flash，收包结束后再从 Flash 读出、解压并写屏。
- `TDX_SMALL_STREAM_ENABLE` 是是否编入小文件路径的编译开关；它不是运行时选择值。release 构建脚本传入 `-DTDX_SMALL_STREAM_ENABLE=1`。

两种模式使用相同的 ZLIB 解码器、原始屏幕数据和 EPD 输出路径；区别是压缩输入暂存位置及处理时序，不是两套颜色格式。小文件模式用 RAM FIFO 边收边处理；大文件模式用 SPI Flash 暂存，收完后回放。

[↩ 返回目录](#toc)

<a id="sec-1-1"></a>
### 如何切换

当前选择变量在 [tdx_image_stream.c](BLE/BackupUpgrade_OTA/Profile/tdx_image_stream.c) 中初始化：

```c
TDX_IMAGE_MODE g_tdx_image_transfer_mode = TDX_IMAGE_SMALL_STREAM;
```

要让固件默认走大文件路径，将初始化值改为 `TDX_IMAGE_LARGE_FLASH` 后重新编译；恢复小文件路径则改回 `TDX_IMAGE_SMALL_STREAM`。选择在 `tdxInfo_WriteAttrCB()` 收到首包时复制到 `s_image_session_mode`，所以必须在一张图的首包到达前确定。

当前代码没有提供 BLE 命令来切换 `g_tdx_image_transfer_mode`。`TDXINFO_SWITCH_MODE` 修改 EEPROM 中的设备工作模式，不选择图像大/小文件路径。`TIS_UseLarge()` 用于停止/清理小文件流状态，也不会修改上述选择变量。

如果构建时设为 `TDX_SMALL_STREAM_ENABLE=0`，小文件流实现会被编译掉，图像接收走大文件 Flash 路径。

[↩ 返回目录](#toc)

<a id="sec-2"></a>
## 小文件格式调用时序

```text
BLE GATT 写入
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   └─ case TDXINFO_SEND_DATA
      ├─ 首包：decrypt_ecb() / malloc+memcpy
      ├─ 校验图像头，并读取 g_tdx_image_transfer_mode
      └─ 若为 TDX_IMAGE_SMALL_STREAM
         └─ Profile/tdx_image_stream.c: TIS_Begin()
            └─ Profile/zlib_image_codec.c: zlib_image_codec_begin(pixel_sink)

后续 BLE 数据包
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   └─ TDXINFO_SEND_DATA
      ├─ decrypt_ecb() / malloc+memcpy
      └─ Profile/tdx_image_stream.c: TIS_Push()
         ├─ memcpy 到 zip_fifo（回绕时分成两段）
         └─ schedule(SBP_IMAGE_STREAM_EVT)

TMOS 解码处理
└─ APP/peripheral.c: Peripheral_ProcessEvent() 的 SBP_IMAGE_STREAM_EVT 分支
   └─ Profile/tdx_image_stream.c: TIS_Process()
      ├─ Profile/zlib_image_codec.c: zlib_image_codec_pump()
      │  └─ zic_run() / zic_decode() 解压
      ├─ zlib_image_codec.c: zic_flush()
      │  └─ TIS pixel_sink()
      │     └─ APP/api/display_api.c: refreshScreenColor(..., IS_NONEED_DECMPRESS)
      │        └─ data_decrypt()
      │           └─ ImgDataCallBack(pData, Len)
      │              └─ Display_Picture_To_Color(pData, dataLen, offset)
      │                 └─ SPI0_MasterTrans() 连续写面板数据
      └─ 收包及解压全部完成后：zlib_image_codec_finish()
         ├─ TdxInfo_ArmRefreshNotify(..., 1)，登记一次待发送成功通知
         └─ 解除刷新延迟并调用 Display_EPD_AB()
            └─ SPD1657_400_600_color6.c: Display_EPD_Driver()
               ├─ 发送 EPD `0x12` 刷新命令
               └─ 启动 BUSY 观察，不等待物理刷新完成
            └─ Display_EPD_AB() 内部撤销片选后调用 TdxInfo_RefreshCommandIssued()
               └─ 立即发送一次成功通知
```

小文件的 ZLIB 解压和 SPI 写屏由 TMOS 事件逐步处理，可与 BLE 收包交错进行。`TIS_Push()` 只把尚未处理的压缩输入放入 FIFO；FIFO 大小代表可容纳的未处理积压量，不是整张图片的大小上限。小文件路径不调用 `zlib_image_store_begin/feed/finish()`，因此不写 SPI Flash。

[↩ 返回目录](#toc)

<a id="sec-3"></a>
## 大文件格式调用时序

```text
BLE GATT 写入
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   └─ case TDXINFO_SEND_DATA
      ├─ 首包：decrypt_ecb() / malloc+memcpy，解析图像头
      └─ InitFirstPackage()
         ├─ APP/api/flash_api.c: InitFlashDriver()
         │  └─ APP/driver/flash/extern_flash.c: initExternFlashDriver()
         └─ Profile/zlib_image_store.c: zlib_image_store_begin()
            ├─ 擦除该图像槽起始扇区
            └─ 读回页 0，检查擦除后空白状态

后续 BLE 数据包
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   └─ InitOtherPackage()
      └─ Profile/zlib_image_store.c: zlib_image_store_feed()
         ├─ 以 256 字节页缓冲压缩输入
         ├─ 每到 4 KB 扇区边界时擦除扇区
         └─ write_page()
            └─ APP/driver/flash/extern_flash.c:
               TDX_SPI_FLASH_W_256Bytes()
               ├─ SPI_FLASH_WriteEnable()
               ├─ SPI1_MasterSendByte() 发送页编程命令、地址和 256 字节
               └─ SPI_FLASH_WaitForWriteEndTimeout()

最后一包
└─ Profile/tdxinfoservice.c: InitOtherPackage()
   └─ Profile/zlib_image_store.c: zlib_image_store_finish()
      ├─ 写入最后一个不足 256 字节的页（尾部填充）
      └─ 最后写页 0 的提交头，标记图像可回放
   ├─ DeInitFlashDriver()
   ├─ TdxInfo_ArmRefreshNotify()，登记待发送成功通知
   └─ 排入刷新事件；此时不提前通知APP

Flash 回放和写屏
└─ APP/peripheral_main.c: EVENT_Get_Battle_Charge
   └─ APP/api/presave_api.c: preSaveDisplayColor()
      └─ Profile/zlib_image_store.c: zlib_image_store_replay()
         ├─ 读取并检查提交头
         ├─ 按 256 字节从 SPI Flash 读压缩数据
         └─ zlib_image_codec_begin/feed/finish()
            └─ zic_flush() → zlib_panel_sink()
               └─ refreshScreenColor(..., IS_NONEED_DECMPRESS)
                  └─ data_decrypt()
                     └─ ImgDataCallBack() → Display_Picture_To_Color()
                        └─ SPI0_MasterTrans() 连续写面板数据
      └─ 读完且写屏数据完成后调用 Display_EPD_AB()
         └─ Display_EPD_Driver()
            ├─ 发送 EPD `0x12` 刷新命令
            └─ 启动 BUSY 观察，不等待物理刷新完成
         └─ Display_EPD_AB() 内部撤销片选后调用 TdxInfo_RefreshCommandIssued()
            └─ 立即发送一次成功通知
```

大文件格式用外部 Flash 保存压缩图像，降低对“全图压缩输入 RAM 缓冲”的依赖；代价是收包后要经过 Flash 回放，增加写入、读取和解压流程。当前槽大小由 `EXTERN_FLASH_PIC_SIZE=188 KiB` 决定，提交页占 256 字节，压缩数据容量为 192256 字节。

[↩ 返回目录](#toc)

<a id="sec-4"></a>
## 为缩短耗时做的修改

<a id="sec-4-1"></a>
### 解压输出和写屏

- [img_perf.h](BLE/BackupUpgrade_OTA/APP/include/img_perf.h) 中 `IMG_ZLIB_BATCH=4095`。解压器使用该值作为输出批次上限，避免过小批次造成过多回调。
- 当前构建启用 `TDX_LARGE_RAW_COLOR_DISABLE=1` 和 `TDX_LARGE_RAW_DIRECT=1`。六色输入已经是面板字节格式时直接交给回调，跳过旧逐字节颜色转换。当前固件中 32 KB `imageBatchBuffer` 被条件编译排除；它仍保留在其他非 direct 配置的源码分支中。
- SPD1657 六色批量数据经 `SPI0_MasterTrans()` 使用 FIFO 连续发送，每段最长 4095 字节；不是每个像素调用一次 `SPI0_MasterSendByte()`。
- 批量图像传输开始时使用 `IMG_PANEL_SPI_DIV=6`，完成后恢复原 SPI 分频和主机延时设置。此加速针对面板 SPI0；SPI Flash 使用 SPI1，时钟没有因此改变。

[↩ 返回目录](#toc)

<a id="sec-4-2"></a>
### EPD 驱动和初始化

- release 构建设置 `TDX_DISABLE_EPD_DELAY=1`。`display_api.c` 中不必要的 `DeviceInit()` 5 ms、`DevicePower()` 5 ms 和数据开始前 20 ms 等待被跳过。
- SPD1657 电源准备路径不再等待 200 ms，保留 10 ms 电源稳定等待。复位路径在此设置下保留约 2 ms 复位延时；`Init_EPD_Driver()` 仍执行复位、寄存器初始化和 `EPD_Check_Busy()`。
- `Display_EPD_Driver()` 当前直接发送 `0x12` 刷新命令并启动 BUSY 状态观察；刷新命令后不阻塞等待物理刷新完成。`Display_EPD_AB()` 返回表示刷新命令已发出，实际 BUSY 完成由后续监控处理。
- 小文件路径与大文件回放路径最终复用 `refreshScreenColor()`、`ImgDataCallBack()`、`Display_Picture_To_Color()` 和同一个 EPD 驱动链路，避免维护两套写屏流程。

[↩ 返回目录](#toc)

<a id="sec-4-3"></a>
### 大小文件统一成功通知

大小文件模式的图像显示成功通知共用 `TdxInfo_ArmRefreshNotify()` 和 `TdxInfo_RefreshCommandIssued()`。成功通知不再在蓝牙数据刚接收完成时提前发送，也不再由小文件模式连续发送 5 次。通知发生在 SPD1657 的 `0x12` 刷新命令及其参数已经送上 SPI 总线、驱动返回并撤销片选之后，但仍在 `Display_EPD_AB()` 返回之前，不等待 BUSY 结束。

```text
大文件模式
└─ Profile/tdxinfoservice.c: InitOtherPackage()
   ├─ 完成 Flash 提交
   ├─ TdxInfo_ArmRefreshNotify(conn, type, refresh_count)
   └─ APP/peripheral_main.c: EVENT_Get_Battle_Charge
      └─ APP/api/presave_api.c: preSaveDisplayColor()
         └─ APP/api/display_api.c: Display_EPD_AB()
            └─ APP/driver/epd/SPD1657_400_600_color6.c: Display_EPD_Driver()
               ├─ EPD_W21_WriteCMD(0x12)
               ├─ EPD_W21_WriteDATA(0x00)
               └─ 返回 Display_EPD_AB()
            └─ 撤销 A/B 片选
               └─ Profile/tdxinfoservice.c: TdxInfo_RefreshCommandIssued()
                  └─ notitySendEnd() → notitySendFunc()，成功通知一次

小文件模式
└─ Profile/tdx_image_stream.c: TIS_Process()
   ├─ 完成解压和写屏数据
   ├─ TdxInfo_ArmRefreshNotify(conn, type, 1)
   └─ APP/api/display_api.c: Display_EPD_AB()
      └─ APP/driver/epd/SPD1657_400_600_color6.c: Display_EPD_Driver()
         ├─ EPD_W21_WriteCMD(0x12)
         ├─ EPD_W21_WriteDATA(0x00)
         └─ 返回 Display_EPD_AB()
      └─ 撤销 A/B 片选
         └─ Profile/tdxinfoservice.c: TdxInfo_RefreshCommandIssued()
            └─ notitySendEnd() → notitySendFunc()，成功通知一次
```

“通知一次”指每个图像事务只调用一次 `notitySendFunc()`。该函数内部仍保留 BLE 栈返回 `blePending`、`bleNoResources` 或 `bleTimeout` 时最多 3 次、每次间隔 5 ms 的短重试。连接断开、解压失败、Flash 回放失败或写屏流程取消时会清除待发送状态，避免下一次显示误发旧通知。

AB 异图的 A 图是协议例外：`OP_TYPE_IMG_DIFF_A` 只把 A 图保存到 Flash，不会立即调用 `Display_EPD_AB()`。APP需要收到这次存储成功应答后才能继续发送 B 图，因此 A 图保存完成后仍立即通知一次；收到 B 图后执行两屏显示，最终显示成功通知在 B 屏刷新命令发出后发送。

[↩ 返回目录](#toc)

<a id="sec-4-4"></a>
### AB 同图与 AB 异图时序

APP 首包中的 `fScreenType` 决定双屏路径：`OP_TYPE_IMG_AB` 和 `OP_TYPE_SAVE_IMG_AB` 表示 AB 同图；`OP_TYPE_IMG_DIFF_A`、`OP_TYPE_IMG_DIFF_B` 表示 AB 异图。SPI 片选为低电平有效，A/B 同时接收命令或数据时两路片选都应拉低，片选拉高表示未选中。

```text
AB 同图
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   ├─ 识别 OP_TYPE_IMG_AB / OP_TYPE_SAVE_IMG_AB
   └─ global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB
      └─ APP/api/display_api.c: Display_Picture_To_Color()
         ├─ DeviceInit()
         ├─ DevicePower()：A/B 片选同时有效
         ├─ Init_EPD_Driver()：A/B 同时复位、同时初始化
         ├─ SPI0_MasterTrans()：同一份图像数据同时写入 A/B
         └─ Display_EPD_AB()
            └─ Display_EPD_Driver()
               ├─ A/B 同时收到刷新命令
               └─ TdxInfo_RefreshCommandIssued()：成功通知一次

AB 异图
└─ Profile/tdxinfoservice.c: tdxInfo_WriteAttrCB()
   ├─ OP_TYPE_IMG_DIFF_A：A 图写入 Flash，立即返回一次存储成功应答
   └─ OP_TYPE_IMG_DIFF_B：B 图写入 Flash，登记两次刷新命令后通知
      └─ APP/peripheral_main.c: EVENT_Get_Battle_Charge
         ├─ APP/api/display_api.c: EPD_PrepareABDiff()
         │  ├─ A/B 同时上电和 DeviceInit()
         │  ├─ A/B 复位线同时复位
         │  ├─ A/B 片选同时有效
         │  └─ Init_EPD_Driver()：两屏只初始化一次
         ├─ A 屏阶段
         │  └─ preSaveDisplayColor(A)
         │     ├─ 只选择 A 屏并写入 A 图数据
         │     └─ Display_EPD_AB()：发出 A 刷新命令，通知计数 2→1
         └─ B 屏阶段
            └─ preSaveDisplayColor(B)
               ├─ 只选择 B 屏并写入 B 图数据
               └─ Display_EPD_AB()：发出 B 刷新命令，通知计数 1→0
                  └─ 成功通知一次
```

AB 异图流程取消了 A、B 两屏之间固定的 `mDelaymS(2000)`。B 屏开始写入前不再执行公共复位和完整驱动初始化，因此不会因为初始化 B 而复位正在刷新的 A。A 图完成后不进入待机，B 图刷新命令发出后才进入原有低功耗/BUSY监控流程。任一屏回放失败时会取消共用初始化状态和待发送成功通知。

[↩ 返回目录](#toc)

<a id="sec-4-5"></a>
### SPI Flash

- 首次 Flash 初始化使用 `TDX_FLASH_POWERUP_DELAY_MS=10`，随后通过 JEDEC ID 轮询确认器件就绪；不是每次图像写入都固定等待 200 ms。Flash 保持供电时，后续初始化不重复执行上电等待。
- `TDX_FLASH_WIP_LEGACY_DELAY=0` 关闭 WIP 状态轮询间固定 1 ms 延时。程序轮询状态寄存器，在 Flash 完成时立即继续，同时保留页编程和扇区擦除超时及错误返回检查。
- 图像以 256 字节页写入，4 KB 扇区按 16 页边界擦除；页 0 作为提交头最后写入，避免未完整接收的图像被当作有效数据回放。
- 当前 `zlib_image_store.c` 不再对已编程页执行回读比较，`T FV` 和 `T VC` 为 0。擦除后的页 0 空白检查、擦除/编程 API 错误检查、回放时的提交头检查均保留。回放必须从 Flash 读取压缩数据，这是大文件路径的一部分。
- Flash SPI1 页编程仍通过 `SPI1_MasterSendByte()` 发送命令、地址和数据。本轮 Flash 耗时优化来自缩短固定等待并取消写后重复读回，并未改 SPI1 传输时钟或页编程协议。

[↩ 返回目录](#toc)

<a id="sec-5"></a>
## 内存分配

以下数值来自当前 A/T release 构建对应的 `obj/BackupUpgrade_OTA.map` 和链接报告。

| 对象 | 地址/大小 | 用途 |
|---|---:|---|
| `zip_fifo` | 87040 字节（85 KiB） | 小文件模式的压缩输入环形 FIFO |
| `stream` | 116 字节 | FIFO 索引、会话状态和耗时统计 |
| `zic` | 10064 字节 | ZLIB 解码器状态，含 4 KiB 历史窗口和最多 4095 字节输出批次 |
| `store` | 268 字节 | Flash 页缓冲（256 字节）及存储状态 |
| `MEM_BUF` | 6144 字节 | BLE 协议栈堆 |
| `imageBatchBuffer` | 当前固件未分配 | 32 KiB 旧颜色转换/批处理缓冲区，当前 direct build 条件编译掉 |

当前链接报告 RAM 使用 128716 / 131072 字节，剩余 2356 字节。`zip_fifo` 是静态全局区：即使单次图像选择大文件模式，只要 `TDX_SMALL_STREAM_ENABLE=1` 编入小文件支持，这 85 KiB 仍被固件保留，不会因切换到大文件而释放。大文件模式另外使用 `store` 页缓冲，不需要再分配整张图的 RAM 缓冲。

收包回调会为当前 GATT 数据块分配临时解密/拷贝缓冲并在处理后释放；`preSaveDisplayColor()` 也分配 256 字节 `PicData` 临时缓冲。该临时分配与上表中的静态 FIFO、解码器和 Flash 页缓冲用途不同。

[↩ 返回目录](#toc)

<a id="sec-6"></a>
## 当前小文件耗时参考

最近三次 192000 字节屏幕输出测试的日志都显示 `IMG done` 和 `S refresh-done`。字段以十六进制打印；换算为毫秒后：

| 字段 | 三次测试 |
|---|---:|
| `T ZIP+OUT` | 5262 / 4616 / 2045 ms |
| `T DECODE` | 4783 / 4137 / 1566 ms |
| `T SPI` | 三次均约 490 ms |
| `T FLOW` | 10819 / 9195 / 2233 ms |
| `T MAXQ` | 13827 / 43992 / 884 字节 |

`T ZIP+OUT` 包含解压和输出；`T DECODE` 扣除了 SPI 输出耗时，二者不能相加。`T FLOW` 覆盖整个小文件处理流程；其中接收与解压可以重叠，也不能简单把各项相加作为总时间。`T MAXQ` 是 FIFO 峰值占用，不是时间，也不是图片总长度。

日志计时点对应的函数区间：

- `T RX`：从 `TIS_Begin()` 到最后一个输入包到达的时间；包含系统调度，不是单纯的 BLE 空中传输时间。
- `T ZIP+OUT`：所有 `zlib_image_codec_pump()` 调用的累计时间，包含 pump 内触发的输出回调。
- `T DECODE`：pump 时间扣除输出回调时间后的累计值。
- `T SPI`：源码中包住 `refreshScreenColor()` 的输出路径累计时间，包含回调和首批 EPD 准备工作，不应当作纯 SPI 线上传输时间。
- `T EPD`：`Display_EPD_AB()` 函数调用耗时，不包括面板之后的物理 BUSY 刷新时间。
- `T FLOW`：从 `TIS_Begin()` 到刷新命令调用及统计的总流程时间；接收和解压有重叠。
- `T FINAL`：最后一个输入包到达后，处理剩余 FIFO、完成解压并发出刷新命令所需时间。
- `T MAXQ`：FIFO 历史最高占用字节数；`T ZIN` 是送入解压器的累计压缩字节数；`T PUMP/T INN/T EVT/T SLICE` 是调用或数据块计数。

大文件回放日志中，`T FI` 是 Flash 槽初始化/起始擦除和空白检查阶段；`T FET` 是累计扇区擦除时间；`T FP` 是累计页编程时间；`T FW` 是数据页写入总时间（在扇区边界会包含该次擦除，因此与 `T FET` 有重叠）。`T FR` 是回放读 Flash 时间，`T FD` 是回放解码输入处理时间（包括解码时触发的 sink 输出），`T WRITE` 是 sink 写屏路径累计时间，`T REF` 是 `Display_EPD_AB()` 调用时间，`T ALL` 是回放显示流程总时间。计时项有父子包含关系，不能简单相加。

[↩ 返回目录](#toc)

