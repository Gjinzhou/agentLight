#!/usr/bin/env node
/**
 * ClaudeLight Hook 集成脚本
 * 用于 Claude Code 的 hooks 系统，自动将 Agent 状态映射到灯效
 */

let noble;
try {
  noble = require('@abandonware/noble');
} catch (e) {
  // 静默退出，不影响 Claude Code 正常运行
  process.exit(0);
}

const fs = require('fs');
const path = require('path');
const os = require('os');

// BLE 配置
const DEVICE_NAME = 'ClaudeLight';
const SERVICE_UUID = 'b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001';
const MODE_CHAR_UUID = 'b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001';

// 日志文件
const LOG_DIR = path.join(os.homedir(), '.claude', 'claude-light');
const LOG_FILE = path.join(LOG_DIR, 'claude_light.log');

// 状态映射: Claude Code Hook 事件 -> 灯效模式
const EVENT_MODE_MAP = {
  // Agent 开始工作 - 黄灯慢闪
  PreToolUse: 'busy',
  // Agent 完成工具调用 - 绿灯常亮
  PostToolUse: 'success',
  // 需要用户确认权限 - 红灯快闪
  Notification: 'alarm',
  // Agent 停止工作 - 绿灯常亮
  Stop: 'success',
  // 子 Agent 停止 - 绿灯常亮
  SubagentStop: 'success',
};

// 特殊通知类型映射
const NOTIFICATION_MODE_MAP = {
  permission: 'alarm',   // 需要权限确认
  error: 'error',        // 错误
  warning: 'yellow',     // 警告
  info: 'thinking',      // 信息
};

// 防抖配置
const DEBOUNCE_MS = 500;
const lastEventTime = {};
let lastMode = null;

// =====================================================
// 日志
// =====================================================

function log(level, message) {
  const timestamp = new Date().toISOString();
  const line = `${timestamp} [${level}] ${message}`;
  try {
    if (!fs.existsSync(LOG_DIR)) {
      fs.mkdirSync(LOG_DIR, { recursive: true });
    }
    fs.appendFileSync(LOG_FILE, line + '\n', 'utf-8');
  } catch (e) { /* 忽略 */ }
}

// =====================================================
// BLE 工具函数
// =====================================================

const SERVICE_UUID_SHORT = SERVICE_UUID.replace(/-/g, '').toLowerCase();

function matchesDevice(peripheral) {
  const name = peripheral.advertisement.localName;
  if (name && name.includes(DEVICE_NAME)) return true;

  const serviceUuids = peripheral.advertisement.serviceUuids || [];
  return serviceUuids.some(uuid =>
    uuid.replace(/-/g, '').toLowerCase() === SERVICE_UUID_SHORT
  );
}

function waitForPoweredOn(timeout = 10000) {
  return new Promise((resolve, reject) => {
    if (noble.state === 'poweredOn') {
      return resolve();
    }

    const timer = setTimeout(() => {
      reject(new Error('BLE 适配器未就绪'));
    }, timeout);

    noble.on('stateChange', (state) => {
      if (state === 'poweredOn') {
        clearTimeout(timer);
        resolve();
      }
    });
  });
}

function findDevice(timeout = 3000) {
  return new Promise(async (resolve, reject) => {
    try {
      await waitForPoweredOn();
    } catch (e) {
      return reject(e);
    }

    let found = false;

    const timer = setTimeout(() => {
      if (!found) {
        noble.stopScanning();
        noble.removeListener('discover', onDiscover);
        resolve(null);
      }
    }, timeout);

    function onDiscover(peripheral) {
      if (found) return;

      if (matchesDevice(peripheral)) {
        found = true;
        clearTimeout(timer);
        noble.stopScanning();
        noble.removeListener('discover', onDiscover);
        resolve(peripheral);
      }
    }

    noble.on('discover', onDiscover);
    noble.startScanning([], true);  // allowDuplicates=true，捕获 scan response
  });
}

async function sendMode(mode) {
  // 防抖：相同模式不重复发送
  if (mode === lastMode) return true;

  try {
    const device = await findDevice();
    if (!device) return false;

    return new Promise((resolve, reject) => {
      device.connect((error) => {
        if (error) return reject(error);

        device.discoverSomeServicesAndCharacteristics(
          [SERVICE_UUID],
          [MODE_CHAR_UUID],
          (error, services, characteristics) => {
            if (error) {
              device.disconnect();
              return reject(error);
            }

            if (characteristics.length === 0) {
              device.disconnect();
              return reject(new Error('Characteristic 未找到'));
            }

            const char = characteristics[0];
            char.write(Buffer.from(mode), false, (error) => {
              device.disconnect();
              if (error) return reject(error);
              lastMode = mode;
              log('INFO', `已发送模式: ${mode}`);
              resolve(true);
            });
          }
        );
      });
    });
  } catch (e) {
    log('DEBUG', `BLE 发送失败: ${e.message}`);
    return false;
  }
}

// =====================================================
// Hook 逻辑
// =====================================================

function shouldProcessEvent(eventType) {
  const now = Date.now();
  const lastTime = lastEventTime[eventType];

  if (lastTime !== undefined && (now - lastTime) < DEBOUNCE_MS) {
    return false;
  }

  lastEventTime[eventType] = now;
  return true;
}

function determineMode(hookData) {
  const eventType = hookData.event_type || '';

  // 通知事件特殊处理
  if (eventType === 'Notification') {
    const notification = hookData.notification || {};
    const notifType = notification.type || '';
    return NOTIFICATION_MODE_MAP[notifType] || 'alarm';
  }

  // 默认映射
  return EVENT_MODE_MAP[eventType] || 'thinking';
}

function readStdin() {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf-8');
    process.stdin.on('data', (chunk) => {
      data += chunk;
    });
    process.stdin.on('end', () => {
      resolve(data);
    });

    // 如果 stdin 是 TTY（无管道输入），直接返回空
    if (process.stdin.isTTY) {
      resolve('');
    }
  });
}

async function main() {
  try {
    const input = await readStdin();

    if (!input.trim()) {
      process.exit(0);
    }

    let hookData;
    try {
      hookData = JSON.parse(input);
    } catch (e) {
      log('ERROR', `JSON 解析失败: ${input.slice(0, 100)}`);
      process.exit(0);
    }

    const eventType = hookData.event_type || 'unknown';
    log('INFO', `收到事件: ${eventType}`);

    // 防抖检查
    if (!shouldProcessEvent(eventType)) {
      log('DEBUG', `事件被防抖过滤: ${eventType}`);
      process.exit(0);
    }

    // 确定模式
    const mode = determineMode(hookData);
    log('INFO', `映射模式: ${mode}`);

    // 发送 BLE 指令
    const result = await sendMode(mode);

    if (result) {
      log('INFO', '灯效更新成功');
    } else {
      log('WARNING', '灯效更新失败');
    }
  } catch (e) {
    log('ERROR', `Hook 处理异常: ${e.message}`);
  }

  process.exit(0);
}

main();
