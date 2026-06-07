#!/usr/bin/env node
/**
 * ClaudeLight BLE 控制脚本
 * 用于向 ESP32 发送 BLE 指令控制灯效
 */

let noble;
try {
  noble = require("@abandonware/noble");
} catch (e) {
  console.error("请先安装依赖: npm install");
  process.exit(1);
}

const fs = require("fs");
const path = require("path");

// BLE 配置
const DEVICE_NAME = "ClaudeLight";
const SERVICE_UUID = "b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001";
const MODE_CHAR_UUID = "b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001";

// 有效模式列表
const VALID_MODES = [
  "demo",
  "thinking",
  "ai",
  "busy",
  "success",
  "error",
  "alarm",
  "traffic",
  "off",
  "red",
  "yellow",
  "green",
];

// 日志配置
const LOG_FILE = path.join(process.cwd(), "claude_light_ble.log");

function log(level, message) {
  const timestamp = new Date().toISOString();
  const line = `${timestamp} [${level}] ${message}`;
  try {
    fs.appendFileSync(LOG_FILE, line + "\n", "utf-8");
  } catch (e) {
    /* 忽略日志写入失败 */
  }

  if (level === "ERROR") {
    console.error(`[${level}] ${message}`);
  } else {
    console.log(`[${level}] ${message}`);
  }
}

// =====================================================
// BLE 工具函数
// =====================================================

const SERVICE_UUID_SHORT = SERVICE_UUID.replace(/-/g, "").toLowerCase();

function matchesDevice(peripheral) {
  const name = peripheral.advertisement.localName;
  if (name && name.includes(DEVICE_NAME)) return true;

  const serviceUuids = peripheral.advertisement.serviceUuids || [];
  return serviceUuids.some(
    (uuid) => uuid.replace(/-/g, "").toLowerCase() === SERVICE_UUID_SHORT,
  );
}

function waitForPoweredOn(timeout = 10000) {
  return new Promise((resolve, reject) => {
    if (noble.state === "poweredOn") {
      return resolve();
    }

    const timer = setTimeout(() => {
      reject(new Error("BLE 适配器未就绪 (超时)"));
    }, timeout);

    noble.on("stateChange", (state) => {
      if (state === "poweredOn") {
        clearTimeout(timer);
        resolve();
      }
    });
  });
}

function findDevice(timeout = 20000) {
  return new Promise(async (resolve, reject) => {
    try {
      await waitForPoweredOn();
    } catch (e) {
      return reject(e);
    }

    log("INFO", `正在扫描 BLE 设备: ${DEVICE_NAME}...`);

    let found = false;

    const timer = setTimeout(() => {
      if (!found) {
        noble.stopScanning();
        noble.removeListener("discover", onDiscover);
        log("ERROR", `未找到 ${DEVICE_NAME} 设备`);
        resolve(null);
      }
    }, timeout);

    function onDiscover(peripheral) {
      if (found) return;

      if (matchesDevice(peripheral)) {
        found = true;
        clearTimeout(timer);
        noble.stopScanning();
        noble.removeListener("discover", onDiscover);
        const name = peripheral.advertisement.localName || DEVICE_NAME;
        log("INFO", `找到设备: ${name} (${peripheral.address})`);
        resolve(peripheral);
      }
    }

    noble.on("discover", onDiscover);
    noble.startScanning([], true); // allowDuplicates=true，捕获 scan response 中的名称
  });
}

function connectAndWrite(peripheral, data) {
  return new Promise((resolve, reject) => {
    peripheral.connect((error) => {
      if (error) return reject(error);

      log("INFO", `已连接到 ${peripheral.advertisement.localName}`);

      peripheral.discoverSomeServicesAndCharacteristics(
        [SERVICE_UUID],
        [MODE_CHAR_UUID],
        (error, services, characteristics) => {
          if (error) {
            peripheral.disconnect();
            return reject(error);
          }

          if (characteristics.length === 0) {
            peripheral.disconnect();
            return reject(new Error("Characteristic 未找到"));
          }

          const char = characteristics[0];
          char.write(Buffer.from(data), false, (error) => {
            peripheral.disconnect();
            if (error) return reject(error);
            resolve(true);
          });
        },
      );
    });
  });
}

function connectAndRead(peripheral) {
  return new Promise((resolve, reject) => {
    peripheral.connect((error) => {
      if (error) return reject(error);

      log("INFO", `已连接到 ${peripheral.advertisement.localName}`);

      peripheral.discoverSomeServicesAndCharacteristics(
        [SERVICE_UUID],
        [MODE_CHAR_UUID],
        (error, services, characteristics) => {
          if (error) {
            peripheral.disconnect();
            return reject(error);
          }

          if (characteristics.length === 0) {
            peripheral.disconnect();
            return reject(new Error("Characteristic 未找到"));
          }

          const char = characteristics[0];
          char.read((error, data) => {
            peripheral.disconnect();
            if (error) return reject(error);
            resolve(data.toString("utf-8").trim());
          });
        },
      );
    });
  });
}

// =====================================================
// 业务函数
// =====================================================

async function sendMode(mode) {
  if (!VALID_MODES.includes(mode)) {
    log("ERROR", `无效模式: ${mode}`);
    log("INFO", `有效模式: ${VALID_MODES.join(", ")}`);
    return false;
  }

  const device = await findDevice();
  if (!device) return false;

  try {
    await connectAndWrite(device, mode);
    log("INFO", `已发送模式: ${mode}`);
    log("INFO", "指令发送成功");
    return true;
  } catch (e) {
    log("ERROR", `BLE 通信失败: ${e.message}`);
    return false;
  }
}

async function readMode() {
  const device = await findDevice();
  if (!device) return null;

  try {
    const mode = await connectAndRead(device);
    log("INFO", `当前模式: ${mode}`);
    return mode;
  } catch (e) {
    log("ERROR", `读取失败: ${e.message}`);
    return null;
  }
}

// =====================================================
// CLI
// =====================================================

function printUsage() {
  console.log("ClaudeLight BLE 控制脚本");
  console.log();
  console.log("用法:");
  console.log(`  node ${path.basename(process.argv[1])} <mode>`);
  console.log(`  node ${path.basename(process.argv[1])} --read`);
  console.log(`  node ${path.basename(process.argv[1])} --scan`);
  console.log();
  console.log("可用模式:");
  for (const mode of VALID_MODES) {
    console.log(`  - ${mode}`);
  }
  console.log();
  console.log("示例:");
  console.log(`  node ${path.basename(process.argv[1])} thinking`);
  console.log(`  node ${path.basename(process.argv[1])} success`);
  console.log(`  node ${path.basename(process.argv[1])} --read`);
  console.log(`  node ${path.basename(process.argv[1])} --scan`);
}

async function scanAll() {
  try {
    await waitForPoweredOn();
  } catch (e) {
    console.error(`BLE 适配器未就绪: ${e.message}`);
    process.exit(1);
  }

  console.log("扫描所有 BLE 设备 (5秒)...");
  console.log();

  const devices = new Map();

  function onDiscover(peripheral) {
    const key = peripheral.address;
    if (!devices.has(key)) {
      devices.set(key, peripheral);
    }
  }

  noble.on("discover", onDiscover);
  noble.startScanning([], true);

  await new Promise((r) => setTimeout(r, 5000));

  noble.stopScanning();
  noble.removeListener("discover", onDiscover);

  console.log(`共发现 ${devices.size} 个设备:\n`);

  for (const [addr, p] of devices) {
    const name = p.advertisement.localName || "(未命名)";
    const svc = (p.advertisement.serviceUuids || []).join(", ");
    const match = matchesDevice(p) ? " <-- MATCH!" : "";
    console.log(
      `  ${addr}  ${name}${svc ? "  [服务: " + svc + "]" : ""}${match}`,
    );
  }
}

async function main() {
  if (process.argv.length < 3) {
    printUsage();
    process.exit(1);
  }

  const arg = process.argv[2].toLowerCase();

  if (arg === "--help" || arg === "-h") {
    printUsage();
    process.exit(0);
  }

  if (arg === "--read") {
    const result = await readMode();
    if (result === null) process.exit(1);
  } else if (arg === "--scan") {
    await scanAll();
  } else {
    const result = await sendMode(arg);
    process.exit(1);
  }
}

main();
