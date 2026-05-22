import { holdingRegisters, inputRegisters } from "./registers.js";
import { ModbusRtuMaster } from "./modbus_rtu.js";

const $ = (id) => document.getElementById(id);

const ui = {
  btnShowDevice: $("btnShowDevice"),
  btnConnect: $("btnConnect"),
  btnDisconnect: $("btnDisconnect"),
  btnReadHolding: $("btnReadHolding"),
  btnSaveHolding: $("btnSaveHolding"),
  btnPollToggle: $("btnPollToggle"),
  btnClearLog: $("btnClearLog"),
  connStatus: $("connStatus"),
  serialLog: $("serialLog"),
  verboseRaw: $("verboseRaw"),
  portSelect: $("portSelect"),
  btnRefreshPorts: $("btnRefreshPorts"),
  btnAddPort: $("btnAddPort"),
  deviceDialog: $("deviceDialog"),
  baudrate: $("baudrate"),
  dataBits: $("dataBits"),
  parity: $("parity"),
  stopBits: $("stopBits"),
  slaveId: $("slaveId"),
  addrBase: $("addrBase"),
  timeoutMs: $("timeoutMs"),
  retryCount: $("retryCount"),
  pollMs: $("pollMs"),
  holdFilter: $("holdFilter"),
  inFilter: $("inFilter"),
  holdingBody: $("holdingTable").querySelector("tbody"),
  inputBody: $("inputTable").querySelector("tbody"),
};

let port = null;
let mb = null;
let pollTimer = null;
let logLines = [];
const holdingState = new Map();
const inputState = new Map();
const SETTINGS_KEY = "webserial_modbus_device_v2";

function num(id, fallback) {
  const value = Number($(id)?.value);
  return Number.isFinite(value) ? value : fallback;
}

function saveSettings() {
  try {
    localStorage.setItem(
      SETTINGS_KEY,
      JSON.stringify({
        baudrate: ui.baudrate.value,
        dataBits: ui.dataBits.value,
        parity: ui.parity.value,
        stopBits: ui.stopBits.value,
        slaveId: num("slaveId", 254),
        addrBase: ui.addrBase.value,
        timeoutMs: num("timeoutMs", 10000),
        retryCount: num("retryCount", 2),
        pollMs: num("pollMs", 800),
        verboseRaw: ui.verboseRaw.checked,
      })
    );
  } catch {}
}

function loadSettings() {
  try {
    const settings = JSON.parse(localStorage.getItem(SETTINGS_KEY) || "{}");
    if (settings.baudrate) ui.baudrate.value = settings.baudrate;
    if (settings.dataBits) ui.dataBits.value = settings.dataBits;
    if (settings.parity) ui.parity.value = settings.parity;
    if (settings.stopBits) ui.stopBits.value = settings.stopBits;
    if (settings.addrBase) ui.addrBase.value = settings.addrBase;
    if (Number.isFinite(settings.slaveId)) ui.slaveId.value = String(settings.slaveId);
    if (Number.isFinite(settings.timeoutMs)) ui.timeoutMs.value = String(Math.max(2000, settings.timeoutMs));
    if (Number.isFinite(settings.retryCount)) ui.retryCount.value = String(Math.min(10, Math.max(0, settings.retryCount)));
    if (Number.isFinite(settings.pollMs)) ui.pollMs.value = String(settings.pollMs);
    if (typeof settings.verboseRaw === "boolean") ui.verboseRaw.checked = settings.verboseRaw;
  } catch {}
}

function appendLog(line) {
  const ts = new Date().toLocaleTimeString();
  logLines.push(`[${ts}] ${line}`);
  if (logLines.length > 500) logLines = logLines.slice(-500);
  ui.serialLog.textContent = logLines.join("\n");
  ui.serialLog.scrollTop = ui.serialLog.scrollHeight;
}

function setStatus(kind, text) {
  ui.connStatus.classList.remove("ok", "bad");
  if (kind) ui.connStatus.classList.add(kind);
  ui.connStatus.textContent = text;
}

function hasWebSerial() {
  return "serial" in navigator;
}

function displayHoldingAddr(addr) {
  return ui.addrBase.value === "1" ? String(40001 + addr) : String(addr);
}

function displayInputAddr(addr) {
  return ui.addrBase.value === "1" ? String(30001 + addr) : String(addr);
}

function buildHoldingTable() {
  ui.holdingBody.innerHTML = "";
  for (const reg of holdingRegisters) {
    const row = document.createElement("tr");
    row.dataset.addr = String(reg.addr);
    row.dataset.filter = `${reg.addr} ${reg.name} ${reg.label}`.toLowerCase();
    row.innerHTML = `
      <td class="mono">${displayHoldingAddr(reg.addr)}</td>
      <td><div class="mono">${reg.name}</div><div>${reg.label}</div></td>
      <td></td>
      <td class="mono"></td>
    `;
    const input = document.createElement("input");
    input.type = "number";
    input.min = "0";
    input.max = "65535";
    input.step = "1";
    input.value = "0";
    input.addEventListener("input", () => {
      const state = holdingState.get(reg.addr);
      holdingState.set(reg.addr, { ...state, value: Number(input.value) & 0xffff, dirty: true });
      renderDirty(reg.addr);
    });
    row.children[2].appendChild(input);
    ui.holdingBody.appendChild(row);
    holdingState.set(reg.addr, { reg, value: 0, dirty: false });
  }
}

function buildInputTable() {
  ui.inputBody.innerHTML = "";
  for (const reg of inputRegisters) {
    const row = document.createElement("tr");
    row.dataset.addr = String(reg.addr);
    row.dataset.filter = `${reg.addr} ${reg.name} ${reg.label}`.toLowerCase();
    row.innerHTML = `
      <td class="mono">${displayInputAddr(reg.addr)}</td>
      <td><div class="mono">${reg.name}</div><div>${reg.label}</div></td>
      <td class="mono">-</td>
      <td class="mono">-</td>
    `;
    ui.inputBody.appendChild(row);
    inputState.set(reg.addr, { reg, value: null, updatedAt: null });
  }
}

function renderDirty(addr) {
  const row = ui.holdingBody.querySelector(`tr[data-addr="${addr}"]`);
  const state = holdingState.get(addr);
  if (!row || !state) return;
  row.children[3].textContent = state.dirty ? "YES" : "";
  row.children[3].classList.toggle("dirty", state.dirty);
}

function setHoldingValue(addr, value, dirty = false) {
  const state = holdingState.get(addr);
  const row = ui.holdingBody.querySelector(`tr[data-addr="${addr}"]`);
  if (!state || !row) return;
  holdingState.set(addr, { ...state, value, dirty });
  row.querySelector("input").value = String(value);
  renderDirty(addr);
}

function setInputValue(addr, value) {
  const row = ui.inputBody.querySelector(`tr[data-addr="${addr}"]`);
  if (!row) return;
  row.children[2].textContent = String(value);
  row.children[3].textContent = new Date().toLocaleTimeString();
}

function filterTable(body, query) {
  const q = query.trim().toLowerCase();
  for (const row of body.querySelectorAll("tr")) {
    row.style.display = !q || row.dataset.filter.includes(q) ? "" : "none";
  }
}

function groupRanges(addrs, maxCount = 125) {
  const sorted = Array.from(new Set(addrs)).sort((a, b) => a - b);
  const ranges = [];
  let i = 0;
  while (i < sorted.length) {
    const start = sorted[i];
    let end = start;
    i++;
    while (i < sorted.length && sorted[i] === end + 1 && sorted[i] - start + 1 <= maxCount) {
      end = sorted[i++];
    }
    ranges.push({ start, count: end - start + 1 });
  }
  return ranges;
}

function groupDirtyWrites(dirtyStates, maxCount = 120) {
  const sorted = dirtyStates.sort((a, b) => a.reg.addr - b.reg.addr);
  const groups = [];
  let group = null;
  for (const state of sorted) {
    const addr = state.reg.addr;
    if (!group || addr !== group.start + group.values.length || group.values.length >= maxCount) {
      group = { start: addr, values: [] };
      groups.push(group);
    }
    group.values.push(Number(state.value) & 0xffff);
  }
  return groups;
}

function protoAddr(addr) {
  return ui.addrBase.value === "1" ? addr - 1 : addr;
}

function syncMasterSettings() {
  if (!mb) return;
  mb.setSlaveId(num("slaveId", 254));
  mb.setTimeoutMs(num("timeoutMs", 10000));
  mb.setRetries(num("retryCount", 2));
  mb.setLogRaw(ui.verboseRaw.checked);
  mb.setSerialParams({
    baudRate: num("baudrate", 19200),
    dataBits: num("dataBits", 8),
    parity: ui.parity.value,
    stopBits: num("stopBits", 2),
  });
}

async function readHoldingAll() {
  if (!mb) return;
  ui.btnReadHolding.disabled = true;
  try {
    for (const { start, count } of groupRanges(holdingRegisters.map((reg) => reg.addr), 125)) {
      appendLog(`Reading holding ${start}..${start + count - 1}`);
      const regs = await mb.readHolding(protoAddr(start), count);
      regs.forEach((value, index) => {
        const addr = start + index;
        if (holdingState.has(addr)) setHoldingValue(addr, value, false);
      });
    }
    appendLog("Holding registers loaded");
  } catch (error) {
    appendLog(`Read holding failed: ${error.message}`);
  } finally {
    ui.btnReadHolding.disabled = false;
  }
}

async function saveHoldingDirty() {
  if (!mb) return;
  ui.btnSaveHolding.disabled = true;
  try {
    const dirty = Array.from(holdingState.values()).filter((state) => state.dirty);
    if (!dirty.length) {
      appendLog("No changes to save");
      return;
    }
    for (const { start, values } of groupDirtyWrites(dirty, 120)) {
      appendLog(`Writing holding ${start}..${start + values.length - 1}`);
      if (values.length === 1) await mb.writeSingleHolding(protoAddr(start), values[0]);
      else await mb.writeMultipleHolding(protoAddr(start), values);
      values.forEach((value, index) => setHoldingValue(start + index, value, false));
    }
    appendLog(`Saved ${dirty.length} holding register(s)`);
  } catch (error) {
    appendLog(`Save failed: ${error.message}`);
  } finally {
    ui.btnSaveHolding.disabled = false;
  }
}

async function pollInputOnce() {
  if (!mb) return;
  for (const { start, count } of groupRanges(inputRegisters.map((reg) => reg.addr), 125)) {
    appendLog(`Reading input ${start}..${start + count - 1}`);
    const regs = await mb.readInput(protoAddr(start), count);
    regs.forEach((value, index) => {
      const addr = start + index;
      if (inputState.has(addr)) setInputValue(addr, value);
    });
  }
}

function setConnectedUi(connected) {
  ui.btnConnect.disabled = connected;
  ui.btnDisconnect.disabled = !connected;
  ui.btnReadHolding.disabled = !connected;
  ui.btnSaveHolding.disabled = !connected;
  ui.btnPollToggle.disabled = !connected;
  ui.baudrate.disabled = connected;
  ui.dataBits.disabled = connected;
  ui.parity.disabled = connected;
  ui.stopBits.disabled = connected;
  ui.portSelect.disabled = connected;
  ui.btnRefreshPorts.disabled = connected;
  ui.btnAddPort.disabled = connected;
}

async function refreshPorts() {
  if (!hasWebSerial()) return;
  const ports = await navigator.serial.getPorts();
  ui.portSelect.innerHTML = `<option value="">${ports.length ? "Select..." : "No saved ports"}</option>`;
  ports.forEach((portItem, index) => {
    const option = document.createElement("option");
    option.value = String(index);
    option.textContent = `Port ${index + 1}`;
    ui.portSelect.appendChild(option);
  });
}

async function addPort() {
  await navigator.serial.requestPort();
  await refreshPorts();
}

async function selectedOrNewPort() {
  const ports = await navigator.serial.getPorts();
  const index = Number(ui.portSelect.value);
  if (Number.isFinite(index) && index >= 0 && index < ports.length) return ports[index];
  return navigator.serial.requestPort();
}

async function connect() {
  if (!hasWebSerial()) throw new Error("WebSerial not supported. Use Chrome or Edge desktop.");
  port = await selectedOrNewPort();
  appendLog("Opening port...");
  await port.open({
    baudRate: num("baudrate", 19200),
    dataBits: num("dataBits", 8),
    parity: ui.parity.value,
    stopBits: num("stopBits", 2),
    flowControl: "none",
  });
  mb = new ModbusRtuMaster({
    port,
    slaveId: num("slaveId", 254),
    timeoutMs: num("timeoutMs", 10000),
    retries: num("retryCount", 2),
    logRaw: ui.verboseRaw.checked,
    log: appendLog,
  });
  syncMasterSettings();
  await mb.open();
  setConnectedUi(true);
  setStatus("ok", "Connected");
  appendLog("Connected");
}

async function disconnect() {
  stopPoll();
  try {
    await mb?.close();
  } catch {}
  try {
    await port?.close();
  } catch {}
  mb = null;
  port = null;
  setConnectedUi(false);
  setStatus("", "Disconnected");
  appendLog("Disconnected");
}

function startPoll() {
  if (pollTimer) return;
  ui.btnPollToggle.textContent = "Stop Poll";
  const interval = Math.max(200, num("pollMs", 800));
  pollTimer = setInterval(() => pollInputOnce().catch((error) => appendLog(`Poll error: ${error.message}`)), interval);
  pollInputOnce().catch((error) => appendLog(`Poll error: ${error.message}`));
}

function stopPoll() {
  if (!pollTimer) return;
  clearInterval(pollTimer);
  pollTimer = null;
  ui.btnPollToggle.textContent = "Start Poll";
}

ui.btnShowDevice.addEventListener("click", () => ui.deviceDialog.showModal());
ui.btnConnect.addEventListener("click", () => connect().catch((error) => { setStatus("bad", error.message); appendLog(error.message); }));
ui.btnDisconnect.addEventListener("click", disconnect);
ui.btnRefreshPorts.addEventListener("click", () => refreshPorts().catch((error) => appendLog(`Refresh ports failed: ${error.message}`)));
ui.btnAddPort.addEventListener("click", () => addPort().catch((error) => appendLog(`Add port failed: ${error.message}`)));
ui.btnClearLog.addEventListener("click", () => { logLines = []; ui.serialLog.textContent = ""; });
ui.btnReadHolding.addEventListener("click", () => { syncMasterSettings(); saveSettings(); appendLog(`READ start slave=${ui.slaveId.value}`); readHoldingAll(); });
ui.btnSaveHolding.addEventListener("click", () => { syncMasterSettings(); saveSettings(); saveHoldingDirty(); });
ui.btnPollToggle.addEventListener("click", () => { syncMasterSettings(); saveSettings(); pollTimer ? stopPoll() : startPoll(); });
ui.holdFilter.addEventListener("input", () => filterTable(ui.holdingBody, ui.holdFilter.value));
ui.inFilter.addEventListener("input", () => filterTable(ui.inputBody, ui.inFilter.value));
ui.addrBase.addEventListener("change", () => {
  for (const row of ui.holdingBody.querySelectorAll("tr")) row.children[0].textContent = displayHoldingAddr(Number(row.dataset.addr));
  for (const row of ui.inputBody.querySelectorAll("tr")) row.children[0].textContent = displayInputAddr(Number(row.dataset.addr));
});

[ui.baudrate, ui.dataBits, ui.parity, ui.stopBits, ui.slaveId, ui.addrBase, ui.timeoutMs, ui.retryCount, ui.pollMs, ui.verboseRaw].forEach((item) => {
  item.addEventListener("change", saveSettings);
  item.addEventListener("input", saveSettings);
});

loadSettings();
buildHoldingTable();
buildInputTable();
setConnectedUi(false);
if (!hasWebSerial()) setStatus("bad", "WebSerial not supported");
refreshPorts().catch(() => {});
appendLog("Ready");
