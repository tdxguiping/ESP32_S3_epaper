// ESP Launchpad compatible Web Serial filters.
// 与 ESP Launchpad 接近的串口过滤列表。
const USB_PORT_FILTERS = [
  { usbVendorId: 0x10c4, usbProductId: 0xea60 }, // CP2102/CP2102N
  { usbVendorId: 0x0403, usbProductId: 0x6010 }, // FT2232H
  { usbVendorId: 0x303a, usbProductId: 0x1001 }, // Espressif USB_SERIAL_JTAG
  { usbVendorId: 0x303a, usbProductId: 0x1002 }, // Espressif esp-usb-bridge firmware
  { usbVendorId: 0x303a, usbProductId: 0x0002 }, // ESP32-S2 USB_CDC
  { usbVendorId: 0x303a, usbProductId: 0x0009 }, // ESP32-S3 USB_CDC
  { usbVendorId: 0x1a86, usbProductId: 0x55d4 }, // CH9102F
  { usbVendorId: 0x1a86, usbProductId: 0x7523 }, // CH340T
  { usbVendorId: 0x0403, usbProductId: 0x6001 }, // FT232R
];

// Ports that Windows often shows as "USB JTAG/serial debug unit".
// Web Serial cannot read the human friendly COM port name from Chrome,
// so automatic detection uses Espressif USB VID/PID values instead.
// Windows 上常见名称是 "USB JTAG/serial debug unit"。
// 浏览器 JS 读不到这个显示名称，所以后台检测用 Espressif USB VID/PID 匹配。
const AUTO_CONSOLE_PORT_MATCHES = [
  { usbVendorId: 0x303a, usbProductId: 0x1001, label: "USB JTAG/serial debug unit" },
  { usbVendorId: 0x303a, usbProductId: 0x0009, label: "ESP32-S3 USB Serial" },
  { usbVendorId: 0x303a, usbProductId: 0x0002, label: "ESP32-S2 USB Serial" },
  { usbVendorId: 0x303a, usbProductId: 0x1002, label: "Espressif USB bridge" },
];

// Cross-page guard: when serial_protocol.html is open, the main flashing page
// must release the COM port and stop every background auto-connect task.
// 跨页面保护：serial_protocol.html 打开时，主烧写页面必须释放串口，
// 并停止后台自动连接，避免两个页面抢同一个 COM 口。
const SERIAL_PROTOCOL_CHANNEL_NAME = "tdx_esp32s3_serial_protocol_channel";
const SERIAL_PROTOCOL_ACTIVE_KEY = "tdx_serial_protocol_page_active";
const SERIAL_PROTOCOL_HEARTBEAT_KEY = "tdx_serial_protocol_page_heartbeat";
const SERIAL_PROTOCOL_HEARTBEAT_MAX_AGE_MS = 6000;
const INDEX_AUTO_CONSOLE_DETECT_ENABLE = false;


const FIRMWARE_FILES = [
  { id: "bootloader", address: 0x0000, label: "Bootloader", defaultName: "bootloader.bin" },
  { id: "partitionTable", address: 0x8000, label: "Partition Table", defaultName: "partition-table.bin" },
  { id: "otaData", address: 0xD000, label: "OTA Data Initial", defaultName: "ota_data_initial.bin" },
  { id: "app", address: 0x20000, label: "Application", defaultName: "file_server.bin" },
];

// Firmware loaded by fetch() from the parent directory. Browser security does not
// allow setting <input type="file"> values, so the auto-loaded files are kept
// here and used by the flasher unless the user manually chooses another file.
// 通过 fetch() 从上一级目录加载固件。浏览器不允许代码设置文件输入框，
// 所以自动加载的数据保存在这里；用户手动选择文件时，手动文件优先。
const autoFirmwareFiles = new Map();

let device = null;
let rememberedPortInfo = null;
let transport = null;
let esploader = null;
let ESPLoader = null;
let Transport = null;
let connectedChipName = "";
let currentMode = "none"; // none | loader | console
let lastConnectedMode = "loader";

let consolePort = null;
let consoleReader = null;
let consoleReading = false;
let reconnecting = false;
let reconnectToken = 0;
let suppressAutoReconnectUntil = 0;
let autoConsoleDetectTimer = null;
let autoConsoleDetectBusy = false;
let autoConsoleScanPausedUntil = 0;
let lastAutoConsoleNoPortLogAt = 0;
let serialProtocolPageActive = false;
let serialProtocolPauseTimer = null;
let serialProtocolBroadcastChannel = null;

const logEl = document.getElementById("log");
const consoleOutputEl = document.getElementById("consoleOutput");
const consoleInputEl = document.getElementById("consoleInput");
const baudrateEl = document.getElementById("baudrate");
const consoleBaudrateEl = document.getElementById("consoleBaudrate");
const flashModeEl = document.getElementById("flashMode");
const flashFreqEl = document.getElementById("flashFreq");
const flashSizeEl = document.getElementById("flashSize");
const eraseAllEl = document.getElementById("eraseAll");
const strictS3El = document.getElementById("strictS3");
const autoReconnectEl = document.getElementById("autoReconnect");
const reconnectIntervalMsEl = document.getElementById("reconnectIntervalMs");
const reconnectStatusEl = document.getElementById("reconnectStatus");
const autoConsoleDetectStatusEl = document.getElementById("autoConsoleDetectStatus");
const appendCRLFEl = document.getElementById("appendCRLF");
const serialDataBitsEl = document.getElementById("serialDataBits");
const serialStopBitsEl = document.getElementById("serialStopBits");
const serialParityEl = document.getElementById("serialParity");
const serialFlowControlEl = document.getElementById("serialFlowControl");
const serialBufferSizeEl = document.getElementById("serialBufferSize");
const autoConsoleAttemptsEl = document.getElementById("autoConsoleAttempts");
const autoConsoleDelayMsEl = document.getElementById("autoConsoleDelayMs");
const resetSettingsButton = document.getElementById("resetSettingsButton");

const checkFilesButton = document.getElementById("checkFilesButton");
const connectButton = document.getElementById("connectButton");
const flashButton = document.getElementById("flashButton");
const eraseButton = document.getElementById("eraseButton");
const resetButton = document.getElementById("resetButton");
const disconnectButton = document.getElementById("disconnectButton");
const consoleConnectButton = document.getElementById("consoleConnectButton");
const consoleResetButton = document.getElementById("consoleResetButton");
const consoleDisconnectButton = document.getElementById("consoleDisconnectButton");
const openConsoleButton = document.getElementById("openConsoleButton");
const closeConsoleButton = document.getElementById("closeConsoleButton");
const clearConsoleButton = document.getElementById("clearConsoleButton");
const sendConsoleButton = document.getElementById("sendConsoleButton");

const openSerialProtocolButton = document.getElementById("openSerialProtocolButton");

const terminal = {
  clean() { logEl.textContent = ""; },
  writeLine(data) { appendLog(`${data}\n`); },
  write(data) { appendLog(data); },
};

function delay(ms) { return new Promise((resolve) => setTimeout(resolve, ms)); }
function settleWithin(promise, ms, label) {
  // Resolve after timeout instead of waiting forever during serial cleanup.
  // 串口清理只等固定时间，避免点击“断开”后页面卡住。
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
function appendLog(message) { logEl.textContent += String(message); logEl.scrollTop = logEl.scrollHeight; }
function logLine(message) { appendLog(`${message}\n`); }
function appendConsole(message) { consoleOutputEl.textContent += String(message); consoleOutputEl.scrollTop = consoleOutputEl.scrollHeight; }
function consoleLine(message) { appendConsole(`${message}\n`); }

function nowTimeText() { return new Date().toLocaleTimeString(); }
function errorToText(err) {
  if (!err) return "unknown";
  if (err instanceof Error) return `${err.name || "Error"}: ${err.message || err}`;
  if (typeof err === "object") {
    try { return JSON.stringify(err, null, 2); } catch (_) {}
  }
  return String(err);
}
function showFaultPopup(title, message, err) {
  const box = document.getElementById("faultPopup");
  const titleEl = document.getElementById("faultPopupTitleText");
  const textEl = document.getElementById("faultPopupText");
  if (!box || !textEl) return;
  const detail = err !== undefined ? errorToText(err) : "";
  const full = `[${nowTimeText()}] ${title || "失败信息"}\n${message || ""}${detail ? `\n${detail}` : ""}`.trim();
  if (titleEl) titleEl.textContent = title || "失败信息";
  textEl.textContent = textEl.textContent ? `${full}\n\n${textEl.textContent}` : full;
  box.classList.remove("faultPopupHidden");
}
function hideFaultPopup() { document.getElementById("faultPopup")?.classList.add("faultPopupHidden"); }
function clearFaultPopup() {
  const textEl = document.getElementById("faultPopupText");
  if (textEl) textEl.textContent = "";
  hideFaultPopup();
}

function formatHex(address) { return `0x${address.toString(16).toUpperCase().padStart(4, "0")}`; }
function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} bytes`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}
function getFileInput(fileDef) { return document.getElementById(`file_${fileDef.id}`); }
function getSelectedFile(fileDef) { return getFileInput(fileDef)?.files?.[0] || null; }
function getSerialOptionsFromSettings() {
  let bufferSize = parseInt(serialBufferSizeEl?.value || "255", 10);
  if (!Number.isFinite(bufferSize) || bufferSize < 1) bufferSize = 255;
  if (bufferSize > 16777216) bufferSize = 16777216;

  const dataBits = parseInt(serialDataBitsEl?.value || "8", 10);
  const stopBits = parseInt(serialStopBitsEl?.value || "1", 10);
  const parity = ["none", "even", "odd"].includes(serialParityEl?.value) ? serialParityEl.value : "none";
  const flowControl = serialFlowControlEl?.value === "hardware" ? "hardware" : "none";

  return {
    dataBits: dataBits === 7 || dataBits === 8 ? dataBits : 8,
    stopBits: stopBits === 1 || stopBits === 2 ? stopBits : 1,
    parity,
    flowControl,
    bufferSize,
  };
}
function getFlashOptionValue(selectEl) { return selectEl.value === "keep" ? "keep" : selectEl.value; }
function getReconnectIntervalMs() { return parseInt(reconnectIntervalMsEl.value || "2000", 10); }
function getAutoConsoleAttempts() { return parseInt(autoConsoleAttemptsEl?.value || "8", 10); }
function getAutoConsoleDelayMs() { return parseInt(autoConsoleDelayMsEl?.value || "1000", 10); }
function setReconnectStatus(text) { reconnectStatusEl.textContent = `自动重连状态：${text}`; }
function setAutoConsoleDetectStatus(text) {
  if (autoConsoleDetectStatusEl) autoConsoleDetectStatusEl.textContent = `后台串口检测：${text}`;
}

function getSerialProtocolHeartbeatAgeMs() {
  const heartbeat = parseInt(localStorage.getItem(SERIAL_PROTOCOL_HEARTBEAT_KEY) || "0", 10);
  if (!Number.isFinite(heartbeat) || heartbeat <= 0) return Infinity;
  return Date.now() - heartbeat;
}

function isSerialProtocolPageFresh() {
  return localStorage.getItem(SERIAL_PROTOCOL_ACTIVE_KEY) === "1" &&
         getSerialProtocolHeartbeatAgeMs() < SERIAL_PROTOCOL_HEARTBEAT_MAX_AGE_MS;
}

function setSerialProtocolPageActive(active, reason = "") {
  serialProtocolPageActive = !!active;
  if (active) {
    setAutoConsoleDetectStatus(`串口协议页面已打开，主页面停止自动串口连接${reason ? `：${reason}` : ""}`);
    setReconnectStatus("串口协议页面占用串口，主页面已暂停");
  } else {
    setAutoConsoleDetectStatus("串口协议页面已关闭，主页面可恢复后台检测");
    setReconnectStatus("空闲");
  }
}

function stopAutoConsoleDetector() {
  if (autoConsoleDetectTimer) {
    clearInterval(autoConsoleDetectTimer);
    autoConsoleDetectTimer = null;
  }
  autoConsoleDetectBusy = false;
}

async function pauseMainPageSerialForProtocol(reason = "serial_protocol_active") {
  setSerialProtocolPageActive(true, reason);
  stopAutoConsoleDetector();
  stopAutoReconnect();
  autoConsoleScanPausedUntil = Date.now() + 24 * 60 * 60 * 1000;
  try {
    if (currentMode !== "none" || consolePort || esploader || transport) {
      logLine(`检测到串口协议页面打开，主页面断开串口并停止自动连接。原因：${reason}`);
      await cleanupAllSerialConnections();
    }
  } catch (err) {
    logLine(`主页面释放串口失败：${err?.message || err}`);
  }
}

function resumeMainPageSerialAfterProtocol(reason = "serial_protocol_inactive") {
  if (isSerialProtocolPageFresh()) return;
  setSerialProtocolPageActive(false, reason);
  autoConsoleScanPausedUntil = 0;
  if (INDEX_AUTO_CONSOLE_DETECT_ENABLE && !autoConsoleDetectTimer) startAutoConsoleDetector();
}

function broadcastSerialProtocolState(active) {
  const message = { type: active ? "serial_protocol_active" : "serial_protocol_inactive", at: Date.now() };
  try { serialProtocolBroadcastChannel?.postMessage(message); } catch (_) {}
  try {
    if (active) {
      localStorage.setItem(SERIAL_PROTOCOL_ACTIVE_KEY, "1");
      localStorage.setItem(SERIAL_PROTOCOL_HEARTBEAT_KEY, String(Date.now()));
    } else {
      localStorage.removeItem(SERIAL_PROTOCOL_ACTIVE_KEY);
      localStorage.removeItem(SERIAL_PROTOCOL_HEARTBEAT_KEY);
    }
  } catch (_) {}
}

function setupSerialProtocolPageGuard() {
  if ("BroadcastChannel" in window) {
    serialProtocolBroadcastChannel = new BroadcastChannel(SERIAL_PROTOCOL_CHANNEL_NAME);
    serialProtocolBroadcastChannel.onmessage = (event) => {
      const data = event.data || {};
      if (data.type === "serial_protocol_active" || data.type === "serial_protocol_heartbeat") {
        pauseMainPageSerialForProtocol(data.type);
      } else if (data.type === "serial_protocol_inactive") {
        resumeMainPageSerialAfterProtocol(data.type);
      }
    };
  }

  window.addEventListener("storage", (event) => {
    if (event.key === SERIAL_PROTOCOL_ACTIVE_KEY || event.key === SERIAL_PROTOCOL_HEARTBEAT_KEY) {
      if (isSerialProtocolPageFresh()) pauseMainPageSerialForProtocol("storage_event");
      else resumeMainPageSerialAfterProtocol("storage_event");
    }
  });

  serialProtocolPauseTimer = setInterval(() => {
    if (isSerialProtocolPageFresh()) {
      if (!serialProtocolPageActive) pauseMainPageSerialForProtocol("heartbeat_detected");
    } else if (serialProtocolPageActive) {
      resumeMainPageSerialAfterProtocol("heartbeat_timeout");
    }
  }, 1000);

  if (isSerialProtocolPageFresh()) {
    pauseMainPageSerialForProtocol("page_init_detected_existing_protocol_page");
  }
}

function describeSerialPort(port) {
  try {
    const info = port?.getInfo ? port.getInfo() : {};
    const vid = info.usbVendorId !== undefined ? `VID=0x${info.usbVendorId.toString(16).padStart(4, "0")}` : "VID=?";
    const pid = info.usbProductId !== undefined ? `PID=0x${info.usbProductId.toString(16).padStart(4, "0")}` : "PID=?";
    const match = AUTO_CONSOLE_PORT_MATCHES.find((item) =>
      item.usbVendorId === info.usbVendorId && item.usbProductId === info.usbProductId
    );
    return `${match?.label || "Serial Port"} (${vid}, ${pid})`;
  } catch (_) {
    return "Serial Port";
  }
}

function isAutoConsoleTargetPort(port) {
  if (!port?.getInfo) return false;
  try {
    const info = port.getInfo();
    return AUTO_CONSOLE_PORT_MATCHES.some((item) =>
      item.usbVendorId === info.usbVendorId && item.usbProductId === info.usbProductId
    );
  } catch (_) {
    return false;
  }
}

const SETTINGS_IDS = [
  "baudrate", "consoleBaudrate", "serialDataBits", "serialStopBits", "serialParity",
  "serialFlowControl", "serialBufferSize", "reconnectIntervalMs", "autoConsoleAttempts",
  "autoConsoleDelayMs", "autoReconnect", "appendCRLF", "flashMode", "flashFreq",
  "flashSize", "eraseAll", "strictS3"
];

function loadSavedSettings() {
  for (const id of SETTINGS_IDS) {
    const el = document.getElementById(id);
    if (!el) continue;
    const saved = localStorage.getItem(`tdx_flasher_${id}`);
    if (saved === null) continue;
    if (el.type === "checkbox") el.checked = saved === "true";
    else el.value = saved;
  }
}

function bindSettingsPersistence() {
  for (const id of SETTINGS_IDS) {
    const el = document.getElementById(id);
    if (!el) continue;
    el.addEventListener("change", () => {
      localStorage.setItem(`tdx_flasher_${id}`, el.type === "checkbox" ? String(el.checked) : el.value);
    });
  }
}

function resetSavedSettings() {
  for (const id of SETTINGS_IDS) localStorage.removeItem(`tdx_flasher_${id}`);
  location.reload();
}

function setCurrentMode(mode) {
  currentMode = mode;
  if (mode !== "none") lastConnectedMode = mode;
  setConnectedUi(mode);
}

function setConnectedUi(mode) {
  const isLoader = mode === "loader";
  const isConsole = mode === "console";
  const isBusyReconnect = reconnecting;

  connectButton.disabled = isLoader || isConsole || isBusyReconnect;
  if (consoleConnectButton) consoleConnectButton.disabled = isLoader || isConsole || isBusyReconnect;
  flashButton.disabled = !isLoader || isBusyReconnect;
  eraseButton.disabled = !isLoader || isBusyReconnect;
  resetButton.disabled = mode === "none" || isBusyReconnect;
  if (consoleResetButton) consoleResetButton.disabled = mode === "none" || isBusyReconnect;
  disconnectButton.disabled = mode === "none" && !isBusyReconnect;
  if (consoleDisconnectButton) consoleDisconnectButton.disabled = mode === "none" && !isBusyReconnect;

  openConsoleButton.disabled = isConsole || isBusyReconnect;
  closeConsoleButton.disabled = !isConsole;
  sendConsoleButton.disabled = !isConsole;
  consoleInputEl.disabled = !isConsole;
}

function isSerialDisconnectError(err) {
  const msg = String(err?.message || err || "").toLowerCase();
  return msg.includes("device has been lost") ||
         msg.includes("port is closed") ||
         msg.includes("port is already closed") ||
         msg.includes("the device has been lost") ||
         msg.includes("networkerror") ||
         msg.includes("disconnected") ||
         msg.includes("lost") ||
         msg.includes("closed") ||
         msg.includes("failed to read") ||
         msg.includes("failed to write");
}

function rememberPort(port) {
  device = port;
  try { rememberedPortInfo = port?.getInfo ? port.getInfo() : null; }
  catch (_) { rememberedPortInfo = null; }
}

function portMatchesRemembered(port) {
  if (!port) return false;
  if (device && port === device) return true;
  if (!rememberedPortInfo || !port.getInfo) return false;
  try {
    const info = port.getInfo();
    if (rememberedPortInfo.usbVendorId && info.usbVendorId !== rememberedPortInfo.usbVendorId) return false;
    if (rememberedPortInfo.usbProductId && info.usbProductId !== rememberedPortInfo.usbProductId) return false;
    return Boolean(info.usbVendorId || info.usbProductId);
  } catch (_) {
    return false;
  }
}

async function findRememberedPort() {
  if (!navigator.serial?.getPorts) return null;
  const ports = await navigator.serial.getPorts();
  if (device && ports.includes(device)) return device;
  return ports.find(portMatchesRemembered) || ports[0] || null;
}

async function findAutoConsoleTargetPort() {
  if (!navigator.serial?.getPorts) return null;
  const ports = await navigator.serial.getPorts();
  // If the remembered device is already the Espressif USB JTAG/serial port, prefer it.
  // 如果上次授权的设备就是 Espressif USB JTAG/serial 串口，优先使用它。
  if (device && ports.includes(device) && isAutoConsoleTargetPort(device)) return device;
  return ports.find(isAutoConsoleTargetPort) || null;
}

async function tryAutoOpenDetectedConsole(reason = "periodic-scan") {
  if (!navigator.serial?.getPorts) {
    setAutoConsoleDetectStatus("当前浏览器不支持 navigator.serial.getPorts");
    return;
  }
  if (!window.isSecureContext) {
    setAutoConsoleDetectStatus("页面不是 secure context，请用 http://localhost 或 HTTPS 打开");
    return;
  }
  if (serialProtocolPageActive || isSerialProtocolPageFresh()) {
    setAutoConsoleDetectStatus("串口协议页面已打开，主页面停止后台自动连接");
    return;
  }
  if (autoConsoleDetectBusy || reconnecting) return;
  if (Date.now() < autoConsoleScanPausedUntil) return;
  if (currentMode === "console" && consolePort) {
    setAutoConsoleDetectStatus(`已连接 ${describeSerialPort(consolePort)}`);
    return;
  }
  // Do not steal the port while bootloader/flashing mode is connected.
  // 烧写模式已连接时，不抢占串口。
  if (currentMode !== "none") return;

  autoConsoleDetectBusy = true;
  try {
    const port = await findAutoConsoleTargetPort();
    if (!port) {
      setAutoConsoleDetectStatus("未发现已授权的 USB JTAG/serial debug unit；先手动点一次连接设备授权串口");
      const now = Date.now();
      if (now - lastAutoConsoleNoPortLogAt > 15000) {
        consoleLine("\n[Auto detect] 未发现已授权的 USB JTAG/serial debug unit。浏览器安全限制下，第一次必须手动点击连接设备并授权串口。\n");
        lastAutoConsoleNoPortLogAt = now;
      }
      return;
    }

    const desc = describeSerialPort(port);
    setAutoConsoleDetectStatus(`检测到 ${desc}，正在自动打开控制台...`);
    consoleLine(`
[Auto detect] 检测到 ${desc}，自动打开串口控制台。原因：${reason}
`);
    await openConsoleOnPort(port, { auto: true, source: "auto-detect" });
    setAutoConsoleDetectStatus(`已自动连接 ${desc}`);
    logLine(`后台检测到 ${desc}，已自动打开串口控制台。`);
  } catch (err) {
    const msg = err?.message || String(err);
    setAutoConsoleDetectStatus(`自动打开失败：${msg}`);
    consoleLine(`
[Auto detect] 自动打开控制台失败：${msg}
`);
    await cleanupAllSerialConnections();
  } finally {
    autoConsoleDetectBusy = false;
  }
}

function startAutoConsoleDetector() {
  if (!INDEX_AUTO_CONSOLE_DETECT_ENABLE) {
    setAutoConsoleDetectStatus("已关闭，需手动点击连接设备");
    return;
  }
  if (serialProtocolPageActive || isSerialProtocolPageFresh()) {
    setAutoConsoleDetectStatus("串口协议页面已打开，主页面不启动后台自动连接");
    return;
  }
  if (autoConsoleDetectTimer) return;
  if (!navigator.serial) {
    setAutoConsoleDetectStatus("当前浏览器不支持 Web Serial");
    return;
  }
  setAutoConsoleDetectStatus("已启动，等待检测 USB JTAG/serial debug unit");
  // Run once after page initialization, then keep scanning in background.
  // 页面初始化后先运行一次，之后后台持续检测。
  setTimeout(() => tryAutoOpenDetectedConsole("page-load"), 500);
  autoConsoleDetectTimer = setInterval(() => {
    tryAutoOpenDetectedConsole("background-scan");
  }, 2000);
}

async function cleanupLoaderConnection() {
  const oldTransport = transport;
  esploader = null;
  transport = null;
  connectedChipName = "";
  if (oldTransport) {
    try {
      const r = await settleWithin(oldTransport.disconnect(), 1000, "loader transport.disconnect");
      if (!r.ok) logLine(`[后台断开] ${r.label} 超时，页面先恢复控制。`);
    } catch (_) { /* The port may already be gone. 串口可能已经断开。 */ }
  }
}

async function cleanupConsoleConnection() {
  consoleReading = false;
  const reader = consoleReader;
  const port = consolePort;
  consoleReader = null;
  consolePort = null;

  if (reader) {
    try {
      const r = await settleWithin(reader.cancel(), 300, "console reader.cancel");
      if (!r.ok) consoleLine(`[后台断开] ${r.label} 超时，页面先恢复控制。`);
      try { reader.releaseLock?.(); } catch (_) {}
    } catch (_) { /* The reader may already be cancelled. reader 可能已经取消。 */ }
  }
  if (port) {
    try {
      const r = await settleWithin(port.close(), 700, "console port.close");
      if (!r.ok) {
        consoleLine(`[后台断开] ${r.label} 超时，可拔插设备或刷新页面后重新连接。`);
        showFaultPopup("串口后台断开未完全完成", "页面已恢复控制；浏览器底层串口关闭超时。", r.err || r.label);
      }
    } catch (_) { /* The port may already be closed. 串口可能已经关闭。 */ }
  }
}

async function cleanupAllSerialConnections() {
  await cleanupConsoleConnection();
  await cleanupLoaderConnection();
  setCurrentMode("none");
}

function getSelectedFirmwareSource(fileDef) {
  const file = getSelectedFile(fileDef);
  if (file) return { type: "manual", file, name: file.name, size: file.size };
  const autoFile = autoFirmwareFiles.get(fileDef.id);
  if (autoFile) return { type: "auto", ...autoFile };
  return null;
}

function updateFileStatus(fileDef) {
  const statusEl = document.getElementById(`status_${fileDef.id}`);
  const source = getSelectedFirmwareSource(fileDef);
  if (!source) { statusEl.className = "wait"; statusEl.textContent = `未选择，正在尝试自动加载 ../${fileDef.defaultName}`; return; }
  if (source.size <= 0) { statusEl.className = "bad"; statusEl.textContent = `错误：空文件 ${source.name}`; return; }
  statusEl.className = "ok";
  if (source.type === "manual") statusEl.textContent = `手动 OK，${source.name}，${formatBytes(source.size)}`;
  else statusEl.textContent = `自动 OK，${source.name}，${formatBytes(source.size)}，来源=${source.source}`;
}
function updateAllFileStatus() { FIRMWARE_FILES.forEach(updateFileStatus); }

function arrayBufferToBinaryString(buffer) {
  const bytes = new Uint8Array(buffer);
  const chunkSize = 0x8000;
  let result = "";
  for (let i = 0; i < bytes.length; i += chunkSize) {
    const chunk = bytes.subarray(i, i + chunkSize);
    result += String.fromCharCode.apply(null, chunk);
  }
  return result;
}

async function autoLoadDefaultFirmwareFromParent() {
  logLine("开始尝试自动加载上一级目录中的 4 个默认 bin 文件...");
  for (const fileDef of FIRMWARE_FILES) {
    const urls = [`../${fileDef.defaultName}`, `./${fileDef.defaultName}`];
    let loaded = false;
    for (const url of urls) {
      try {
        const resp = await fetch(url, { cache: "no-store" });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const buffer = await resp.arrayBuffer();
        if (!buffer.byteLength) throw new Error("empty file");
        autoFirmwareFiles.set(fileDef.id, {
          name: fileDef.defaultName,
          size: buffer.byteLength,
          data: arrayBufferToBinaryString(buffer),
          source: url,
        });
        logLine(`自动加载成功：${formatHex(fileDef.address)} ${url} ${buffer.byteLength} bytes`);
        loaded = true;
        break;
      } catch (err) {
        // Try next candidate silently, then report only after all candidates fail.
        // 静默尝试下一个路径，所有路径失败后再报告。
      }
    }
    if (!loaded) logLine(`自动加载失败：${formatHex(fileDef.address)} ../${fileDef.defaultName}，可手动选择文件。`);
    updateFileStatus(fileDef);
  }
}

function readFileAsBinaryString(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = () => reject(reader.error || new Error(`读取文件失败：${file.name}`));
    reader.readAsBinaryString(file);
  });
}

async function loadFirmwareFiles() {
  updateAllFileStatus();
  const fileArray = [];
  const missing = [];
  for (const fileDef of FIRMWARE_FILES) {
    const source = getSelectedFirmwareSource(fileDef);
    if (!source) { missing.push(`${formatHex(fileDef.address)} ${fileDef.label}`); continue; }
    if (source.size <= 0) throw new Error(`${fileDef.label} 是空文件：${source.name}`);
    const data = source.type === "manual" ? await readFileAsBinaryString(source.file) : source.data;
    fileArray.push({ data, address: fileDef.address });
  }
  if (missing.length > 0) throw new Error(`还有文件没选/自动加载失败：${missing.join("，")}`);
  return fileArray;
}

async function loadEsptoolJs() {
  if (ESPLoader && Transport) return;
  let esptooljs = null;
  try {
    // Prefer local npm install, same dependency as ESP Launchpad.
    // 优先使用 npm install 后的本地 esptool-js。
    esptooljs = await import("../node_modules/esptool-js/bundle.js");
    logLine("esptool-js: local node_modules loaded");
  } catch (localErr) {
    logLine("esptool-js: local node_modules not found, loading CDN fallback...");
    esptooljs = await import("https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/bundle.js");
    logLine("esptool-js: CDN loaded");
  }
  ESPLoader = esptooljs.ESPLoader;
  Transport = esptooljs.Transport;
  if (!ESPLoader || !Transport) throw new Error("esptool-js loaded, but ESPLoader/Transport is missing");
}

async function finishConnectAfterPortSelected(selectedDevice, options = {}) {
  await cleanupAllSerialConnections();
  await settleWithin(loadEsptoolJs(), 10000, "load esptool-js").then((result) => {
    if (!result.ok) {
      throw result.err || new Error(result.timeout ? "加载 esptool-js 超时" : "加载 esptool-js 失败");
    }
  });
  rememberPort(selectedDevice);
  transport = new Transport(selectedDevice);

  const loaderOptions = {
    transport,
    baudrate: parseInt(baudrateEl.value, 10),
    terminal,
    serialOptions: getSerialOptionsFromSettings(),
  };

  esploader = new ESPLoader(loaderOptions);
  logLine(`${options.auto ? "自动重连" : "连接设备"}，baudrate=${loaderOptions.baudrate} ...`);
  connectedChipName = await esploader.main();
  const chipName = esploader?.chip?.CHIP_NAME || connectedChipName;
  await esploader.flashId();
  logLine(`Connected to device: ${connectedChipName}`);
  logLine(`Chip: ${chipName}`);

  const normalizedName = String(chipName || connectedChipName).toUpperCase();
  if (strictS3El.checked && !normalizedName.includes("ESP32-S3")) {
    throw new Error(`检测到 ${chipName || connectedChipName}，当前页面只允许 ESP32-S3`);
  }
  setCurrentMode("loader");
  setReconnectStatus("已连接烧写模式");
}

// IMPORTANT: This function is called directly by the button's onclick attribute.
// 关键点：这个函数由按钮 onclick 直接调用，第一步就是 requestPort，和 ESP Launchpad 的 Connect 行为一致。
window.connectDeviceFromUserClick = async function connectDeviceFromUserClick() {
  try {
    if (!("serial" in navigator)) {
      throw new Error("当前浏览器没有 navigator.serial。请使用 Chrome / Edge，并通过 http://localhost 或 HTTPS 打开页面。");
    }
    if (!window.isSecureContext) {
      throw new Error("当前页面不是 secure context。请用 http://localhost:8000 或 HTTPS 打开，不要用普通 file:// 或局域网 http://IP。");
    }

    // This must be called immediately inside the user click event.
    // 必须在用户点击事件里立即调用，否则 Chrome/Edge 不会弹出串口选择窗口。
    const selectedDevice = await navigator.serial.requestPort({ filters: USB_PORT_FILTERS });
    stopAutoReconnect();
    logLine("已选择串口，开始加载 esptool-js...");

    connectButton.disabled = true;
    await finishConnectAfterPortSelected(selectedDevice);
  } catch (err) {
    console.error(err);
    const msg = err?.message || String(err);
    logLine(`ERROR: ${msg}`);
    showFaultPopup("连接设备失败", "连接失败或未弹出串口选择窗口。", err);
    setCurrentMode("none");
  }
};

window.openConsoleFromUserClick = openConsoleFromUserClick;

async function openConsoleFromUserClick() {
  try {
    if (!("serial" in navigator)) {
      throw new Error("当前浏览器没有 navigator.serial。请使用 Chrome / Edge，并通过 http://localhost 或 HTTPS 打开页面。");
    }
    if (!window.isSecureContext) {
      throw new Error("当前页面不是 secure context。请用 http://localhost:8000 或 HTTPS 打开。");
    }

    let selectedDevice = device;
    if (!selectedDevice) {
      selectedDevice = await navigator.serial.requestPort({ filters: USB_PORT_FILTERS });
    }
    stopAutoReconnect();
    await openConsoleOnPort(selectedDevice, { auto: false });
  } catch (err) {
    console.error(err);
    const msg = err?.message || String(err);
    logLine(`ERROR: ${msg}`);
    showFaultPopup("打开串口控制台失败", "打开串口控制台失败。", err);
    if (isSerialDisconnectError(err)) handleSerialLost("打开串口控制台时串口断开", "console", err);
  }
}

async function openConsoleOnPort(port, options = {}) {
  await cleanupAllSerialConnections();
  rememberPort(port);

  const baudRate = parseInt(consoleBaudrateEl.value, 10);
  logLine(`${options.auto ? (options.source === "auto-detect" ? "后台检测自动打开" : "自动重连") : "打开"}串口控制台，baudrate=${baudRate} ...`);
  await port.open({ baudRate, ...getSerialOptionsFromSettings() });
  consolePort = port;
  consoleReading = true;
  setCurrentMode("console");
  setReconnectStatus("已连接串口控制台");
  consoleLine(`\n[Console opened, baudrate=${baudRate}]`);
  startConsoleReadLoop();
}

async function startConsoleReadLoop() {
  const decoder = new TextDecoder();
  while (consoleReading && consolePort?.readable) {
    try {
      consoleReader = consolePort.readable.getReader();
      while (consoleReading) {
        const { value, done } = await consoleReader.read();
        if (done) break;
        if (value) appendConsole(decoder.decode(value, { stream: true }));
      }
    } catch (err) {
      if (consoleReading) {
        console.error(err);
        consoleLine(`\n[Console disconnected: ${err?.message || err}]`);
        handleSerialLost("串口控制台读取中断", "console", err);
      }
      break;
    } finally {
      try { consoleReader?.releaseLock(); }
      catch (_) { /* The lock may already be released. 锁可能已经释放。 */ }
      consoleReader = null;
    }
  }
}

async function sendConsoleText() {
  if (currentMode !== "console" || !consolePort?.writable) throw new Error("串口控制台没有打开");
  const encoder = new TextEncoder();
  const suffix = appendCRLFEl.checked ? "\r\n" : "";
  const text = `${consoleInputEl.value}${suffix}`;
  const writer = consolePort.writable.getWriter();
  try {
    await writer.write(encoder.encode(text));
    consoleInputEl.value = "";
  } finally {
    writer.releaseLock();
  }
}

async function closeConsoleByUser() {
  stopAutoReconnect();
  await cleanupConsoleConnection();
  if (currentMode === "console") setCurrentMode("none");
  setReconnectStatus("空闲");
  logLine("串口控制台已关闭。 ");
}

async function resetDevice() {
  if (currentMode === "loader" && esploader) {
    logLine("Reset Device: hard_reset ...");
    // Flash complete/reset is intentional. Do not force automatic bootloader reconnect.
    // 烧写模式下的手动 reset 是主动操作，不强制回到 bootloader。
    suppressAutoReconnectUntil = Date.now() + 2000;
    await esploader.after("hard_reset");
    await cleanupLoaderConnection();
    setCurrentMode("none");
    setReconnectStatus("Reset 完成，设备已运行应用");
    logLine("Reset Device done. ");
    return;
  }

  if (currentMode === "console" && consolePort) {
    logLine("Reset Device from console serial signals ...");
    await hardResetWithSerialSignals(consolePort);
    consoleLine("\n[Reset Device]\n");
    return;
  }

  throw new Error("没有已连接的设备");
}

async function hardResetWithSerialSignals(port) {
  if (!port?.setSignals) throw new Error("当前浏览器/串口对象不支持 setSignals，无法通过网页 Reset Device");
  // Keep GPIO0 released and pulse EN/reset via RTS.
  // 释放 GPIO0，只用 RTS 对 EN/reset 做一次复位脉冲。
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await delay(100);
  await port.setSignals({ requestToSend: false });
  await delay(100);
}

async function resetDeviceFromOpenConsoleAfterFlash() {
  if (currentMode !== "console" || !consolePort) {
    throw new Error("烧写后串口控制台没有打开，无法调用 Reset Device");
  }
  logLine("烧写后调用 Reset Device，让设备从 App 正常重启 ...");
  consoleLine("\n[Reset Device after flash]\n");
  await hardResetWithSerialSignals(consolePort);
  await delay(300);
  logLine("烧写后 Reset Device 完成，继续接收设备启动日志。 ");
}

async function disconnectDevice() {
  autoConsoleScanPausedUntil = Date.now() + 10000;
  stopAutoReconnect();
  await cleanupAllSerialConnections();
  device = null;
  rememberedPortInfo = null;
  setReconnectStatus("空闲");
  logLine("已断开。 ");
}

async function flashDevice() {
  autoConsoleScanPausedUntil = Date.now() + 120000;
  if (!esploader) throw new Error("请先连接设备");
  logLine("读取已选择的 4 个固件文件...");
  const fileArray = await loadFirmwareFiles();
  const flashOptions = {
    fileArray,
    flashMode: getFlashOptionValue(flashModeEl),
    flashFreq: getFlashOptionValue(flashFreqEl),
    flashSize: flashSizeEl.value,
    eraseAll: eraseAllEl.checked,
    compress: true,
    reportProgress: (fileIndex, written, total) => {
      const current = FIRMWARE_FILES[fileIndex];
      const source = current ? getSelectedFirmwareSource(current) : null;
      const name = source?.name || `file ${fileIndex}`;
      const pct = total > 0 ? ((written / total) * 100).toFixed(1) : "0.0";
      logLine(`${name} ${written}/${total} bytes ${pct}%`);
    },
  };
  logLine("开始烧写：");
  for (const fileDef of FIRMWARE_FILES) {
    const source = getSelectedFirmwareSource(fileDef);
    logLine(`  ${formatHex(fileDef.address)}  ${fileDef.label}  ${source?.name || "未选择"}  ${source?.type || "missing"}`);
  }
  logLine(`flash_mode=${flashOptions.flashMode}, flash_freq=${flashOptions.flashFreq}, flash_size=${flashOptions.flashSize}, eraseAll=${flashOptions.eraseAll}`);
  await esploader.writeFlash(flashOptions);
  logLine("烧写完成，执行 hard_reset ...");
  suppressAutoReconnectUntil = Date.now() + 8000;
  await esploader.after("hard_reset");
  await cleanupLoaderConnection();
  setCurrentMode("none");
  setReconnectStatus("烧写完成，设备已运行应用，请手动打开串口控制台");
  logLine("Flash Done ⚡️");
  autoConsoleScanPausedUntil = Date.now() + 3000;
}

async function openConsoleAfterFlashWithRetry() {
  const attempts = Math.max(1, getAutoConsoleAttempts());
  const waitMs = Math.max(200, getAutoConsoleDelayMs());
  logLine(`烧写完成后自动打开串口控制台，最多尝试 ${attempts} 次，间隔 ${waitMs} ms。`);

  for (let attempt = 1; attempt <= attempts; attempt++) {
    try {
      const port = await findRememberedPort();
      if (!port) throw new Error("没有找到浏览器已授权的串口");
      logLine(`自动打开串口控制台，第 ${attempt}/${attempts} 次...`);
      await openConsoleOnPort(port, { auto: true });
      logLine("烧写后串口控制台已打开，开始接收设备日志。 ");
      return true;
    } catch (err) {
      const msg = err?.message || String(err);
      logLine(`自动打开串口控制台第 ${attempt} 次失败：${msg}`);
      await cleanupAllSerialConnections();
      if (attempt < attempts) await delay(waitMs);
    }
  }

  setReconnectStatus("烧写完成，但自动打开控制台失败，可手动点击打开串口控制台");
  logLine("烧写完成，但多次自动打开串口控制台失败。请手动点击“打开串口控制台”。");
  return false;
}

async function eraseFlashOnly() {
  if (!esploader) throw new Error("请先连接设备");
  if (!confirm("确认整片擦除 Flash？这会清掉 NVS / WiFi 配置 / 用户数据。")) return;
  logLine("开始整片擦除 Flash ...");
  await esploader.eraseFlash();
  logLine("整片擦除完成。 ");
}

function stopAutoReconnect() {
  reconnectToken++;
  reconnecting = false;
  setReconnectStatus("空闲");
  setConnectedUi(currentMode);
}

function handleSerialLost(reason, targetMode, err = null) {
  showFaultPopup("串口连接异常", reason || "串口已断开", err);
  if (Date.now() < suppressAutoReconnectUntil) {
    logLine(`串口断开：${reason}。这是主动 reset/烧写完成后的预期断开，不自动重连。`);
    return;
  }
  if (serialProtocolPageActive || isSerialProtocolPageFresh()) {
    logLine(`串口断开：${reason}。串口协议页面已打开，主页面不自动重连。`);
    return;
  }
  if (err) console.error(err);
  const modeToReconnect = targetMode || currentMode || lastConnectedMode || "loader";
  logLine(`串口断开：${reason}。目标自动重连模式=${modeToReconnect}`);
  cleanupAllSerialConnections().finally(() => {
    if (autoReconnectEl.checked) startAutoReconnect(modeToReconnect, reason);
    else setReconnectStatus("串口已断开，自动重连未开启");
  });
}

async function startAutoReconnect(targetMode, reason) {
  if (reconnecting) return;
  reconnecting = true;
  const token = ++reconnectToken;
  setCurrentMode("none");
  setReconnectStatus(`重连中，目标=${targetMode}，原因=${reason}`);

  let attempt = 0;
  while (reconnecting && token === reconnectToken && autoReconnectEl.checked) {
    attempt++;
    try {
      const port = await findRememberedPort();
      if (!port) {
        setReconnectStatus(`等待串口重新出现，第 ${attempt} 次`);
        await delay(getReconnectIntervalMs());
        continue;
      }

      setReconnectStatus(`第 ${attempt} 次重连...`);
      if (targetMode === "console") await openConsoleOnPort(port, { auto: true });
      else await finishConnectAfterPortSelected(port, { auto: true });

      reconnecting = false;
      setReconnectStatus(`自动重连成功，模式=${targetMode}`);
      logLine(`自动重连成功，模式=${targetMode}`);
      return;
    } catch (reconnectErr) {
      console.error(reconnectErr);
      const msg = reconnectErr?.message || String(reconnectErr);
      setReconnectStatus(`第 ${attempt} 次失败：${msg}`);
      logLine(`自动重连第 ${attempt} 次失败：${msg}`);
      await cleanupAllSerialConnections();
      await delay(getReconnectIntervalMs());
    }
  }

  reconnecting = false;
  setConnectedUi(currentMode);
  setReconnectStatus("已停止");
}

async function runAction(action, button) {
  const oldDisabled = button.disabled;
  button.disabled = true;
  try { await action(); }
  catch (err) {
    console.error(err);
    const msg = err?.message || String(err);
    logLine(`ERROR: ${msg}`);
    showFaultPopup("操作失败", "当前操作失败。详细信息如下：", err);
    if (isSerialDisconnectError(err)) handleSerialLost(`操作过程中串口异常：${msg}`, currentMode || lastConnectedMode, err);
  } finally {
    if (!oldDisabled) button.disabled = false;
    setConnectedUi(currentMode);
  }
}

loadSavedSettings();
if (autoReconnectEl) autoReconnectEl.checked = false;
bindSettingsPersistence();

FIRMWARE_FILES.forEach((fileDef) => {
  const input = getFileInput(fileDef);
  input.addEventListener("change", () => updateFileStatus(fileDef));
});
updateAllFileStatus();
setCurrentMode("none");
setReconnectStatus("空闲");
autoLoadDefaultFirmwareFromParent();

checkFilesButton.addEventListener("click", () => runAction(async () => {
  await loadFirmwareFiles();
  logLine("4 个已选文件检查完成。 ");
  for (const fileDef of FIRMWARE_FILES) {
    const source = getSelectedFirmwareSource(fileDef);
    logLine(`  ${formatHex(fileDef.address)}  ${fileDef.label}  ${source.name}  ${source.size} bytes  ${source.type}`);
  }
}, checkFilesButton));

flashButton.addEventListener("click", () => runAction(flashDevice, flashButton));
eraseButton.addEventListener("click", () => runAction(eraseFlashOnly, eraseButton));
resetButton.addEventListener("click", () => runAction(resetDevice, resetButton));
disconnectButton.addEventListener("click", () => {
  // Disconnect must not hang behind runAction.
  // “断开”不能被 runAction 的 await 卡住。
  disconnectButton.disabled = true;
  disconnectDevice().catch((err) => {
    showFaultPopup("断开失败", "执行断开时出现异常。", err);
  }).finally(() => {
    disconnectButton.disabled = false;
    setConnectedUi(currentMode);
  });
});
consoleResetButton?.addEventListener("click", () => runAction(resetDevice, consoleResetButton));
consoleDisconnectButton?.addEventListener("click", () => runAction(disconnectDevice, consoleDisconnectButton));
openConsoleButton.addEventListener("click", () => runAction(openConsoleFromUserClick, openConsoleButton));
closeConsoleButton.addEventListener("click", () => runAction(closeConsoleByUser, closeConsoleButton));
clearConsoleButton.addEventListener("click", () => { consoleOutputEl.textContent = ""; });
sendConsoleButton.addEventListener("click", () => runAction(sendConsoleText, sendConsoleButton));
consoleInputEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    runAction(sendConsoleText, sendConsoleButton);
  }
});

autoReconnectEl.addEventListener("change", () => {
  if (!autoReconnectEl.checked) stopAutoReconnect();
});
resetSettingsButton?.addEventListener("click", resetSavedSettings);

if (navigator.serial?.addEventListener) {
  navigator.serial.addEventListener("disconnect", (event) => {
    const port = event.target;
    if (portMatchesRemembered(port)) {
      const targetMode = currentMode || lastConnectedMode || "loader";
      handleSerialLost("浏览器检测到串口设备断开", targetMode);
    }
  });
  navigator.serial.addEventListener("connect", (event) => {
    const port = event.target;
    if (reconnecting && portMatchesRemembered(port)) {
      logLine("浏览器检测到串口设备重新插入，下一轮自动重连会尝试连接。 ");
    }
    if (isAutoConsoleTargetPort(port)) {
      consoleLine(`
[Auto detect] 浏览器检测到 ${describeSerialPort(port)} 插入。
`);
      if (INDEX_AUTO_CONSOLE_DETECT_ENABLE) {
        setTimeout(() => tryAutoOpenDetectedConsole("serial-connect-event"), 500);
      }
    }
  });
}

document.getElementById("faultPopupCloseButton")?.addEventListener("click", hideFaultPopup);
document.getElementById("faultPopupClearButton")?.addEventListener("click", clearFaultPopup);
window.addEventListener("error", (event) => {
  showFaultPopup("页面脚本错误", event.message || "JavaScript error", event.error);
});
window.addEventListener("unhandledrejection", (event) => {
  showFaultPopup("页面异步操作失败", "Promise rejected", event.reason);
});

setupSerialProtocolPageGuard();
stopAutoConsoleDetector();
setAutoConsoleDetectStatus("已关闭，需手动点击连接设备");
if (connectButton) connectButton.disabled = false;
if (consoleConnectButton) consoleConnectButton.disabled = false;

logLine("页面 JS 已加载。点击“连接设备”后应立即弹出浏览器串口选择窗口。 ");
logLine("v20 新增：失败信息会在右下角小窗口显示，方便看到串口异常原因。 ");

async function openSerialProtocolPageFromMain(event) {
  if (event) event.preventDefault();
  broadcastSerialProtocolState(true);
  try {
    logLine("准备打开串口协议页面：主页面先断开串口并停止自动连接。 ");
    await pauseMainPageSerialForProtocol("open_serial_protocol_button");
  } catch (err) {
    logLine(`释放串口连接失败，仍继续打开串口协议页面：${err?.message || err}`);
  }
  window.open("serial_protocol.html", "_blank", "noopener,noreferrer");
}

for (const el of document.querySelectorAll("#openSerialProtocolButton, #openSerialProtocolLink")) {
  el.addEventListener("click", openSerialProtocolPageFromMain);
}
