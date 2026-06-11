// Serial HTTP-like page for the frame image protocol.
// 串口 HTTP-like 页面：所有控制和传图都通过 USB Serial/JTAG 发送 HTTP-like 请求。
// v20 keeps v18 RX protections and makes manual Disconnect fire-and-forget.
// v20 保留 v18 接收保护，并把手动“断开”改成立即返回、后台释放串口，避免页面卡住。
const USB_PORT_FILTERS = [
  { usbVendorId: 0x10c4, usbProductId: 0xea60 },
  { usbVendorId: 0x0403, usbProductId: 0x6010 },
  { usbVendorId: 0x303a, usbProductId: 0x1001 },
  { usbVendorId: 0x303a, usbProductId: 0x1002 },
  { usbVendorId: 0x303a, usbProductId: 0x0002 },
  { usbVendorId: 0x303a, usbProductId: 0x0009 },
  { usbVendorId: 0x1a86, usbProductId: 0x55d4 },
  { usbVendorId: 0x1a86, usbProductId: 0x7523 },
  { usbVendorId: 0x0403, usbProductId: 0x6001 },
];

const AUTO_CONSOLE_PORT_MATCHES = [
  { usbVendorId: 0x303a, usbProductId: 0x1001, label: "USB JTAG/serial debug unit" },
  { usbVendorId: 0x303a, usbProductId: 0x0009, label: "ESP32-S3 USB Serial" },
  { usbVendorId: 0x303a, usbProductId: 0x0002, label: "ESP32-S2 USB Serial" },
  { usbVendorId: 0x303a, usbProductId: 0x1002, label: "Espressif USB bridge" },
];

// Notify the main flashing page that this protocol page owns the serial port.
// 通知主烧写页面：当前协议页面正在使用串口，主页面必须断开并停止自动连接。
const SERIAL_PROTOCOL_CHANNEL_NAME = "tdx_esp32s3_serial_protocol_channel";
const SERIAL_PROTOCOL_ACTIVE_KEY = "tdx_serial_protocol_page_active";
const SERIAL_PROTOCOL_HEARTBEAT_KEY = "tdx_serial_protocol_page_heartbeat";
let serialProtocolBroadcastChannel = null;
let serialProtocolHeartbeatTimer = null;

let serialPort = null;
let serialReader = null;
let reading = false;
let manualDisconnect = false;
let reconnectBusy = false;
let rxTextBuffer = "";
let autoDetectTimer = null;

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();
const $ = (id) => document.getElementById(id);
const rxOutput = $("rxOutput");
const statusEl = $("status");

// Keep serial output rendering lightweight. Directly doing rxOutput.textContent += text for
// every serial packet becomes O(n^2) and can freeze Chrome when the device prints many logs.
// 保持串口窗口轻量刷新。每个串口包都直接 textContent += text 会变成 O(n^2)，设备日志多时会卡死页面。
const RX_DISPLAY_MAX_CHARS = 180000;
const RX_PARSE_BUFFER_MAX_CHARS = 65536;
const RX_FLUSH_INTERVAL_MS = 80;
const RX_FLOOD_WINDOW_MS = 2000;
const RX_FLOOD_MAX_BYTES = 1024 * 1024;
let rxPendingText = "";
let rxCopyText = "";
let rxFlushTimer = null;
let rxFloodWindowStart = Date.now();
let rxFloodBytes = 0;
// Keep serial upload defaults fast enough for large multipart image transfers.
// 保持串口上传默认参数适合大 multipart 图片传输。
const SERIAL_DEFAULT_BUFFER_SIZE = 65536;
const SERIAL_DEFAULT_WRITE_CHUNK_SIZE = 16384;
const SERIAL_DEFAULT_WRITE_CHUNK_TIMEOUT_MS = 15000;
const EPD_TYPE_TABLE = [
  { type: 1, width: 800, height: 480, display_size: 192000, name: "EPD_800_480", color_type: 3, color_name: "BWR_3_Color", color_count: 3, colors: "3 色" },
  { type: 2, width: 1024, height: 600, display_size: 307200, name: "EPD_1024_600", color_type: 6, color_name: "BWYRBG_6_Color", color_count: 6, colors: "6 色" },
  { type: 3, width: 1600, height: 1200, display_size: 960000, name: "EPD_1600_1200_79", color_type: 6, color_name: "BWYRBG_6_Color", color_count: 6, colors: "6 色" },
  { type: 4, width: 1600, height: 1200, display_size: 960000, name: "EPD_1600_1200_133", color_type: 6, color_name: "BWYRBG_6_Color", color_count: 6, colors: "6 色" },
  { type: 5, width: 1360, height: 480, display_size: 81600, name: "EPD_1360_480_1085", color_type: 4, color_name: "BWRY_4_Color", color_count: 4, colors: "4 色" },
  { type: 6, width: 800, height: 480, display_size: 96000, name: "EPD_800_480_4S_75", color_type: 4, color_name: "BWRY_4_Color", color_count: 4, colors: "4 色" },
  { type: 7, width: 1360, height: 480, display_size: 163200, name: "EPD_1360_480_1085_3COLOR", color_type: 3, color_name: "BWR_3_Color", color_count: 3, colors: "3 色" },
  { type: 8, width: 800, height: 480, display_size: 96000, name: "EPD_800_480_4S_75_DKE", color_type: 4, color_name: "BWRY_4_Color", color_count: 4, colors: "4 色" },
  { type: 9, width: 800, height: 480, display_size: 96000, name: "EPD_800_480_4S_75_mofang", color_type: 4, color_name: "BWRY_4_Color", color_count: 4, colors: "4 色" },
];
const EPD_COLOR_TABLE = {
  BWR_3_Color: [
    { label: "Black", css: "#111827" },
    { label: "White", css: "#ffffff" },
    { label: "Red", css: "#dc2626" },
  ],
  BWRY_4_Color: [
    { label: "Black", css: "#111827" },
    { label: "White", css: "#ffffff" },
    { label: "Red", css: "#dc2626" },
    { label: "Yellow", css: "#facc15" },
  ],
  BWYRBG_6_Color: [
    { label: "Black", css: "#111827" },
    { label: "White", css: "#ffffff" },
    { label: "Red", css: "#dc2626" },
    { label: "Yellow", css: "#facc15" },
    { label: "Blue", css: "#2563eb" },
    { label: "Green", css: "#16a34a" },
  ],
};
let currentEpdType = null;
let epdTypeList = [];
let operationToastTimer = null;
let pendingEpdListRefresh = false;
let pendingEpdSetType = null;
let pendingEpdTest = false;

const els = {
  baudrate: $("baudrate"), dataBits: $("dataBits"), stopBits: $("stopBits"), parity: $("parity"), flowControl: $("flowControl"), bufferSize: $("bufferSize"),
  serialOpenTimeoutMs: $("serialOpenTimeoutMs"), serialResetTimeoutMs: $("serialResetTimeoutMs"),
  writeChunkSize: $("writeChunkSize"), writeChunkDelayMs: $("writeChunkDelayMs"), writeChunkTimeoutMs: $("writeChunkTimeoutMs"),
  jsonRouteMode: $("jsonRouteMode"), fileRouteMode: $("fileRouteMode"), autoConnect: $("autoConnect"), prettyRxJson: $("prettyRxJson"),
  connectButton: $("connectButton"), resetButton: $("resetButton"), disconnectButton: $("disconnectButton"), clearRxButton: $("clearRxButton"),
  copyRxButton: $("copyRxButton"),
  rawText: $("rawText"), sendRawButton: $("sendRawButton"), sendRawJsonButton: $("sendRawJsonButton"),
  wifiSsid: $("wifiSsid"), wifiPassword: $("wifiPassword"), sendWifiButton: $("sendWifiButton"),
  wifiRequestPreview: $("wifiRequestPreview"), copyWifiRawButton: $("copyWifiRawButton"),
  slideshowFiles: $("slideshowFiles"), slideshowInterval: $("slideshowInterval"), slideshowRandom: $("slideshowRandom"), sendStartSlideshowButton: $("sendStartSlideshowButton"),
  slideshowSw: $("slideshowSw"), setSlideshowInterval: $("setSlideshowInterval"), setSlideshowRandom: $("setSlideshowRandom"), sendSetSlideshowButton: $("sendSetSlideshowButton"),
  deleteFiles: $("deleteFiles"), sendDeleteButton: $("sendDeleteButton"), wifiWorkSeconds: $("wifiWorkSeconds"), sendWifiWorkTimeButton: $("sendWifiWorkTimeButton"),
  directFilePath: $("directFilePath"), directFileContentType: $("directFileContentType"), directFileFunc: $("directFileFunc"), directFileInput: $("directFileInput"), sendDirectFileButton: $("sendDirectFileButton"),
  singleFunc: $("singleFunc"), singleFileName: $("singleFileName"), singleOldFileName: $("singleOldFileName"), singleSave: $("singleSave"), singleShow: $("singleShow"), singleBin: $("singleBin"), singleImage: $("singleImage"), sendSingleMultipartButton: $("sendSingleMultipartButton"),
  cast2picScreen: $("cast2picScreen"), cast2picSave: $("cast2picSave"), cast2picShow: $("cast2picShow"), pic1Name: $("pic1Name"), pic1Bin: $("pic1Bin"), pic1Image: $("pic1Image"), pic2Name: $("pic2Name"), pic2Bin: $("pic2Bin"), pic2Image: $("pic2Image"), sendCast2picButton: $("sendCast2picButton"),
  epdTypeState: $("epdTypeState"), epdTypeSummary: $("epdTypeSummary"), epdTypeDetails: $("epdTypeDetails"), epdColorSwatches: $("epdColorSwatches"),
  refreshEpdTypesButton: $("refreshEpdTypesButton"), testEpdDisplayButton: $("testEpdDisplayButton"), epdTypeList: $("epdTypeList"),
  operationToast: $("operationToast"),
  singleBinMatch: $("singleBinMatch"), pic1BinMatch: $("pic1BinMatch"), pic2BinMatch: $("pic2BinMatch"),
  fileInfoOutput: $("fileInfoOutput"),
};

function delay(ms) { return new Promise((resolve) => setTimeout(resolve, ms)); }
function yieldToUi() { return new Promise((resolve) => setTimeout(resolve, 0)); }
function getMsSetting(id, fallback, min = 100, max = 60000) {
  const el = $(id);
  const n = parseInt(el?.value || String(fallback), 10);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, n)) : fallback;
}
function withTimeout(promise, ms, message) {
  let timer = null;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(message || `operation timeout after ${ms} ms`)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => {
    if (timer) clearTimeout(timer);
  });
}
function settleWithin(promise, ms, label) {
  // Resolve after timeout instead of waiting forever. This is used only for cleanup paths.
  // 清理串口时只等固定时间，超时就返回，不让页面一直卡在 await。
  let timer = null;
  return new Promise((resolve) => {
    let done = false;
    const finish = (result) => {
      if (done) return;
      done = true;
      if (timer) clearTimeout(timer);
      resolve(result);
    };
    timer = setTimeout(() => finish({ ok: false, timeout: true, label }), ms);
    Promise.resolve(promise).then(
      () => finish({ ok: true, label }),
      (err) => finish({ ok: false, timeout: false, label, err })
    );
  });
}
function setStatus(text) { statusEl.textContent = `状态：${text}`; }
function showOperationToast(message, ok = true) {
  if (!els.operationToast) return;
  if (operationToastTimer) clearTimeout(operationToastTimer);
  els.operationToast.textContent = `[${nowTimeText()}] ${message}`;
  els.operationToast.className = `operationToast ${ok ? "" : "bad"}`;
  operationToastTimer = setTimeout(() => {
    els.operationToast.classList.add("operationToastHidden");
  }, 4200);
}
function flushRxOutput() {
  if (!rxPendingText) return;
  const nextText = `${rxOutput.textContent}${rxPendingText}`;
  rxOutput.textContent = nextText.length > RX_DISPLAY_MAX_CHARS ? nextText.slice(-RX_DISPLAY_MAX_CHARS) : nextText;
  rxPendingText = "";
  rxOutput.scrollTop = rxOutput.scrollHeight;
  if (rxFlushTimer) {
    clearTimeout(rxFlushTimer);
    rxFlushTimer = null;
  }
}

function scheduleRxFlush() {
  if (rxFlushTimer) return;
  rxFlushTimer = setTimeout(flushRxOutput, RX_FLUSH_INTERVAL_MS);
}

function appendRx(text) {
  const chunk = String(text);
  rxCopyText += chunk;
  rxPendingText += chunk;
  if (rxPendingText.length > RX_DISPLAY_MAX_CHARS) {
    rxPendingText = rxPendingText.slice(-RX_DISPLAY_MAX_CHARS);
  }
  scheduleRxFlush();
}
function rxLine(text) { appendRx(`${text}\n`); }

function epdTypeById(type) {
  return EPD_TYPE_TABLE.find((item) => item.type === Number(type)) || null;
}

function mergeEpdConfig(obj) {
  const tableConfig = epdTypeById(obj?.type);
  const colorCount = Number(obj?.color_count ?? obj?.color_type ?? tableConfig?.color_count ?? tableConfig?.color_type ?? 0);
  return {
    ...(tableConfig || {}),
    ...obj,
    display_size: Number(obj?.display_size ?? obj?.displaySize ?? tableConfig?.display_size ?? 0),
    width: Number(obj?.width ?? tableConfig?.width ?? 0),
    height: Number(obj?.height ?? tableConfig?.height ?? 0),
    type: Number(obj?.type ?? tableConfig?.type ?? 0),
    name: obj?.name || tableConfig?.name || "unknown",
    color_type: Number(obj?.color_type ?? tableConfig?.color_type ?? colorCount),
    color_name: obj?.color_name || tableConfig?.color_name || "",
    color_count: colorCount,
    colors: obj?.colors || (colorCount > 0 ? `${colorCount} 色` : tableConfig?.colors || ""),
  };
}

function epdColorSwatches(config) {
  const colors = EPD_COLOR_TABLE[config?.color_name] || [];
  return colors.map((item) => (
    `<span class="epdColorSwatch"><span class="epdColorBox" style="background:${item.css}"></span><span>${item.label}</span></span>`
  )).join(" ");
}

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#39;",
  }[ch]));
}

function renderEpdTypeInfo(config, failed = false) {
  if (!els.epdTypeState || !els.epdTypeSummary || !els.epdTypeDetails) return;
  if (!config) {
    els.epdTypeState.textContent = "等待设备上报";
    els.epdTypeState.className = "epdStateBadge";
    els.epdTypeSummary.textContent = "当前 EPD 信息：未收到。";
    if (els.epdColorSwatches) els.epdColorSwatches.innerHTML = "";
    els.epdTypeDetails.innerHTML = "";
    return;
  }

  els.epdTypeState.textContent = failed ? "EPD 类型异常" : "EPD 类型已收到";
  els.epdTypeState.className = `epdStateBadge ${failed ? "bad" : "ok"}`;
  els.epdTypeSummary.textContent = `${config.name} / type=${config.type} / ${config.width}x${config.height} / ${config.colors || "颜色未知"} / bin=${config.display_size} bytes`;
  if (els.epdColorSwatches) {
    els.epdColorSwatches.innerHTML = epdColorSwatches(config) || `<span class="epdColorSwatch">颜色类型未知</span>`;
  }
  els.epdTypeDetails.innerHTML = [
    `<span><b>type</b><em>${config.type}</em></span>`,
    `<span><b>name</b><em>${config.name}</em></span>`,
    `<span><b>resolution</b><em>${config.width} x ${config.height}</em></span>`,
    `<span><b>display_size</b><em>${config.display_size} bytes</em></span>`,
    `<span><b>color_type</b><em>${config.color_type || "unknown"}</em></span>`,
    `<span><b>color_name</b><em>${config.color_name || "unknown"}</em></span>`,
    `<span><b>color_count</b><em>${config.color_count || "unknown"}</em></span>`,
  ].join("");
}

function renderEpdTypeList(list = epdTypeList, currentType = currentEpdType?.type) {
  if (!els.epdTypeList) return;
  const configs = (Array.isArray(list) && list.length ? list : EPD_TYPE_TABLE).map(mergeEpdConfig);
  if (!configs.length) {
    els.epdTypeList.textContent = "未收到 EPD 列表。";
    return;
  }
  els.epdTypeList.innerHTML = configs.map((config) => (
    `<label class="epdTypeRow">` +
    `<input type="radio" name="epdTypeSelect" value="${config.type}" ${Number(config.type) === Number(currentType) ? "checked" : ""}>` +
    `<span><span class="epdTypeName">${escapeHtml(config.name)}</span>` +
    `<span class="epdTypeMeta">type=${config.type} / ${config.width}x${config.height} / ${escapeHtml(config.color_name || config.colors || "unknown")} / bin=${config.display_size}</span></span>` +
    `</label>`
  )).join("");
}

function handleProtocolObject(obj) {
  if (!obj || typeof obj !== "object") return;
  if (obj.func === "epd_type_list") {
    epdTypeList = Array.isArray(obj.types) ? obj.types.map(mergeEpdConfig) : [];
    renderEpdTypeList(epdTypeList, obj.current_type ?? currentEpdType?.type);
    rxLine(`[EPD_type] list count=${epdTypeList.length} current=${obj.current_type ?? "unknown"}`);
    if (pendingEpdListRefresh) {
      showOperationToast(`EPD 列表刷新成功，count=${epdTypeList.length}`);
      pendingEpdListRefresh = false;
    }
    return;
  }
  if (obj.func === "epd_type") {
    currentEpdType = mergeEpdConfig(obj);
    renderEpdTypeInfo(currentEpdType, obj.result !== 0);
    renderEpdTypeList(epdTypeList, currentEpdType.type);
    updateFileInfo();
    return;
  }
  if (obj.func === "set_epd_type_result") {
    if (obj.result === 0) {
      currentEpdType = mergeEpdConfig(obj);
      renderEpdTypeInfo(currentEpdType, false);
      renderEpdTypeList(epdTypeList, currentEpdType.type);
      updateFileInfo();
      rxLine(`[EPD_type] set ok type=${currentEpdType.type} changed=${obj.changed ? 1 : 0}`);
      if (pendingEpdSetType !== null) {
        showOperationToast(`EPD_type 设置成功：type=${currentEpdType.type} ${currentEpdType.name}`);
        pendingEpdSetType = null;
      }
    } else {
      rxLine(`[EPD_type] set failed: ${obj.message || "unknown"}`);
      renderEpdTypeList(epdTypeList, currentEpdType?.type);
      if (pendingEpdSetType !== null) {
        showOperationToast(`EPD_type 设置失败：type=${pendingEpdSetType}，${obj.message || "unknown"}`, false);
        pendingEpdSetType = null;
      }
      showFaultPopup("EPD_type 设置失败", obj.message || "设备拒绝设置 EPD 类型");
    }
    return;
  }
  if (obj.func === "test_epd_display_result") {
    rxLine(obj.result === 0 ? `[EPD_type] test requested type=${obj.type} name=${obj.name || ""}` : `[EPD_type] test failed: ${obj.message || "unknown"}`);
    if (pendingEpdTest) {
      showOperationToast(obj.result === 0 ? `EPD 测试已执行：type=${obj.type} ${obj.name || ""}` : `EPD 测试失败：${obj.message || "unknown"}`, obj.result === 0);
      pendingEpdTest = false;
    }
  }
}

async function copyRxOutputToClipboard() {
  flushRxOutput();
  const text = rxCopyText || "";
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      throw new Error("clipboard api unavailable");
    }
  } catch (_) {
    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.style.position = "fixed";
    textarea.style.left = "-9999px";
    textarea.style.top = "0";
    document.body.appendChild(textarea);
    textarea.focus();
    textarea.select();
    document.execCommand("copy");
    document.body.removeChild(textarea);
  }
  rxLine(`[复制] 串口接收窗口内容已复制，chars=${text.length}`);
}

function nowTimeText() {
  return new Date().toLocaleTimeString();
}
function errorToText(err) {
  if (!err) return "unknown";
  if (err instanceof Error) return `${err.name || "Error"}: ${err.message || err}`;
  if (typeof err === "object") {
    try { return JSON.stringify(err, null, 2); } catch (_) {}
  }
  return String(err);
}
function showFaultPopup(title, message, err) {
  const box = $("faultPopup");
  const titleEl = $("faultPopupTitleText");
  const textEl = $("faultPopupText");
  if (!box || !textEl) return;
  const detail = err !== undefined ? errorToText(err) : "";
  const full = `[${nowTimeText()}] ${title || "失败信息"}\n${message || ""}${detail ? `\n${detail}` : ""}`.trim();
  if (titleEl) titleEl.textContent = title || "失败信息";
  textEl.textContent = textEl.textContent ? `${full}\n\n${textEl.textContent}` : full;
  box.classList.remove("faultPopupHidden");
}
function hideFaultPopup() {
  $("faultPopup")?.classList.add("faultPopupHidden");
}
function clearFaultPopup() {
  const textEl = $("faultPopupText");
  if (textEl) textEl.textContent = "";
  hideFaultPopup();
}

function isConnected() { return !!serialPort && serialPort.readable && serialPort.writable; }

function broadcastProtocolPageState(type) {
  const now = Date.now();
  try {
    if (!serialProtocolBroadcastChannel && "BroadcastChannel" in window) {
      serialProtocolBroadcastChannel = new BroadcastChannel(SERIAL_PROTOCOL_CHANNEL_NAME);
    }
    serialProtocolBroadcastChannel?.postMessage({ type, at: now });
  } catch (_) {}

  try {
    if (type === "serial_protocol_active" || type === "serial_protocol_heartbeat") {
      localStorage.setItem(SERIAL_PROTOCOL_ACTIVE_KEY, "1");
      localStorage.setItem(SERIAL_PROTOCOL_HEARTBEAT_KEY, String(now));
    } else if (type === "serial_protocol_inactive") {
      localStorage.removeItem(SERIAL_PROTOCOL_ACTIVE_KEY);
      localStorage.removeItem(SERIAL_PROTOCOL_HEARTBEAT_KEY);
    }
  } catch (_) {}
}

function startProtocolPageHeartbeat() {
  broadcastProtocolPageState("serial_protocol_active");
  if (serialProtocolHeartbeatTimer) return;
  serialProtocolHeartbeatTimer = setInterval(() => {
    broadcastProtocolPageState("serial_protocol_heartbeat");
  }, 1000);
}

function stopProtocolPageHeartbeat() {
  if (serialProtocolHeartbeatTimer) {
    clearInterval(serialProtocolHeartbeatTimer);
    serialProtocolHeartbeatTimer = null;
  }
  broadcastProtocolPageState("serial_protocol_inactive");
}

function getSerialOptions() {
  const baudRate = parseInt(els.baudrate.value || "921600", 10);
  const dataBits = parseInt(els.dataBits.value || "8", 10);
  const stopBits = parseInt(els.stopBits.value || "1", 10);
  const bufferSize = Math.max(1, Math.min(16777216, parseInt(els.bufferSize.value || String(SERIAL_DEFAULT_BUFFER_SIZE), 10) || SERIAL_DEFAULT_BUFFER_SIZE));
  return {
    baudRate: Number.isFinite(baudRate) ? baudRate : 921600,
    dataBits: dataBits === 7 ? 7 : 8,
    stopBits: stopBits === 2 ? 2 : 1,
    parity: ["none", "even", "odd"].includes(els.parity.value) ? els.parity.value : "none",
    flowControl: els.flowControl.value === "hardware" ? "hardware" : "none",
    bufferSize,
  };
}

function setUiConnected(connected) {
  els.connectButton.disabled = connected;
  els.resetButton.disabled = !connected;
  els.disconnectButton.disabled = !connected;
  els.sendRawButton.disabled = !connected;
  els.sendRawJsonButton.disabled = !connected;
  document.querySelectorAll("button.httpGetCmd, button.httpJsonCmd").forEach((btn) => { btn.disabled = !connected; });
  [els.sendWifiButton, els.sendStartSlideshowButton, els.sendSetSlideshowButton, els.sendDeleteButton, els.sendWifiWorkTimeButton, els.sendDirectFileButton, els.sendSingleMultipartButton, els.sendCast2picButton, els.refreshEpdTypesButton, els.testEpdDisplayButton].forEach((btn) => { if (btn) btn.disabled = !connected; });
}

function describePort(port) {
  try {
    const info = port.getInfo ? port.getInfo() : {};
    const vid = info.usbVendorId !== undefined ? `VID=0x${info.usbVendorId.toString(16).padStart(4,"0")}` : "VID=?";
    const pid = info.usbProductId !== undefined ? `PID=0x${info.usbProductId.toString(16).padStart(4,"0")}` : "PID=?";
    const match = AUTO_CONSOLE_PORT_MATCHES.find((item) => item.usbVendorId === info.usbVendorId && item.usbProductId === info.usbProductId);
    return `${match?.label || "Serial Port"} (${vid}, ${pid})`;
  } catch (_) { return "Serial Port"; }
}

function isAutoTargetPort(port) {
  if (!port?.getInfo) return false;
  try {
    const info = port.getInfo();
    return AUTO_CONSOLE_PORT_MATCHES.some((item) => item.usbVendorId === info.usbVendorId && item.usbProductId === info.usbProductId);
  } catch (_) { return false; }
}

async function closeSerialHardware(reason = "close") {
  const port = serialPort;
  const reader = serialReader;
  serialReader = null;

  if (reader) {
    try { await withTimeout(reader.cancel(), 1000, "取消串口 reader 超时"); } catch (_) {}
    try { reader.releaseLock(); } catch (_) {}
  }

  if (port) {
    try { await withTimeout(port.close(), 1500, "关闭串口超时"); } catch (_) {}
  }

  serialPort = null;
  reading = false;
  setUiConnected(false);
}

async function disconnectOnSerialFault(reason, err) {
  manualDisconnect = true;
  reconnectBusy = false;
  const detail = errorToText(err);
  rxLine(`\n[串口故障] ${reason}${detail ? `：${detail}` : ""}`);
  rxLine("[处理] 已强制断开串口，停止本次自动重连，避免页面卡住。请重新点击“连接串口”。\n");
  showFaultPopup("串口故障，已断开", `${reason}\n已强制断开串口，停止本次自动重连，避免页面卡住。`, err);
  setStatus(`串口故障，已断开：${reason}`);
  await closeSerialHardware(reason);
}


async function connectFromUser() {
  if (!navigator.serial) throw new Error("当前浏览器不支持 Web Serial，请使用 Chrome / Edge，并通过 http://localhost 打开页面。");
  manualDisconnect = false;
  const port = await navigator.serial.requestPort({ filters: USB_PORT_FILTERS });
  await openSerialPort(port, false);
}

async function openSerialPort(port, auto) {
  if (isConnected()) return;
  serialPort = port;
  rxLine(`${auto ? "自动连接" : "连接"}串口：${describePort(port)}`);
  try {
    await withTimeout(serialPort.open(getSerialOptions()), getMsSetting("serialOpenTimeoutMs", 8000, 1000, 30000), "串口打开超时");
  } catch (err) {
    showFaultPopup("串口打开失败", `${auto ? "自动连接" : "手动连接"} ${describePort(port)} 失败。`, err);
    try { await withTimeout(port.close(), 1000, "关闭失败串口超时"); } catch (_) {}
    serialPort = null;
    setUiConnected(false);
    setStatus("串口打开失败，已断开");
    throw err;
  }
  setUiConnected(true);
  setStatus(`已连接 ${describePort(port)}，baudrate=${els.baudrate.value}`);
  startReadLoop();
  setTimeout(async () => {
    try {
      await sendGetRequest("/epd_type_list");
      await delay(50);
      await sendGetRequest("/epd_type");
    } catch (err) {
      rxLine(`[EPD_type] 查询失败：${errorToText(err)}`);
    }
  }, 150);
}


async function startReadLoop() {
  if (!serialPort?.readable || reading) return;
  reading = true;
  let readFault = null;
  try {
    while (serialPort && serialPort.readable) {
      serialReader = serialPort.readable.getReader();
      try {
        while (true) {
          const { value, done } = await serialReader.read();
          if (done) break;
          handleIncomingBytes(value);
        }
      } finally {
        try { serialReader.releaseLock(); } catch (_) {}
        serialReader = null;
      }
    }
  } catch (err) {
    readFault = err;
    rxLine(`\n[串口接收错误] ${errorToText(err)}`);
  } finally {
    reading = false;
    if (readFault) {
      manualDisconnect = true;
      reconnectBusy = false;
      showFaultPopup("串口接收失败，已断开", "接收数据过程中发生错误。页面已释放串口，避免卡住不动。", readFault);
      await closeSerialHardware("read fault");
      setStatus("串口接收失败，已断开");
      return;
    }
    const shouldReconnect = !manualDisconnect;
    serialPort = null;
    setUiConnected(false);
    setStatus(shouldReconnect ? "串口断开，等待自动重连" : "已断开");
    if (shouldReconnect) scheduleReconnect();
  }
}


function handleIncomingBytes(value) {
  const now = Date.now();
  if (now - rxFloodWindowStart > RX_FLOOD_WINDOW_MS) {
    rxFloodWindowStart = now;
    rxFloodBytes = 0;
  }
  rxFloodBytes += value?.byteLength || 0;
  if (rxFloodBytes > RX_FLOOD_MAX_BYTES) {
    // Treat extreme RX flood as a serial fault. It often means the device rebooted into noisy logs
    // or the parser is wrong; disconnecting keeps the page responsive.
    // 串口数据洪泛按故障处理，避免大量日志把浏览器主线程卡死。
    throw new Error(`串口接收数据过快：${rxFloodBytes} bytes / ${RX_FLOOD_WINDOW_MS} ms`);
  }

  const text = textDecoder.decode(value, { stream: true });
  if (!els.prettyRxJson.checked) {
    appendRx(text);
    return;
  }
  rxTextBuffer += text;
  if (rxTextBuffer.length > RX_PARSE_BUFFER_MAX_CHARS) {
    appendRx(`[接收解析缓存超过 ${RX_PARSE_BUFFER_MAX_CHARS} 字符，改为原样刷新并清空缓存，防止页面无响应。]
`);
    appendRx(rxTextBuffer.slice(-RX_PARSE_BUFFER_MAX_CHARS));
    rxTextBuffer = "";
    return;
  }
  formatIncomingTextBuffer(false);
}

function tryFormatJsonText(text) {
  const trimmed = String(text || "").trim();
  if (!trimmed) return text;
  try {
    const obj = JSON.parse(trimmed);
    handleProtocolObject(obj);
    return JSON.stringify(obj, null, 2);
  } catch (_) {
    return text;
  }
}

function formatHttpLikeMessage(raw) {
  const headerEnd = raw.indexOf("\r\n\r\n");
  if (headerEnd < 0) return tryFormatJsonText(raw);
  const headers = raw.slice(0, headerEnd);
  const body = raw.slice(headerEnd + 4);
  try {
    handleProtocolObject(JSON.parse(String(body || "").trim()));
  } catch (_) {}
  const bodyOut = tryFormatJsonText(body);
  return `${headers}\r\n\r\n${bodyOut}`;
}

function formatIncomingTextBuffer(forceFlush) {
  const frameHead = "@#$\r\n";
  const frameTail = "\r\n%^&";
  while (true) {
    const frameStart = rxTextBuffer.indexOf(frameHead);
    if (frameStart < 0) break;
    if (frameStart > 0) {
      appendRx(rxTextBuffer.slice(0, frameStart));
      rxTextBuffer = rxTextBuffer.slice(frameStart);
    }
    const frameEnd = rxTextBuffer.indexOf(frameTail, frameHead.length);
    if (frameEnd < 0 && !forceFlush) return;
    if (frameEnd < 0) break;
    const rawFrame = rxTextBuffer.slice(frameHead.length, frameEnd);
    appendRx(`${frameHead}${formatHttpLikeMessage(rawFrame)}${frameTail}\n`);
    rxTextBuffer = rxTextBuffer.slice(frameEnd + frameTail.length);
  }

  // Try to format complete HTTP-like responses by Content-Length.
  // 尝试按 Content-Length 格式化完整 HTTP-like 响应。
  while (true) {
    const headerEnd = rxTextBuffer.indexOf("\r\n\r\n");
    if (headerEnd >= 0 && /^(HTTP\/|[A-Z]+\s+\/)/.test(rxTextBuffer)) {
      const headerText = rxTextBuffer.slice(0, headerEnd);
      const lenMatch = headerText.match(/\r?\nContent-Length\s*:\s*(\d+)/i);
      const contentLength = lenMatch ? parseInt(lenMatch[1], 10) : 0;
      const totalLength = headerEnd + 4 + (Number.isFinite(contentLength) ? contentLength : 0);
      if (rxTextBuffer.length < totalLength && !forceFlush) return;
      const msg = rxTextBuffer.slice(0, Math.min(totalLength, rxTextBuffer.length));
      appendRx(formatHttpLikeMessage(msg) + "\n");
      rxTextBuffer = rxTextBuffer.slice(Math.min(totalLength, rxTextBuffer.length));
      continue;
    }
    break;
  }

  // Fallback: format complete lines.
  // 兜底：按行格式化普通日志/JSON。
  const parts = rxTextBuffer.split(/\r?\n/);
  rxTextBuffer = parts.pop() || "";
  for (const line of parts) {
    const trimmed = line.trim();
    if (!trimmed) { appendRx("\n"); continue; }
    appendRx(tryFormatJsonText(trimmed) + "\n");
  }
  if (forceFlush && rxTextBuffer) {
    appendRx(tryFormatJsonText(rxTextBuffer));
    rxTextBuffer = "";
  }
}

function scheduleReconnect() {
  if (reconnectBusy) return;
  reconnectBusy = true;
  setTimeout(async () => {
    reconnectBusy = false;
    if (manualDisconnect || isConnected()) return;
    await autoConnectKnownPort();
  }, 1000);
}

async function autoConnectKnownPort() {
  if (!els.autoConnect.checked || !navigator.serial || isConnected()) return;
  try {
    const ports = await navigator.serial.getPorts();
    const port = ports.find(isAutoTargetPort) || ports[0];
    if (!port) return;
    await openSerialPort(port, true);
  } catch (err) {
    setStatus(`自动连接失败：${err?.message || err}`);
  }
}

async function releaseSerialHardwareInBackground(port, reader, reason = "manual disconnect") {
  // Do not let Web Serial cleanup block the UI. Some devices stop responding after reboot,
  // and reader.cancel()/port.close() can stay pending for a long time in Chrome.
  // 不让 Web Serial 清理流程阻塞页面。设备重启/异常后，cancel/close 可能长时间 pending。
  try {
    if (reader) {
      const r = await settleWithin(reader.cancel(), 300, "reader.cancel");
      if (!r.ok) {
        rxLine(`[后台断开] ${r.label} 未及时完成，继续释放页面控制权。`);
      }
      try { reader.releaseLock(); } catch (_) {}
    }

    if (port) {
      const r = await settleWithin(port.close(), 700, "port.close");
      if (!r.ok) {
        rxLine(`[后台断开] ${r.label} 未及时完成，可拔插设备或刷新页面后重新连接。`);
        showFaultPopup("串口后台断开未完全完成", "页面已经恢复控制；浏览器底层串口关闭超时。可拔插设备或刷新页面后重新连接。", r.err || reason);
      } else {
        rxLine("[后台断开] 串口底层关闭完成。");
      }
    }
  } catch (err) {
    rxLine(`[后台断开] ${errorToText(err)}`);
    showFaultPopup("串口后台断开异常", "页面已经恢复控制；后台释放串口时出现异常。", err);
  }
}

async function disconnectSerial() {
  manualDisconnect = true;
  reconnectBusy = false;

  const port = serialPort;
  const reader = serialReader;

  // Detach page state immediately so the click handler returns at once.
  // 先立即解除页面状态，不等待浏览器底层 close，避免点击“断开”后页面卡住。
  serialPort = null;
  serialReader = null;
  reading = false;
  setUiConnected(false);
  setStatus("已断开，后台释放串口中");
  rxLine("\n[手动断开] 页面已立即释放控制权，正在后台关闭串口...\n");

  setTimeout(() => {
    releaseSerialHardwareInBackground(port, reader, "手动断开").finally(() => {
      setStatus("已断开");
    });
  }, 0);
}

async function resetDevice() {
  if (!serialPort?.setSignals) throw new Error("当前浏览器/串口不支持 setSignals，无法 Reset Device。");
  rxLine("\n[Reset Device]\n");
  try {
    const timeoutMs = getMsSetting("serialResetTimeoutMs", 3000, 500, 10000);
    await withTimeout(serialPort.setSignals({ dataTerminalReady: false, requestToSend: true }), timeoutMs, "Reset Device 拉低 RTS 超时");
    await delay(100);
    await withTimeout(serialPort.setSignals({ requestToSend: false }), timeoutMs, "Reset Device 释放 RTS 超时");
    await delay(100);
  } catch (err) {
    await disconnectOnSerialFault("Reset Device 失败", err);
    throw err;
  }
}


function stringToBytes(text) { return textEncoder.encode(text); }
function jsonToBytes(obj) { return stringToBytes(JSON.stringify(obj)); }

function concatBytes(parts) {
  let total = 0;
  for (const part of parts) total += part.byteLength;
  const out = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    out.set(part instanceof Uint8Array ? part : new Uint8Array(part), offset);
    offset += part.byteLength;
  }
  return out;
}

function normalizeHeaderName(name) {
  return String(name || "").replace(/(^|-)([a-z])/g, (m) => m.toUpperCase());
}

function buildHttpLikeRequestBytes({ method, path, headers = {}, bodyBytes = new Uint8Array(0) }) {
  const finalHeaders = {
    Host: "usb",
    ...headers,
    "Content-Length": String(bodyBytes.byteLength),
  };
  let headerText = `${method} ${path} HTTP/1.1\r\n`;
  for (const [name, value] of Object.entries(finalHeaders)) {
    if (value === undefined || value === null) continue;
    headerText += `${normalizeHeaderName(name)}: ${value}\r\n`;
  }
  headerText += "\r\n";
  return concatBytes([stringToBytes(headerText), bodyBytes]);
}

function getWriteChunkSize() {
  const n = parseInt(els.writeChunkSize?.value || String(SERIAL_DEFAULT_WRITE_CHUNK_SIZE), 10);
  return Number.isFinite(n) ? Math.max(256, Math.min(65536, n)) : SERIAL_DEFAULT_WRITE_CHUNK_SIZE;
}

function getWriteChunkDelayMs() {
  const n = parseInt(els.writeChunkDelayMs?.value || "0", 10);
  return Number.isFinite(n) ? Math.max(0, Math.min(1000, n)) : 0;
}

async function writeBytesToSerial(bytes) {
  if (!serialPort?.writable) throw new Error("串口未连接");
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const chunkSize = getWriteChunkSize();
  const chunkDelayMs = getWriteChunkDelayMs();
  const writeTimeoutMs = getMsSetting("writeChunkTimeoutMs", SERIAL_DEFAULT_WRITE_CHUNK_TIMEOUT_MS, 500, 30000);
  const writer = serialPort.writable.getWriter();
  let lastPercent = -1;
  const startMs = performance.now();
  try {
    for (let offset = 0; offset < data.byteLength; offset += chunkSize) {
      const end = Math.min(offset + chunkSize, data.byteLength);
      await withTimeout(writer.write(data.subarray(offset, end)), writeTimeoutMs, `串口写入超时 offset=${offset} end=${end}`);
      const percent = data.byteLength ? Math.floor((end * 100) / data.byteLength) : 100;
      if (percent === 100 || percent >= lastPercent + 10) {
        const elapsedMs = Math.max(1, Math.round(performance.now() - startMs));
        const rateKb = Math.round((end * 1000) / elapsedMs / 1024);
        appendRx(`[TX progress] ${end}/${data.byteLength} bytes ${percent}% elapsed_ms=${elapsedMs} rate=${rateKb}KB/s\n`);
        lastPercent = percent;
      }
      if (chunkDelayMs > 0 && end < data.byteLength) await delay(chunkDelayMs);
      else if ((offset / chunkSize) % 16 === 0) await yieldToUi();
    }
  } catch (err) {
    try { await withTimeout(writer.abort?.() || Promise.resolve(), 1000, "writer.abort 超时"); } catch (_) {}
    showFaultPopup("串口发送失败", "发送过程中发生错误/超时，已断开串口，避免页面卡住。", err);
    throw err;
  } finally {
    try { writer.releaseLock(); } catch (_) {}
  }
}


function previewBytes(bytes, maxText = 4096) {
  const n = Math.min(bytes.byteLength, maxText);
  const text = textDecoder.decode(bytes.slice(0, n));
  return bytes.byteLength > n ? `${text}\n... <${bytes.byteLength - n} bytes more>` : text;
}

function findAsciiBytes(bytes, ascii, start = 0) {
  const needle = stringToBytes(ascii);
  if (!bytes || needle.byteLength === 0 || bytes.byteLength < needle.byteLength) return -1;
  for (let i = Math.max(0, start); i <= bytes.byteLength - needle.byteLength; i++) {
    let ok = true;
    for (let j = 0; j < needle.byteLength; j++) {
      if (bytes[i + j] !== needle[j]) {
        ok = false;
        break;
      }
    }
    if (ok) return i;
  }
  return -1;
}

function previewHttpLikeBytes(bytes, maxText = 8192) {
  const headerEnd = findAsciiBytes(bytes, "\r\n\r\n");
  if (headerEnd < 0) return previewBytes(bytes, maxText);

  const headerText = textDecoder.decode(bytes.slice(0, headerEnd + 4));
  if (!/multipart\/form-data/i.test(headerText)) {
    return previewBytes(bytes, maxText);
  }

  const binaryMarkers = [
    "Content-Type: application/octet-stream\r\n\r\n",
    "Content-Type: image/jpeg\r\n\r\n",
    "Content-Type: image/jpg\r\n\r\n",
    "Content-Type: application/bin\r\n\r\n",
  ];
  let binaryStart = -1;
  for (const marker of binaryMarkers) {
    const pos = findAsciiBytes(bytes, marker, headerEnd + 4);
    if (pos >= 0) {
      const dataStart = pos + stringToBytes(marker).byteLength;
      if (binaryStart < 0 || dataStart < binaryStart) binaryStart = dataStart;
    }
  }
  if (binaryStart < 0) return previewBytes(bytes, maxText);

  const safeEnd = Math.min(binaryStart, maxText);
  const text = textDecoder.decode(bytes.slice(0, safeEnd));
  return `${text}\n... <multipart binary omitted, total ${bytes.byteLength} bytes>`;
}

async function sendHttpLikeRequest(opts) {
  const startMs = performance.now();
  const bytes = buildHttpLikeRequestBytes(opts);
  const label = opts.label || `${opts.method} ${opts.path}`;
  appendRx(`\n[TX HTTP-like] ${label}, ${bytes.byteLength} bytes\n`);
  appendRx(previewHttpLikeBytes(bytes, opts.bodyBytes?.byteLength > 8192 ? 2048 : 8192) + "\n");
  try {
    await writeBytesToSerial(bytes);
    const elapsedMs = Math.max(1, Math.round(performance.now() - startMs));
    const rateKb = Math.round((bytes.byteLength * 1000) / elapsedMs / 1024);
    appendRx(`[TX complete] ${bytes.byteLength}/${bytes.byteLength} bytes elapsed_ms=${elapsedMs} rate=${rateKb}KB/s\n`);
  } catch (err) {
    await disconnectOnSerialFault("串口发送 HTTP-like 请求失败", err);
    throw err;
  }
}


function routeForJson(func, fallbackPath) {
  if (els.jsonRouteMode.value === "dataup") return "/dataUP";
  const map = {
    ping: "/ping",
    get_saved_images: "/saved_images",
    get_snapshot: "/snapshot",
    wifi: "/wifi",
    wifi_wakeup: "/wifi_wakeup",
    start_slideshow: "/slideshow",
    set_slideshow: "/slideshow_control",
    delete: "/delete",
    set_wifi_work_time: "/wifi_work_time",
    set_epd_type: "/epd_type",
    test_epd_display: "/epd_test",
  };
  return fallbackPath || map[func] || `/${func || "dataUP"}`;
}

async function sendJsonRequest(obj, fallbackPath) {
  const func = obj?.func || "unknown";
  if (func === "ping" && els.jsonRouteMode.value !== "dataup") {
    return sendGetRequest("/ping");
  }
  const path = routeForJson(func, fallbackPath);
  const bodyBytes = jsonToBytes(obj);
  await sendHttpLikeRequest({
    method: "POST",
    path,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-USB-Function": func,
    },
    bodyBytes,
    label: `POST ${path} JSON func=${func}`,
  });
}

async function sendSetEpdType(type) {
  const epdType = Number(type);
  if (!Number.isInteger(epdType) || epdType < 0 || epdType > 255) {
    throw new Error(`invalid EPD type=${type}`);
  }
  const bodyBytes = jsonToBytes({ func: "set_epd_type", type: epdType });
  await sendHttpLikeRequest({
    method: "POST",
    path: "/epd_type",
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-USB-Function": "set_epd_type",
    },
    bodyBytes,
    label: `POST /epd_type JSON type=${epdType}`,
  });
}

async function sendTestEpdDisplay() {
  const bodyBytes = jsonToBytes({ func: "test_epd_display" });
  await sendHttpLikeRequest({
    method: "POST",
    path: "/epd_test",
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-USB-Function": "test_epd_display",
    },
    bodyBytes,
    label: "POST /epd_test JSON func=test_epd_display",
  });
}

function buildWifiBodyObject() {
  return {
    func: "wifi",
    ssid: els.wifiSsid?.value || "",
    key: els.wifiPassword?.value || "",
  };
}

function buildWifiHttpText() {
  const bodyText = JSON.stringify(buildWifiBodyObject());
  const bodyLen = textEncoder.encode(bodyText).byteLength;
  return [
    "POST /wifi HTTP/1.1",
    "Host: usb",
    "Content-Type: application/json; charset=utf-8",
    "X-USB-Function: wifi",
    `Content-Length: ${bodyLen}`,
    "",
    bodyText,
  ].join("\r\n");
}

function updateWifiRequestPreview() {
  if (!els.wifiRequestPreview) return;
  const text = buildWifiHttpText();
  // Display with real line breaks for copy and visual check.
  // 使用真实换行显示，便于人工复制和检查。
  els.wifiRequestPreview.value = text;
}

function copyWifiRequestToRawArea() {
  updateWifiRequestPreview();
  if (els.rawText && els.wifiRequestPreview) {
    els.rawText.value = els.wifiRequestPreview.value;
    showOperationToast("复制 WiFi 请求到原始发送区已执行");
    rxLine("已把 WiFi POST /wifi 示例复制到原始发送区，Content-Length 已按当前 SSID/password 自动计算。 ");
  }
}

async function sendWifiRequest() {
  updateWifiRequestPreview();
  // Use the same generated JSON object and HTTP-like headers shown in the preview.
  // 发送内容与预览一致，Content-Length 由 UTF-8 字节数计算，不手填固定值。
  await sendJsonRequest(buildWifiBodyObject(), "/wifi");
}

async function sendGetRequest(path) {
  await sendHttpLikeRequest({
    method: "GET",
    path,
    headers: {},
    bodyBytes: new Uint8Array(0),
    label: `GET ${path}`,
  });
}

async function sendRawText() {
  const text = els.rawText.value || "";
  appendRx(`\n[TX RAW HTTP-like] ${text.length} chars\n${text}\n`);
  try {
    await writeBytesToSerial(stringToBytes(text));
  } catch (err) {
    await disconnectOnSerialFault("串口发送原始文本失败", err);
    throw err;
  }
}


async function sendRawJsonAsDataUp() {
  const obj = JSON.parse(els.rawText.value || "{}");
  const bodyBytes = jsonToBytes(obj);
  await sendHttpLikeRequest({
    method: "POST",
    path: "/dataUP",
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-USB-Function": obj.func || "json",
    },
    bodyBytes,
    label: `POST /dataUP JSON func=${obj.func || "json"}`,
  });
}

function parseList(text) {
  return String(text || "").split(/[\s,，]+/).map((s) => s.trim()).filter(Boolean);
}

function getNumber(id, fallback, min=1) {
  const n = parseInt($(id).value || String(fallback), 10);
  return Number.isFinite(n) && n >= min ? n : fallback;
}

async function fileToBytes(file) {
  return new Uint8Array(await file.arrayBuffer());
}

function escapeHeaderValue(value) {
  return String(value || "").replace(/["\\\r\n]/g, "_");
}

function multipartTextField(boundary, name, value) {
  return stringToBytes(`--${boundary}\r\nContent-Disposition: form-data; name="${escapeHeaderValue(name)}"\r\n\r\n${String(value)}\r\n`);
}

function multipartFileHeader(boundary, field, filename, contentType = "application/octet-stream") {
  return stringToBytes(`--${boundary}\r\nContent-Disposition: form-data; name="${escapeHeaderValue(field)}"; filename="${escapeHeaderValue(filename)}"\r\nContent-Type: ${contentType}\r\n\r\n`);
}

async function multipartFilePart(boundary, field, file) {
  const type = file.type || (field === "image" ? "image/jpeg" : "application/octet-stream");
  const data = await fileToBytes(file);
  return [multipartFileHeader(boundary, field, file.name, type), data, stringToBytes("\r\n")];
}

async function buildMultipartBody(fields, files) {
  const boundary = `----TDXUSB${Date.now().toString(16)}${Math.random().toString(16).slice(2)}`;
  const parts = [];
  for (const [name, value] of fields) {
    parts.push(multipartTextField(boundary, name, value));
  }
  for (const item of files) {
    parts.push(...await multipartFilePart(boundary, item.field, item.file));
  }
  parts.push(stringToBytes(`--${boundary}--\r\n`));
  return { boundary, bodyBytes: concatBytes(parts) };
}

function fileRouteForFunc(func) {
  if (els.fileRouteMode.value === "dataup") return "/dataUP";
  return `/${func}`;
}

function sanitizeHttpPath(path) {
  const value = String(path || "").trim();
  if (!value) return "/upload?path=/data/test.bin";
  return value.startsWith("/") ? value : `/${value}`;
}

async function sendDirectFile() {
  const file = els.directFileInput.files?.[0];
  if (!file) throw new Error("请选择要发送的文件");
  const path = sanitizeHttpPath(els.directFilePath.value);
  const contentType = (els.directFileContentType.value || file.type || "application/octet-stream").trim();
  const func = (els.directFileFunc.value || "upload_raw").trim();
  const bodyBytes = await fileToBytes(file);
  await sendHttpLikeRequest({
    method: "POST",
    path,
    headers: {
      "Content-Type": contentType,
      "X-USB-Function": func,
      "X-File-Name": file.name,
      "X-File-Size": String(file.size),
    },
    bodyBytes,
    label: `POST ${path} raw file=${file.name}`,
  });
}

async function sendMultipartHttpLike({ targetFunc, fields, files }) {
  const { boundary, bodyBytes } = await buildMultipartBody(fields, files);
  const path = fileRouteForFunc(targetFunc);
  await sendHttpLikeRequest({
    method: "POST",
    path,
    headers: {
      "Content-Type": `multipart/form-data; boundary=${boundary}`,
      "X-USB-Function": targetFunc,
      "X-Original-URI": "/dataUP",
    },
    bodyBytes,
    label: `POST ${path} multipart func=${targetFunc}`,
  });
}

async function sendSingleMultipart() {
  const targetFunc = els.singleFunc.value;
  const fileName = els.singleFileName.value.trim();
  const oldFileName = els.singleOldFileName.value.trim();
  const bin = els.singleBin.files?.[0];
  const image = els.singleImage.files?.[0];
  if (!fileName) throw new Error("请输入 fileName");
  if (!bin || !image) throw new Error("请选择 bin 和 image 文件");

  const fields = [
    ["func", targetFunc],
    ["fileName", fileName],
    ["bin_size", String(bin.size)],
    ["image_size", String(image.size)],
    ["save", String(!!els.singleSave.checked)],
    ["show", String(!!els.singleShow.checked)],
  ];
  if (targetFunc === "update") {
    fields.push(["oldfileNames", oldFileName]);
    fields.push(["newfileNames", fileName]);
  }
  const files = [
    { field: "bin", file: bin },
    { field: "image", file: image },
  ];
  await sendMultipartHttpLike({ targetFunc, fields, files });
}

async function sendCast2pic() {
  const screen = els.cast2picScreen.value;
  const pics = [
    { index: 0, suffix: "A", fileName: els.pic1Name.value.trim(), bin: els.pic1Bin.files?.[0], image: els.pic1Image.files?.[0] },
    { index: 1, suffix: "B", fileName: els.pic2Name.value.trim(), bin: els.pic2Bin.files?.[0], image: els.pic2Image.files?.[0] },
  ];
  const selectedPics = screen === "a" ? [pics[0]] : screen === "b" ? [pics[1]] : pics;
  for (const pic of selectedPics) {
    if (!pic.fileName) throw new Error(`第 ${pic.index + 1} 张缺少 fileName`);
    if (!pic.bin || !pic.image) throw new Error(`第 ${pic.index + 1} 张缺少 bin 或 image`);
  }
  const fields = [
    ["func", "cast2pic"],
    ["screen", screen],
    ["save", String(!!els.cast2picSave.checked)],
    ["show", String(!!els.cast2picShow.checked)],
  ];
  for (const pic of selectedPics) {
    fields.push([`fileName${pic.suffix}`, pic.fileName]);
    fields.push([`bin_size${pic.suffix}`, String(pic.bin.size)]);
    fields.push([`image_size${pic.suffix}`, String(pic.image.size)]);
  }
  const files = [];
  for (const pic of selectedPics) {
    files.push({ field: `bin${pic.suffix}`, file: pic.bin });
    files.push({ field: `image${pic.suffix}`, file: pic.image });
  }
  await sendMultipartHttpLike({ targetFunc: "cast2pic", fields, files });
}

function formatFile(file) {
  return file ? `${file.name}  ${file.size} bytes` : "未选择";
}

function binMatchText(file) {
  if (!file) return { text: "未选择", ok: null };
  if (!currentEpdType?.display_size) {
    return { text: `等待 EPD_type，上报后校验；当前文件 ${file.size} bytes`, ok: null };
  }
  if (file.size === currentEpdType.display_size) {
    return { text: `符合 ${currentEpdType.name}: ${file.size}/${currentEpdType.display_size} bytes`, ok: true };
  }
  return { text: `不符合 ${currentEpdType.name}: ${file.size}/${currentEpdType.display_size} bytes`, ok: false };
}

function setBinMatchStatus(el, file) {
  if (!el) return;
  const result = binMatchText(file);
  el.textContent = result.text;
  el.className = `fileMatchStatus ${result.ok === true ? "ok" : result.ok === false ? "bad" : ""}`;
}

function updateFileInfo() {
  if (!els.fileInfoOutput) return;
  setBinMatchStatus(els.singleBinMatch, els.singleBin?.files?.[0]);
  setBinMatchStatus(els.pic1BinMatch, els.pic1Bin?.files?.[0]);
  setBinMatchStatus(els.pic2BinMatch, els.pic2Bin?.files?.[0]);
  const directMatch = binMatchText(els.directFileInput?.files?.[0]);
  const singleMatch = binMatchText(els.singleBin?.files?.[0]);
  const pic1Match = binMatchText(els.pic1Bin?.files?.[0]);
  const pic2Match = binMatchText(els.pic2Bin?.files?.[0]);
  const lines = [
    `当前 EPD: ${currentEpdType ? `${currentEpdType.name} type=${currentEpdType.type} ${currentEpdType.width}x${currentEpdType.height} expected=${currentEpdType.display_size} bytes` : "未收到"}`,
    `直接文件: ${formatFile(els.directFileInput?.files?.[0])}`,
    `直接文件校验: ${directMatch.text}`,
    `单图 bin: ${formatFile(els.singleBin?.files?.[0])}`,
    `单图 bin 校验: ${singleMatch.text}`,
    `单图 image: ${formatFile(els.singleImage?.files?.[0])}`,
    `cast2pic 第1张 bin: ${formatFile(els.pic1Bin?.files?.[0])}`,
    `cast2pic 第1张 bin 校验: ${pic1Match.text}`,
    `cast2pic 第1张 image: ${formatFile(els.pic1Image?.files?.[0])}`,
    `cast2pic 第2张 bin: ${formatFile(els.pic2Bin?.files?.[0])}`,
    `cast2pic 第2张 bin 校验: ${pic2Match.text}`,
    `cast2pic 第2张 image: ${formatFile(els.pic2Image?.files?.[0])}`,
  ];
  els.fileInfoOutput.textContent = lines.join("\n");
}

async function runAction(fn, button, label, options = {}) {
  if (button) button.disabled = true;
  try {
    await fn();
    if (options.toast !== false) {
      const actionLabel = label || button?.textContent?.trim() || "操作";
      showOperationToast(`${actionLabel} 已执行`);
    }
  }
  catch (err) {
    const actionLabel = label || button?.textContent?.trim() || "操作";
    showOperationToast(`${actionLabel} 失败：${err?.message || err}`, false);
    rxLine(`\n[错误] ${errorToText(err)}\n`);
    showFaultPopup("操作失败", "当前操作失败。详细信息如下：", err);
  }
  finally { if (button) button.disabled = !isConnected(); }
}


function bindEvents() {
  $("faultPopupCloseButton")?.addEventListener("click", hideFaultPopup);
  $("faultPopupClearButton")?.addEventListener("click", clearFaultPopup);
  els.connectButton.addEventListener("click", () => runAction(connectFromUser, els.connectButton));
  els.disconnectButton.addEventListener("click", () => {
    // Disconnect must never be allowed to hang behind runAction.
    // “断开”不能被 runAction 的 await 卡住，必须立即返回。
    try {
      disconnectSerial();
      showOperationToast("断开串口已执行");
    }
    catch (err) {
      showOperationToast(`断开串口失败：${err?.message || err}`, false);
      showFaultPopup("断开串口失败", "执行断开时出现异常。", err);
    }
  });
  els.resetButton.addEventListener("click", () => runAction(resetDevice, els.resetButton));
  els.clearRxButton.addEventListener("click", () => {
    if (rxFlushTimer) {
      clearTimeout(rxFlushTimer);
      rxFlushTimer = null;
    }
    rxPendingText = "";
    rxCopyText = "";
    rxTextBuffer = "";
    rxOutput.textContent = "";
    rxLine("串口接收窗口已清空。");
    showOperationToast("清空接收窗口已执行");
  });
  els.copyRxButton?.addEventListener("click", () => runAction(copyRxOutputToClipboard, els.copyRxButton));
  els.sendRawButton.addEventListener("click", () => runAction(sendRawText, els.sendRawButton));
  els.sendRawJsonButton.addEventListener("click", () => runAction(sendRawJsonAsDataUp, els.sendRawJsonButton));

  document.querySelectorAll("button.httpGetCmd").forEach((btn) => {
    btn.addEventListener("click", () => runAction(() => sendGetRequest(btn.dataset.getPath), btn));
  });
  document.querySelectorAll("button.httpJsonCmd").forEach((btn) => {
    btn.addEventListener("click", () => runAction(() => {
      const obj = JSON.parse(btn.dataset.json || "{}");
      if (btn.dataset.getPath && els.jsonRouteMode.value !== "dataup") return sendGetRequest(btn.dataset.getPath);
      return sendJsonRequest(obj, btn.dataset.path);
    }, btn));
  });

  els.refreshEpdTypesButton?.addEventListener("click", () => runAction(async () => {
    pendingEpdListRefresh = true;
    try {
      await sendGetRequest("/epd_type_list");
      await delay(50);
      await sendGetRequest("/epd_type");
    } catch (err) {
      pendingEpdListRefresh = false;
      throw err;
    }
  }, els.refreshEpdTypesButton, "刷新 EPD 列表", { toast: false }));
  els.testEpdDisplayButton?.addEventListener("click", () => runAction(async () => {
    pendingEpdTest = true;
    try {
      await sendTestEpdDisplay();
    } catch (err) {
      pendingEpdTest = false;
      throw err;
    }
  }, els.testEpdDisplayButton, "测试当前 EPD", { toast: false }));
  els.epdTypeList?.addEventListener("change", (event) => {
    const target = event.target;
    if (!target || target.name !== "epdTypeSelect") return;
    pendingEpdSetType = target.value;
    runAction(async () => {
      try {
        await sendSetEpdType(target.value);
      } catch (err) {
        pendingEpdSetType = null;
        renderEpdTypeList(epdTypeList, currentEpdType?.type);
        throw err;
      }
    }, els.refreshEpdTypesButton, `选择 EPD_type ${target.value}`, { toast: false });
  });

  els.sendWifiButton.addEventListener("click", () => runAction(sendWifiRequest, els.sendWifiButton));
  els.copyWifiRawButton?.addEventListener("click", copyWifiRequestToRawArea);
  els.wifiSsid?.addEventListener("input", updateWifiRequestPreview);
  els.wifiPassword?.addEventListener("input", updateWifiRequestPreview);
  els.sendStartSlideshowButton.addEventListener("click", () => runAction(() => sendJsonRequest({ func: "start_slideshow", fileNames: parseList(els.slideshowFiles.value), interval: getNumber("slideshowInterval", 60), random: !!els.slideshowRandom.checked }, "/slideshow"), els.sendStartSlideshowButton));
  els.sendSetSlideshowButton.addEventListener("click", () => runAction(() => sendJsonRequest({ func: "set_slideshow", sw: parseInt(els.slideshowSw.value, 10), interval: getNumber("setSlideshowInterval", 60), random: !!els.setSlideshowRandom.checked }, "/slideshow_control"), els.sendSetSlideshowButton));
  els.sendDeleteButton.addEventListener("click", () => runAction(() => sendJsonRequest({ func: "delete", fileNames: parseList(els.deleteFiles.value) }, "/delete"), els.sendDeleteButton));
  els.sendWifiWorkTimeButton.addEventListener("click", () => runAction(() => sendJsonRequest({ func: "set_wifi_work_time", seconds: getNumber("wifiWorkSeconds", 300) }, "/wifi_work_time"), els.sendWifiWorkTimeButton));
  els.sendDirectFileButton?.addEventListener("click", () => runAction(sendDirectFile, els.sendDirectFileButton));
  els.sendSingleMultipartButton?.addEventListener("click", () => runAction(sendSingleMultipart, els.sendSingleMultipartButton));
  els.sendCast2picButton?.addEventListener("click", () => runAction(sendCast2pic, els.sendCast2picButton));

  [els.directFileInput, els.singleBin, els.singleImage, els.pic1Bin, els.pic1Image, els.pic2Bin, els.pic2Image].forEach((input) => {
    input?.addEventListener("change", updateFileInfo);
  });
  renderEpdTypeInfo(null);
  renderEpdTypeList();
  updateFileInfo();
}

function bindSettingsStorage() {
  const ids = ["baudrate","dataBits","stopBits","parity","flowControl","bufferSize","serialOpenTimeoutMs","serialResetTimeoutMs","writeChunkSize","writeChunkDelayMs","writeChunkTimeoutMs","jsonRouteMode","fileRouteMode","autoConnect","prettyRxJson","directFilePath","directFileContentType","directFileFunc"];
  const defaultUpgrades = {
    bufferSize: { oldValue: "255", newValue: String(SERIAL_DEFAULT_BUFFER_SIZE) },
    writeChunkSize: { oldValue: "4096", newValue: String(SERIAL_DEFAULT_WRITE_CHUNK_SIZE) },
    writeChunkTimeoutMs: { oldValue: "5000", newValue: String(SERIAL_DEFAULT_WRITE_CHUNK_TIMEOUT_MS) },
  };
  for (const id of ids) {
    const el = $(id);
    if (!el) continue;
    const key = `tdx_serial_protocol_${id}`;
    let saved = localStorage.getItem(key);
    const upgrade = defaultUpgrades[id];
    if (upgrade && saved === upgrade.oldValue) {
      saved = upgrade.newValue;
      localStorage.setItem(key, saved);
    }
    if (saved !== null) {
      if (el.type === "checkbox") el.checked = saved === "true";
      else el.value = saved;
    }
    el.addEventListener("change", () => {
      const value = el.type === "checkbox" ? String(el.checked) : el.value;
      localStorage.setItem(key, value);
      showOperationToast(`设置已保存：${id} = ${value}`);
    });
  }
}

function startAutoDetect() {
  if (autoDetectTimer) return;
  autoDetectTimer = setInterval(() => {
    if (!els.autoConnect.checked || isConnected() || manualDisconnect) return;
    autoConnectKnownPort();
  }, 1500);
  autoConnectKnownPort();
}

window.addEventListener("error", (event) => {
  showFaultPopup("页面脚本错误", event.message || "JavaScript error", event.error);
});
window.addEventListener("unhandledrejection", (event) => {
  showFaultPopup("页面异步操作失败", "Promise rejected", event.reason);
});

window.addEventListener("DOMContentLoaded", () => {
  startProtocolPageHeartbeat();
  bindSettingsStorage();
  bindEvents();
  setUiConnected(false);
  rxLine("串口 HTTP-like 协议页面 v20 已加载。WiFi POST /wifi 已按示例改成 HTTP-like JSON，请求预览会自动计算 Content-Length。 ");
  rxLine("v20 新增：接收窗口非阻塞刷新、自动截断和串口数据洪泛保护，避免配网后大量日志导致页面无响应。 ");
  rxLine("设备端接收必须按 Content-Length 判断请求完整性，不要用超时结束请求。 ");
  if (!navigator.serial) {
    setStatus("当前浏览器不支持 Web Serial。请使用 Chrome / Edge 并通过 http://localhost 打开。");
  } else {
    setStatus("未连接；会后台检测已授权的 ESP32-S3 USB 串口");
    try {
      navigator.serial.addEventListener("disconnect", (event) => {
        if (serialPort && event?.target === serialPort) {
          disconnectOnSerialFault("浏览器检测到串口已拔出/失效", "serial disconnect event");
        }
      });
    } catch (_) {}
    startAutoDetect();
  }
});

window.addEventListener("pagehide", () => {
  stopProtocolPageHeartbeat();
});

window.addEventListener("beforeunload", () => {
  flushRxOutput();
  stopProtocolPageHeartbeat();
});
