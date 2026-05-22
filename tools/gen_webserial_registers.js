const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const headerPath = path.join(root, "App/Product/RegisterMap/Modbus/modbus_register_map.h");
const outPath = path.join(root, "tools/webserial_modbus/registers.js");

const lines = fs.readFileSync(headerPath, "utf8").split(/\r?\n/);
const defineRe = /^\s*#define\s+([A-Za-z0-9_]+)\s+([0-9]+)U?\b/;

let mode = null;
let remapStart = null;
let remapCount = null;
const holding = [];
const input = [];

function label(name) {
  return name.replace(/^MB_REG_/, "").replace(/^MB_IN_/, "").replace(/_/g, " ");
}

for (const line of lines) {
  if (line.includes("Holding Registers")) {
    mode = "holding";
    continue;
  }
  if (line.includes("Input Registers")) {
    mode = "input";
    continue;
  }

  const match = line.match(defineRe);
  if (!match || !mode) continue;

  const name = match[1];
  const addr = Number(match[2]);
  if (name.endsWith("_DEFAULT")) continue;

  if (mode === "holding") {
    if (name === "MB_REG_REMAPPING_ADDR_START") remapStart = addr;
    if (name === "MB_REG_REMAPPING_ADDR_COUNT") remapCount = addr;
    if (name.endsWith("_START") || name.endsWith("_COUNT") || name.endsWith("_END")) continue;
    holding.push({ name, label: label(name), addr });
  } else {
    input.push({ name, label: label(name), addr });
  }
}

if (remapStart !== null && remapCount !== null) {
  for (let index = 0; index < remapCount; index++) {
    holding.push({
      name: `MB_REG_REMAPPING_ADDR[${index}]`,
      label: `REMAPPING ADDR ${index}`,
      addr: remapStart + index,
    });
  }
}

holding.sort((a, b) => a.addr - b.addr || a.name.localeCompare(b.name));
input.sort((a, b) => a.addr - b.addr || a.name.localeCompare(b.name));

const output =
  "// Auto-generated from App/Product/RegisterMap/Modbus/modbus_register_map.h\n" +
  "export const holdingRegisters = " +
  JSON.stringify(holding, null, 2) +
  ";\n\n" +
  "export const inputRegisters = " +
  JSON.stringify(input, null, 2) +
  ";\n";

fs.writeFileSync(outPath, output);
console.log(`holding=${holding.length} input=${input.length}`);
