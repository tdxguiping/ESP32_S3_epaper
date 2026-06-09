# TDX ESP32-S3 Web Flasher v13

功能：

1. Web Serial 烧写 ESP32-S3 固件。
2. 固定 4 个烧写地址：
   - `0x0000` bootloader.bin
   - `0x8000` partition-table.bin
   - `0xD000` ota_data_initial.bin
   - `0x20000` file_server.bin
3. 烧写完成后自动打开串口控制台，并调用 Reset Device。
4. 串口控制台支持后台检测已授权的 USB JTAG/serial debug unit 并自动连接。
5. 新增 `serial_protocol.html`，用于 USB Serial/JTAG HTTP-like 串口协议联调。

## 串口协议 v13

`serial_protocol.html` 不再使用 JSON + Base64 分片。它改成“串口承载 HTTP 文本”：

```http
GET /ping HTTP/1.1
Host: usb
Content-Length: 0


```

POST JSON：

```http
POST /delete HTTP/1.1
Host: usb
Content-Type: application/json
Content-Length: 32

{"func":"delete","fileNames":["26422"]}
```

图片/文件上传使用 HTTP-like multipart，文件内容保持原始二进制：

```http
POST /cast HTTP/1.1
Host: usb
Content-Type: multipart/form-data; boundary=----TDXUSB...
Content-Length: 123456

<multipart body>
```

设备端接收必须按 `Content-Length` 判断 body 是否完整，不要用超时作为请求结束条件。

设备端响应统一包一层 USB 帧头/帧尾，方便 PC 工具从调试日志中识别协议响应：

```text
@#$
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: 35

{"func":"cast_result","result":0}
%^&
```

## 目录结构

```text
fuse_flash/
├── run_esp32s3_web_flasher.bat
├── bootloader.bin
├── partition-table.bin
├── ota_data_initial.bin
├── file_server.bin
└── tdx_esp32s3_web_flasher_file_input_v13/
    ├── index.html
    ├── serial_protocol.html
    ├── V2_相框传图协议.html
    ├── js/
    └── css/
```

## 启动

在 `fuse_flash` 目录运行：

```bash
python -m http.server 8000
```

浏览器打开：

```text
http://localhost:8000/tdx_esp32s3_web_flasher_file_input_v13/index.html
```

或者双击配套的 `run_esp32s3_web_flasher.bat`。


## V18 修改

- 在主页面和串口协议页面增加右下角失败信息小窗口。
- 串口打开、接收、发送、Reset、操作异常、页面脚本异常都会显示在小窗口中。
- 串口发送每块写入加入超时保护，失败后断开串口，避免页面卡住。


## V20 更新

- 在串口协议页面增加 WiFi POST /wifi 请求预览窗口。
- Content-Length 按 UTF-8 JSON body 字节数自动计算。
- “复制 WiFi 请求到原始发送区”可生成可直接从 PC 串口发送的完整 HTTP-like 文本。
- “发送 POST /wifi”按钮发送的内容与预览格式一致。
- 串口协议页面默认波特率调整为 921600。
- 接收窗口支持解析 `@#$` / `%^&` 包裹的设备响应帧。
- multipart 预览隐藏二进制 body，只保留 header 和总长度，避免页面日志被二进制内容干扰。
