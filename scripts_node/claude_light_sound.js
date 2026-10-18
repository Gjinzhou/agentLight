#!/usr/bin/env node
/**
 * ClaudeLight 音效管理脚本 (Node.js / noble)
 *
 * 子命令:
 *   sounds list                读取机内全部音效并格式化展示
 *   sounds play <id>           立即试播指定音效
 *   sounds upload <file.json>  用新音效库覆盖机内存储
 *   storage                    打印存储空间状态
 *   volume <0-100>             设置蜂鸣器音量上限
 *   mute <on|off>              音效总开关
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

const DEVICE_NAME = "ClaudeLight";
const SERVICE_UUID = "b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001";
const MODE_CHAR_UUID = "b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001";
const SETTING_CHAR_UUID = "b8b7e003-7a6b-4f4f-9a8b-11c0ffee0001";
const SOUND_CHAR_UUID = "b8b7e005-7a6b-4f4f-9a8b-11c0ffee0001";
const UPLOAD_CHAR_UUID = "b8b7e006-7a6b-4f4f-9a8b-11c0ffee0001";
const STORAGE_CHAR_UUID = "b8b7e007-7a6b-4f4f-9a8b-11c0ffee0001";

const SERVICE_UUID_SHORT = SERVICE_UUID.replace(/-/g, "").toLowerCase();

function matchesDevice(peripheral) {
  const name = peripheral.advertisement.localName;
  if (name && name.includes(DEVICE_NAME)) return true;
  const serviceUuids = peripheral.advertisement.serviceUuids || [];
  return serviceUuids.some(
    (u) => u.replace(/-/g, "").toLowerCase() === SERVICE_UUID_SHORT,
  );
}

function waitForPoweredOn(timeout = 10000) {
  return new Promise((resolve, reject) => {
    if (noble.state === "poweredOn") return resolve();
    const timer = setTimeout(
      () => reject(new Error("BLE 适配器未就绪 (超时)")),
      timeout,
    );
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
    console.log(`正在扫描 BLE 设备: ${DEVICE_NAME}...`);
    let found = false;
    const timer = setTimeout(() => {
      if (!found) {
        noble.stopScanning();
        noble.removeListener("discover", onDiscover);
        reject(new Error("未找到设备"));
      }
    }, timeout);
    function onDiscover(peripheral) {
      if (found) return;
      if (matchesDevice(peripheral)) {
        found = true;
        clearTimeout(timer);
        noble.stopScanning();
        noble.removeListener("discover", onDiscover);
        console.log(`找到设备: ${peripheral.address}`);
        resolve(peripheral);
      }
    }
    noble.on("discover", onDiscover);
    noble.startScanning([], true);
  });
}

function discover(peripheral, charUuids) {
  return new Promise((resolve, reject) => {
    peripheral.discoverSomeServicesAndCharacteristics(
      [SERVICE_UUID],
      charUuids,
      (error, services, characteristics) => {
        if (error) return reject(error);
        const map = {};
        charUuids.forEach((u, i) => (map[u] = characteristics[i]));
        resolve(map);
      },
    );
  });
}

const pconnect = (p) => new Promise((r, j) => p.connect((e) => (e ? j(e) : r())));
const pdisconnect = (p) =>
  new Promise((r) => p.disconnect(() => r()));
const psubscribe = (c) =>
  new Promise((r, j) => c.subscribe((e) => (e ? j(e) : r())));
const punsubscribe = (c) => new Promise((r) => c.unsubscribe(() => r()));
const pwrite = (c, buf) =>
  new Promise((r, j) => c.write(buf, false, (e) => (e ? j(e) : r())));
const pread = (c) => new Promise((r, j) => c.read((e, d) => (e ? j(e) : r(d))));

// 写一条命令并等待下一条 result 通知（UPLOAD 特征用）
function writeReq(char, obj, timeout = 5000) {
  return new Promise((resolve, reject) => {
    const to = setTimeout(() => reject(new Error("等待设备响应超时")), timeout);
    char.once("data", (d) => {
      clearTimeout(to);
      try {
        resolve(JSON.parse(d.toString("utf-8")));
      } catch (e) {
        reject(e);
      }
    });
    pwrite(char, Buffer.from(JSON.stringify(obj))).catch((e) => {
      clearTimeout(to);
      reject(e);
    });
  });
}

// 等待完整音效 JSON（SOUND 特征的二进制分帧 notify）
function requestList(soundChar, timeout = 15000) {
  return new Promise((resolve, reject) => {
    const chunks = new Map();
    let total = null;
    const to = setTimeout(
      () => reject(new Error("读取音效列表超时")),
      timeout,
    );
    function onData(d) {
      // 帧格式: [0x01, seq, total, ...rawJson]
      if (d.length >= 3 && d[0] === 0x01) {
        const seq = d[1];
        total = d[2];
        chunks.set(seq, d.slice(3));
        if (chunks.size === total) {
          clearTimeout(to);
          soundChar.removeListener("data", onData);
          const parts = [];
          for (let i = 0; i < total; i++) parts.push(chunks.get(i));
          resolve(Buffer.concat(parts).toString("utf-8"));
        }
      }
    }
    soundChar.on("data", onData);
    pwrite(soundChar, Buffer.from(JSON.stringify({ op: "list" }))).catch(
      (e) => {
        clearTimeout(to);
        reject(e);
      },
    );
  });
}

async function cmdSoundsList() {
  const p = await findDevice();
  await pconnect(p);
  const chars = await discover(p, [SOUND_CHAR_UUID]);
  const soundChar = chars[SOUND_CHAR_UUID];
  if (!soundChar) throw new Error("SOUND 特征未找到");
  await psubscribe(soundChar);
  const raw = await requestList(soundChar);
  await punsubscribe(soundChar);
  await pdisconnect(p);

  let obj;
  try {
    obj = JSON.parse(raw);
  } catch (e) {
    console.error("返回内容非合法 JSON:\n" + raw);
    return;
  }
  console.log("\n===== 机内音效库 =====");
  console.log(`version: ${obj.version}, 音效数: ${obj.sounds.length}\n`);
  for (const s of obj.sounds) {
    const totalMs = s.notes.reduce((a, n) => a + (n[1] || 0), 0);
    console.log(
      `  ${String(s.id).padEnd(12)} ${String(s.name || "").padEnd(16)} ` +
        `音符 ${String(s.notes.length).padStart(3)}  时长 ${String(totalMs).padStart(5)}ms`,
    );
  }
  console.log("\n完整 JSON:\n" + JSON.stringify(obj, null, 0));
}

async function cmdSoundsPlay(id) {
  if (!id) {
    console.error("用法: sounds play <id>");
    process.exit(1);
  }
  const p = await findDevice();
  await pconnect(p);
  const chars = await discover(p, [SOUND_CHAR_UUID]);
  await pwrite(
    chars[SOUND_CHAR_UUID],
    Buffer.from(JSON.stringify({ op: "play", id })),
  );
  console.log(`已发送试播: ${id}`);
  await new Promise((r) => setTimeout(r, 300));
  await pdisconnect(p);
}

async function cmdSoundsUpload(file) {
  if (!file || !fs.existsSync(file)) {
    console.error(`找不到文件: ${file || "(未指定)"}`);
    console.error("用法: sounds upload <file.json>");
    process.exit(1);
  }
  const text = fs.readFileSync(file, "utf-8");
  let obj;
  try {
    obj = JSON.parse(text);
  } catch (e) {
    console.error(`本地 JSON 解析失败: ${e.message}`);
    process.exit(1);
  }
  if (!obj.sounds || !Array.isArray(obj.sounds)) {
    console.error("缺少 sounds 数组");
    process.exit(1);
  }
  // 本地基础校验 + 压缩
  const compact = JSON.stringify(obj);
  const BUDGET = 8192;
  if (Buffer.byteLength(compact, "utf-8") > BUDGET) {
    console.error(`音效库超出预算: ${Buffer.byteLength(compact)} > ${BUDGET} 字节`);
    process.exit(1);
  }

  const p = await findDevice();
  await pconnect(p);
  const chars = await discover(p, [UPLOAD_CHAR_UUID]);
  const up = chars[UPLOAD_CHAR_UUID];
  await psubscribe(up);

  let r = await writeReq(up, { op: "begin", size: Buffer.byteLength(compact) });
  if (!r.ok) throw new Error("begin 失败: " + r.msg);
  console.log("begin ok, 开始分块上传...");

  const CS = 60; // 每块原始字节数（转义后仍在 MTU 内）
  let idx = 0;
  for (let pos = 0; pos < compact.length; pos += CS) {
    const piece = compact.slice(pos, pos + CS);
    r = await writeReq(up, { op: "data", i: idx, data: piece });
    if (!r.ok) throw new Error(`data #${idx} 失败: ${r.msg}`);
    idx++;
    process.stdout.write(`  chunk ${idx} 已确认\r`);
  }
  console.log();

  r = await writeReq(up, { op: "commit" });
  await punsubscribe(up);
  await pdisconnect(p);

  if (r.ok) {
    console.log(`上传成功: ${idx} 块, ${compact.length} 字节已写入并生效。`);
  } else {
    console.error(`commit 失败: ${r.msg} (旧库保持不变)`);
  }
}

async function cmdStorage() {
  const p = await findDevice();
  await pconnect(p);
  const chars = await discover(p, [STORAGE_CHAR_UUID]);
  const st = chars[STORAGE_CHAR_UUID];
  const data = await pread(st);
  await pdisconnect(p);
  const obj = JSON.parse(data.toString("utf-8"));
  console.log("\n===== 存储空间状态 =====");
  console.log(`  NVS 分区总量 : ${obj.partitionTotal} 字节`);
  console.log(`  音效区预算   : ${obj.budget} 字节`);
  console.log(`  已用         : ${obj.used} 字节`);
  console.log(`  剩余         : ${obj.free} 字节`);
  console.log(`  分块数       : ${obj.chunks}`);
  console.log(`  音效数量     : ${obj.count}`);
}

async function cmdSetting(obj) {
  const p = await findDevice();
  await pconnect(p);
  const chars = await discover(p, [SETTING_CHAR_UUID]);
  await pwrite(chars[SETTING_CHAR_UUID], Buffer.from(JSON.stringify(obj)));
  await new Promise((r) => setTimeout(r, 300));
  await pdisconnect(p);
  console.log(`已下发设置: ${JSON.stringify(obj)}`);
}

function printUsage() {
  const b = path.basename(process.argv[1]);
  console.log("ClaudeLight 音效管理脚本");
  console.log();
  console.log("用法:");
  console.log(`  node ${b} sounds list`);
  console.log(`  node ${b} sounds play <id>`);
  console.log(`  node ${b} sounds upload <file.json>`);
  console.log(`  node ${b} storage`);
  console.log(`  node ${b} volume <0-100>`);
  console.log(`  node ${b} mute <on|off>`);
  console.log();
  console.log("示例:");
  console.log(`  node ${b} sounds play coin`);
  console.log(`  node ${b} sounds upload my_sounds.json`);
  console.log(`  node ${b} volume 70`);
}

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 0 || args[0] === "--help" || args[0] === "-h") {
    printUsage();
    process.exit(args.length ? 0 : 1);
  }

  const cmd = args[0].toLowerCase();
  try {
    if (cmd === "sounds") {
      const sub = (args[1] || "").toLowerCase();
      if (sub === "list") await cmdSoundsList();
      else if (sub === "play") await cmdSoundsPlay(args[2]);
      else if (sub === "upload") await cmdSoundsUpload(args[2]);
      else {
        printUsage();
        process.exit(1);
      }
    } else if (cmd === "storage") {
      await cmdStorage();
    } else if (cmd === "volume") {
      const v = parseInt(args[1], 10);
      if (isNaN(v) || v < 0 || v > 100) {
        console.error("音量范围 0-100");
        process.exit(1);
      }
      await cmdSetting({ buzzerMax: v });
    } else if (cmd === "mute") {
      const on = (args[1] || "").toLowerCase();
      if (on !== "on" && on !== "off") {
        console.error("用法: mute <on|off>");
        process.exit(1);
      }
      // soundEnabled: off -> false, on -> true
      await cmdSetting({ soundEnabled: on === "on" });
    } else {
      printUsage();
      process.exit(1);
    }
  } catch (e) {
    console.error(`错误: ${e.message}`);
    process.exit(1);
  }
  process.exit(0);
}

main();
