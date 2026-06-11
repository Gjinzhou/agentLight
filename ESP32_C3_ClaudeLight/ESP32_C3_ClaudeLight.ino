#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =====================================================
// ESP32 SuperMini + ClaudeLight: BLE 蓝牙控制版
//
// 接线方式（共阴模块 G Y R GND）：
// ESP32 GND   -> 模块 GND
// ESP32 IO0   -> 220Ω -> 模块 G = 绿灯
// ESP32 IO1   -> 220Ω -> 模块 Y = 黄灯
// ESP32 IO2   -> 220Ω -> 模块 R = 红灯
//
// 共阴模块：GPIO HIGH = 灯亮，GPIO LOW = 灯灭。
// 注意：GPIO0/GPIO2 为 strapping pin，启动瞬间需确保不受外部强拉。
//       使用 USB CDC On Boot 模式时 GPIO1 可作为普通 IO 使用。
// 默认开机模式：demo
// =====================================================

const char* BLE_DEVICE_NAME = "ClaudeLight";

#define SERVICE_UUID        "b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001"
#define MODE_CHAR_UUID      "b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001"

const int GREEN_PIN = 0;
const int YELLOW_PIN = 1;
const int RED_PIN = 2;

const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;

const int RED_MAX = 255;
const int YELLOW_MAX = 220;
const int GREEN_MAX = 220;

const unsigned long NORMAL_MODE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
const unsigned long TRAFFIC_MODE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

String currentMode = "demo";
unsigned long modeStart = 0;

BLEServer* pServer = nullptr;
BLECharacteristic* pModeCharacteristic = nullptr;
bool deviceConnected = false;

// =====================================================
// 基础工具函数
// =====================================================

void writeLed(int pin, int value) {
  value = constrain(value, 0, 255);
  ledcWrite(pin, value);  // 共阴模块：直接输出，无需反转
}

void allOff() {
  writeLed(RED_PIN, 0);
  writeLed(YELLOW_PIN, 0);
  writeLed(GREEN_PIN, 0);
}

void setOnly(int red, int yellow, int green) {
  writeLed(RED_PIN, constrain(red, 0, RED_MAX));
  writeLed(YELLOW_PIN, constrain(yellow, 0, YELLOW_MAX));
  writeLed(GREEN_PIN, constrain(green, 0, GREEN_MAX));
}

int triWave(unsigned long t, unsigned long period, int maxValue) {
  unsigned long x = t % period;
  if (x < period / 2) {
    return map(x, 0, period / 2, 0, maxValue);
  } else {
    return map(x, period / 2, period, maxValue, 0);
  }
}

int fadeInOutBrightness(
  unsigned long t,
  unsigned long fadeIn,
  unsigned long hold,
  unsigned long fadeOut,
  unsigned long offTime,
  int maxValue
) {
  unsigned long total = fadeIn + hold + fadeOut + offTime;
  unsigned long x = t % total;

  if (x < fadeIn) {
    return map(x, 0, fadeIn, 0, maxValue);
  }

  x -= fadeIn;
  if (x < hold) {
    return maxValue;
  }

  x -= hold;
  if (x < fadeOut) {
    return map(x, 0, fadeOut, maxValue, 0);
  }

  return 0;
}

void fadeToStatic(int targetRed, int targetYellow, int targetGreen, int fadeMs = 80) {
  allOff();
  int steps = 12;
  int delayPerStep = max(1, fadeMs / steps);

  for (int i = 0; i <= steps; i++) {
    float p = (float)i / steps;
    setOnly(targetRed * p, targetYellow * p, targetGreen * p);
    delay(delayPerStep);
  }
}

// =====================================================
// 模式处理
// =====================================================

bool isValidMode(String mode) {
  return (
    mode == "red" ||
    mode == "yellow" ||
    mode == "green" ||
    mode == "busy" ||
    mode == "error" ||
    mode == "thinking" ||
    mode == "ai" ||
    mode == "success" ||
    mode == "traffic" ||
    mode == "alarm" ||
    mode == "demo" ||
    mode == "off"
  );
}

void notifyMode() {
  if (pModeCharacteristic) {
    pModeCharacteristic->setValue(currentMode.c_str());
    if (deviceConnected) {
      pModeCharacteristic->notify();
    }
  }
}

void setMode(String mode) {
  mode.trim();
  mode.toLowerCase();

  if (!isValidMode(mode)) {
    Serial.print("Unknown mode: ");
    Serial.println(mode);
    return;
  }

  currentMode = mode;
  modeStart = millis();

  Serial.print("Mode changed to: ");
  Serial.println(currentMode);

  if (mode == "red") {
    fadeToStatic(RED_MAX, 0, 0, 80);
  } else if (mode == "yellow") {
    fadeToStatic(0, YELLOW_MAX, 0, 80);
  } else if (mode == "green") {
    fadeToStatic(0, 0, GREEN_MAX, 80);
  } else if (mode == "success") {
    setOnly(0, 0, GREEN_MAX);
  } else if (mode == "off") {
    allOff();
  }

  notifyMode();
}

void autoTimeoutCheck() {
  unsigned long elapsed = millis() - modeStart;

  if (currentMode == "off") {
    return;
  }

  if (currentMode == "traffic") {
    if (elapsed >= TRAFFIC_MODE_TIMEOUT_MS) {
      Serial.println("Traffic timeout -> off");
      setMode("off");
    }
    return;
  }

  if (elapsed >= NORMAL_MODE_TIMEOUT_MS) {
    Serial.println("Normal mode timeout -> traffic");
    setMode("traffic");
  }
}

// =====================================================
// 灯效模式
// =====================================================

void updateBusy() {
  unsigned long t = millis() - modeStart;
  int y = fadeInOutBrightness(t, 80, 500, 120, 500, YELLOW_MAX);
  setOnly(0, y, 0);
}

void updateError() {
  unsigned long t = millis() - modeStart;
  int r = fadeInOutBrightness(t, 40, 180, 80, 180, RED_MAX);
  setOnly(r, 0, 0);
}

void updateThinking() {
  unsigned long t = millis() - modeStart;
  const unsigned long period = 1050;
  unsigned long x = t % period;

  int g = 0;
  int y = 0;
  int r = 0;

  if (x < 350) {
    g = map(x, 0, 350, GREEN_MAX, 70);
    y = map(x, 0, 350, 20, YELLOW_MAX);
    r = 0;
  } else if (x < 700) {
    unsigned long p = x - 350;
    g = map(p, 0, 350, 70, 0);
    y = map(p, 0, 350, YELLOW_MAX, 70);
    r = map(p, 0, 350, 20, RED_MAX);
  } else {
    unsigned long p = x - 700;
    g = map(p, 0, 350, 20, GREEN_MAX);
    y = map(p, 0, 350, 70, 0);
    r = map(p, 0, 350, RED_MAX, 70);
  }

  setOnly(r, y, g);
}

void updateAi() {
  unsigned long t = millis() - modeStart;
  const unsigned long period = 1800;
  unsigned long x = t % period;

  unsigned long gx = (x + 0) % period;
  unsigned long yx = (x + period / 3) % period;
  unsigned long rx = (x + 2 * period / 3) % period;

  int g = triWave(gx, period, 150);
  int y = triWave(yx, period, 140);
  int r = triWave(rx, period, 170);

  setOnly(r, y, g);
}

void updateSuccess() {
  setOnly(0, 0, GREEN_MAX);
}

void updateAlarm() {
  unsigned long t = millis() - modeStart;
  const unsigned long phaseMs = 260;
  int phase = (t / phaseMs) % 2;
  unsigned long inside = t % phaseMs;

  int brightness;
  if (inside < 60) {
    brightness = map(inside, 0, 60, 0, 255);
  } else if (inside < 180) {
    brightness = 255;
  } else {
    brightness = map(inside, 180, phaseMs, 255, 0);
  }

  if (phase == 0) {
    setOnly(brightness, 0, 0);
  } else {
    setOnly(0, min(brightness, YELLOW_MAX), 0);
  }
}

void updateTraffic() {
  unsigned long t = (millis() - modeStart) % 15000;

  if (t < 5000) {
    setOnly(RED_MAX, 0, 0);
  } else if (t < 6500) {
    unsigned long phase = (t - 5000) % 500;
    int r = 0;
    if (phase < 60) r = map(phase, 0, 60, 0, RED_MAX);
    else if (phase < 230) r = RED_MAX;
    else if (phase < 320) r = map(phase, 230, 320, RED_MAX, 0);
    else r = 0;
    setOnly(r, 0, 0);
  } else if (t < 11500) {
    setOnly(0, 0, GREEN_MAX);
  } else if (t < 13000) {
    unsigned long phase = (t - 11500) % 500;
    int g = 0;
    if (phase < 60) g = map(phase, 0, 60, 0, GREEN_MAX);
    else if (phase < 230) g = GREEN_MAX;
    else if (phase < 320) g = map(phase, 230, 320, GREEN_MAX, 0);
    else g = 0;
    setOnly(0, 0, g);
  } else {
    setOnly(0, YELLOW_MAX, 0);
  }
}

void updateDemo() {
  unsigned long t = (millis() - modeStart) % 16000;

  if (t < 1200) {
    int g = triWave(t, 1200, GREEN_MAX);
    setOnly(0, 0, g);
  } else if (t < 2400) {
    int y = triWave(t - 1200, 1200, YELLOW_MAX);
    setOnly(0, y, 0);
  } else if (t < 3600) {
    int r = triWave(t - 2400, 1200, RED_MAX);
    setOnly(r, 0, 0);
  } else if (t < 6200) {
    updateAi();
  } else if (t < 8200) {
    updateThinking();
  } else if (t < 10200) {
    updateBusy();
  } else if (t < 12200) {
    updateError();
  } else if (t < 14200) {
    updateAlarm();
  } else {
    unsigned long p = t - 14200;
    if (p < 600) setOnly(RED_MAX, 0, 0);
    else if (p < 1200) setOnly(0, 0, GREEN_MAX);
    else setOnly(0, YELLOW_MAX, 0);
  }
}

// =====================================================
// BLE 回调
// =====================================================

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE client connected.");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE client disconnected. Restart advertising.");
    BLEDevice::startAdvertising();
  }
};

class ModeCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    value.trim();

    Serial.print("BLE write: ");
    Serial.println(value);

    setMode(value);
  }

  void onRead(BLECharacteristic* pCharacteristic) {
    pCharacteristic->setValue(currentMode.c_str());
  }
};

// =====================================================
// 初始化
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  ledcAttach(RED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(YELLOW_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(GREEN_PIN, PWM_FREQ, PWM_RESOLUTION);

  allOff();

  currentMode = "demo";
  modeStart = millis();

  Serial.println();
  Serial.println("Power on. Default mode: demo");
  Serial.println("ClaudeLight - BLE enhanced version.");
  Serial.print("BLE device name: ");
  Serial.println(BLE_DEVICE_NAME);

  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pModeCharacteristic = pService->createCharacteristic(
    MODE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pModeCharacteristic->setCallbacks(new ModeCharacteristicCallbacks());
  pModeCharacteristic->setValue(currentMode.c_str());
  pModeCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started.");
  Serial.println("Supported modes:");
  Serial.println("demo / thinking / ai / busy / success / error / alarm / traffic / off / red / yellow / green");
}

// =====================================================
// 主循环
// =====================================================

void loop() {
  autoTimeoutCheck();

  if (currentMode == "busy") {
    updateBusy();
  } else if (currentMode == "error") {
    updateError();
  } else if (currentMode == "thinking") {
    updateThinking();
  } else if (currentMode == "ai") {
    updateAi();
  } else if (currentMode == "success") {
    updateSuccess();
  } else if (currentMode == "traffic") {
    updateTraffic();
  } else if (currentMode == "alarm") {
    updateAlarm();
  } else if (currentMode == "demo") {
    updateDemo();
  } else if (currentMode == "off") {
    allOff();
  }

  delay(5);
}
