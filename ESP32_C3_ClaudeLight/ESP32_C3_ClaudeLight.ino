#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// =====================================================
// ESP32-C3 + ClaudeLight: BLE 蓝牙控制版（支持持久化设置）
//
// 接线方式（四色共阴模块 R Y B G GND）：
// ESP32 GND   -> 模块 GND
// ESP32 IO0   -> 220Ω -> 模块 R = 红灯
// ESP32 IO1   -> 220Ω -> 模块 Y = 黄灯
// ESP32 IO2   -> 220Ω -> 模块 B = 蓝灯
// ESP32 IO3   -> 220Ω -> 模块 G = 绿灯
//
// 共阳模块：GPIO LOW = 灯亮，GPIO HIGH = 灯灭（下拉点亮）。
// 默认开机模式：demo
//
// BLE Setting Characteristic 读写格式（JSON）：
// {
//   "redMax": 255,
//   "yellowMax": 220,
//   "blueMax": 220,
//   "greenMax": 220,
//   "defaultMode": "demo",
//   "timeoutMode": "traffic",
//   "normalTimeout": 5,
//   "trafficTimeout": 10,
//   "bleName": "ClaudeLight"
// }
// BLE Reset Characteristic：写入任意内容即恢复出厂并重启。
// =====================================================

#define SERVICE_UUID        "b8b7e001-7a6b-4f4f-9a8b-11c0ffee0001"
#define MODE_CHAR_UUID      "b8b7e002-7a6b-4f4f-9a8b-11c0ffee0001"
#define SETTING_CHAR_UUID   "b8b7e003-7a6b-4f4f-9a8b-11c0ffee0001"
#define RESET_CHAR_UUID     "b8b7e004-7a6b-4f4f-9a8b-11c0ffee0001"

// NVS 存储命名空间
#define NVS_NAMESPACE       "cl_cfg"

// 默认值常量（用于首次启动和出厂重置）
#define DEFAULT_BLE_NAME        "ClaudeLight"
#define DEFAULT_RED_MAX         255
#define DEFAULT_YELLOW_MAX      220
#define DEFAULT_BLUE_MAX        220
#define DEFAULT_GREEN_MAX       220
#define DEFAULT_MODE            "demo"
#define DEFAULT_TIMEOUT_MODE    "traffic"
#define DEFAULT_NORMAL_TIMEOUT  5
#define DEFAULT_TRAFFIC_TIMEOUT 10

const int RED_PIN = 0;
const int YELLOW_PIN = 1;
const int BLUE_PIN = 2;
const int GREEN_PIN = 3;

const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;

// =====================================================
// 运行时配置（从 NVS 加载，可通过 BLE 修改）
// =====================================================

int RED_MAX    = DEFAULT_RED_MAX;
int YELLOW_MAX = DEFAULT_YELLOW_MAX;
int BLUE_MAX   = DEFAULT_BLUE_MAX;
int GREEN_MAX  = DEFAULT_GREEN_MAX;

String cfgDefaultMode   = DEFAULT_MODE;
String cfgTimeoutMode   = DEFAULT_TIMEOUT_MODE;
unsigned long NORMAL_MODE_TIMEOUT_MS  = DEFAULT_NORMAL_TIMEOUT  * 60UL * 1000UL;
unsigned long TRAFFIC_MODE_TIMEOUT_MS = DEFAULT_TRAFFIC_TIMEOUT * 60UL * 1000UL;
String cfgBleName = DEFAULT_BLE_NAME;

String currentMode = "demo";
unsigned long modeStart = 0;

Preferences preferences;
BLEServer* pServer = nullptr;
BLECharacteristic* pModeCharacteristic = nullptr;
BLECharacteristic* pSettingCharacteristic = nullptr;
BLECharacteristic* pResetCharacteristic = nullptr;
bool deviceConnected = false;

// =====================================================
// NVS 持久化配置
// =====================================================

void loadSettings() {
  preferences.begin(NVS_NAMESPACE, true);  // 只读模式打开

  RED_MAX    = preferences.getInt("redMax",    DEFAULT_RED_MAX);
  YELLOW_MAX = preferences.getInt("yellowMax", DEFAULT_YELLOW_MAX);
  BLUE_MAX   = preferences.getInt("blueMax",   DEFAULT_BLUE_MAX);
  GREEN_MAX  = preferences.getInt("greenMax",  DEFAULT_GREEN_MAX);

  cfgDefaultMode  = preferences.getString("defaultMode",  DEFAULT_MODE);
  cfgTimeoutMode  = preferences.getString("timeoutMode",  DEFAULT_TIMEOUT_MODE);
  cfgBleName      = preferences.getString("bleName",      DEFAULT_BLE_NAME);

  int normalMin  = preferences.getInt("normalTimeout",  DEFAULT_NORMAL_TIMEOUT);
  int trafficMin = preferences.getInt("trafficTimeout", DEFAULT_TRAFFIC_TIMEOUT);

  preferences.end();

  NORMAL_MODE_TIMEOUT_MS  = (unsigned long)normalMin  * 60UL * 1000UL;
  TRAFFIC_MODE_TIMEOUT_MS = (unsigned long)trafficMin * 60UL * 1000UL;

  Serial.println("Settings loaded from NVS.");
}

void saveSettings() {
  preferences.begin(NVS_NAMESPACE, false);  // 读写模式打开

  preferences.putInt("redMax",         RED_MAX);
  preferences.putInt("yellowMax",      YELLOW_MAX);
  preferences.putInt("blueMax",        BLUE_MAX);
  preferences.putInt("greenMax",       GREEN_MAX);
  preferences.putString("defaultMode", cfgDefaultMode);
  preferences.putString("timeoutMode", cfgTimeoutMode);
  preferences.putString("bleName",     cfgBleName);
  preferences.putInt("normalTimeout",  (int)(NORMAL_MODE_TIMEOUT_MS  / 60000UL));
  preferences.putInt("trafficTimeout", (int)(TRAFFIC_MODE_TIMEOUT_MS / 60000UL));

  preferences.end();

  Serial.println("Settings saved to NVS.");
}

void resetSettings() {
  preferences.begin(NVS_NAMESPACE, false);
  preferences.clear();
  preferences.end();

  Serial.println("Settings cleared. Restarting...");
  delay(200);
  ESP.restart();
}

// 将当前配置序列化为 JSON 字符串
String buildSettingsJson() {
  StaticJsonDocument<256> doc;

  doc["redMax"]          = RED_MAX;
  doc["yellowMax"]       = YELLOW_MAX;
  doc["blueMax"]         = BLUE_MAX;
  doc["greenMax"]        = GREEN_MAX;
  doc["defaultMode"]     = cfgDefaultMode;
  doc["timeoutMode"]     = cfgTimeoutMode;
  doc["normalTimeout"]   = (int)(NORMAL_MODE_TIMEOUT_MS  / 60000UL);
  doc["trafficTimeout"]  = (int)(TRAFFIC_MODE_TIMEOUT_MS / 60000UL);
  doc["bleName"]         = cfgBleName;

  String output;
  serializeJson(doc, output);
  return output;
}

// 解析并应用 JSON 设置，返回是否有效
bool applySettingsJson(const String& json) {
  StaticJsonDocument<256> doc;

  if (deserializeJson(doc, json)) {
    Serial.println("JSON parse error.");
    return false;
  }

  // 各字段均为可选，不存在时保留原值
  if (doc.containsKey("redMax")) {
    int val = constrain((int)doc["redMax"], 0, 255);
    RED_MAX = val;
  }
  if (doc.containsKey("yellowMax")) {
    YELLOW_MAX = constrain((int)doc["yellowMax"], 0, 255);
  }
  if (doc.containsKey("blueMax")) {
    BLUE_MAX = constrain((int)doc["blueMax"], 0, 255);
  }
  if (doc.containsKey("greenMax")) {
    GREEN_MAX = constrain((int)doc["greenMax"], 0, 255);
  }
  if (doc.containsKey("defaultMode")) {
    String val = doc["defaultMode"].as<String>();
    if (isValidMode(val)) cfgDefaultMode = val;
  }
  if (doc.containsKey("timeoutMode")) {
    String val = doc["timeoutMode"].as<String>();
    if (isValidMode(val)) cfgTimeoutMode = val;
  }
  if (doc.containsKey("normalTimeout")) {
    int val = (int)doc["normalTimeout"];
    if (val > 0 && val <= 120) NORMAL_MODE_TIMEOUT_MS = (unsigned long)val * 60UL * 1000UL;
  }
  if (doc.containsKey("trafficTimeout")) {
    int val = (int)doc["trafficTimeout"];
    if (val > 0 && val <= 120) TRAFFIC_MODE_TIMEOUT_MS = (unsigned long)val * 60UL * 1000UL;
  }
  if (doc.containsKey("bleName")) {
    String val = doc["bleName"].as<String>();
    if (val.length() > 0 && val.length() <= 20) cfgBleName = val;
  }

  return true;
}

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
  writeLed(BLUE_PIN, 0);
  writeLed(GREEN_PIN, 0);
}

void setOnly(int red, int yellow, int blue, int green) {
  writeLed(RED_PIN, constrain(red, 0, RED_MAX));
  writeLed(YELLOW_PIN, constrain(yellow, 0, YELLOW_MAX));
  writeLed(BLUE_PIN, constrain(blue, 0, BLUE_MAX));
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

void fadeToStatic(int targetRed, int targetYellow, int targetBlue, int targetGreen, int fadeMs = 80) {
  allOff();
  int steps = 12;
  int delayPerStep = max(1, fadeMs / steps);

  for (int i = 0; i <= steps; i++) {
    float p = (float)i / steps;
    setOnly(targetRed * p, targetYellow * p, targetBlue * p, targetGreen * p);
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
    mode == "blue" ||
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
    fadeToStatic(RED_MAX, 0, 0, 0, 80);
  } else if (mode == "yellow") {
    fadeToStatic(0, YELLOW_MAX, 0, 0, 80);
  } else if (mode == "blue") {
    fadeToStatic(0, 0, BLUE_MAX, 0, 80);
  } else if (mode == "green") {
    fadeToStatic(0, 0, 0, GREEN_MAX, 80);
  } else if (mode == "success") {
    setOnly(0, 0, 0, GREEN_MAX);
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
  setOnly(0, y, 0, 0);
}

void updateError() {
  unsigned long t = millis() - modeStart;
  int r = fadeInOutBrightness(t, 40, 180, 80, 180, RED_MAX);
  setOnly(r, 0, 0, 0);
}

void updateBlue() {
  unsigned long t = millis() - modeStart;
  int b = fadeInOutBrightness(t, 60, 400, 100, 400, BLUE_MAX);
  setOnly(0, 0, b, 0);
}

void updateThinking() {
  unsigned long t = millis() - modeStart;
  const unsigned long period = 1400;
  unsigned long x = t % period;

  int g = 0;
  int y = 0;
  int b = 0;
  int r = 0;

  if (x < 350) {
    g = map(x, 0, 350, GREEN_MAX, 70);
    y = map(x, 0, 350, 20, YELLOW_MAX);
    b = 0;
    r = 0;
  } else if (x < 700) {
    unsigned long p = x - 350;
    g = map(p, 0, 350, 70, 0);
    y = map(p, 0, 350, YELLOW_MAX, 70);
    b = map(p, 0, 350, 20, BLUE_MAX);
    r = map(p, 0, 350, 20, RED_MAX);
  } else if (x < 1050) {
    unsigned long p = x - 700;
    g = map(p, 0, 350, 20, GREEN_MAX);
    y = map(p, 0, 350, 70, 0);
    b = map(p, 0, 350, BLUE_MAX, 20);
    r = map(p, 0, 350, RED_MAX, 70);
  } else {
    unsigned long p = x - 1050;
    g = map(p, 0, 350, GREEN_MAX, 70);
    y = 20;
    b = map(p, 0, 350, 20, BLUE_MAX);
    r = 70;
  }

  setOnly(r, y, b, g);
}

void updateAi() {
  unsigned long t = millis() - modeStart;
  const unsigned long period = 2400;
  unsigned long x = t % period;

  unsigned long gx = (x + 0) % period;
  unsigned long yx = (x + period / 4) % period;
  unsigned long bx = (x + 2 * period / 4) % period;
  unsigned long rx = (x + 3 * period / 4) % period;

  int g = triWave(gx, period, 150);
  int y = triWave(yx, period, 140);
  int b = triWave(bx, period, 160);
  int r = triWave(rx, period, 170);

  setOnly(r, y, b, g);
}

void updateSuccess() {
  setOnly(0, 0, 0, GREEN_MAX);
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
    setOnly(brightness, 0, 0, 0);
  } else {
    setOnly(0, min(brightness, YELLOW_MAX), 0, 0);
  }
}

void updateTraffic() {
  unsigned long t = (millis() - modeStart) % 15000;

  if (t < 5000) {
    setOnly(RED_MAX, 0, 0, 0);
  } else if (t < 6500) {
    unsigned long phase = (t - 5000) % 500;
    int r = 0;
    if (phase < 60) r = map(phase, 0, 60, 0, RED_MAX);
    else if (phase < 230) r = RED_MAX;
    else if (phase < 320) r = map(phase, 230, 320, RED_MAX, 0);
    else r = 0;
    setOnly(r, 0, 0, 0);
  } else if (t < 11500) {
    setOnly(0, 0, 0, GREEN_MAX);
  } else if (t < 13000) {
    unsigned long phase = (t - 11500) % 500;
    int g = 0;
    if (phase < 60) g = map(phase, 0, 60, 0, GREEN_MAX);
    else if (phase < 230) g = GREEN_MAX;
    else if (phase < 320) g = map(phase, 230, 320, GREEN_MAX, 0);
    else g = 0;
    setOnly(0, 0, 0, g);
  } else {
    setOnly(0, YELLOW_MAX, 0, 0);
  }
}

void updateDemo() {
  unsigned long t = (millis() - modeStart) % 19000;

  if (t < 1200) {
    int g = triWave(t, 1200, GREEN_MAX);
    setOnly(0, 0, 0, g);
  } else if (t < 2400) {
    int y = triWave(t - 1200, 1200, YELLOW_MAX);
    setOnly(0, y, 0, 0);
  } else if (t < 3600) {
    int r = triWave(t - 2400, 1200, RED_MAX);
    setOnly(r, 0, 0, 0);
  } else if (t < 4800) {
    int b = triWave(t - 3600, 1200, BLUE_MAX);
    setOnly(0, 0, b, 0);
  } else if (t < 7400) {
    updateAi();
  } else if (t < 9400) {
    updateThinking();
  } else if (t < 11400) {
    updateBusy();
  } else if (t < 13400) {
    updateError();
  } else if (t < 15400) {
    updateAlarm();
  } else if (t < 16600) {
    updateBlue();
  } else {
    unsigned long p = t - 16600;
    if (p < 600) setOnly(RED_MAX, 0, 0, 0);
    else if (p < 1200) setOnly(0, 0, 0, GREEN_MAX);
    else if (p < 1800) setOnly(0, 0, BLUE_MAX, 0);
    else setOnly(0, YELLOW_MAX, 0, 0);
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

    Serial.print("BLE write (mode): ");
    Serial.println(value);

    setMode(value);
  }

  void onRead(BLECharacteristic* pCharacteristic) {
    pCharacteristic->setValue(currentMode.c_str());
  }
};

class SettingCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    value.trim();

    Serial.print("BLE write (setting): ");
    Serial.println(value);

    if (applySettingsJson(value)) {
      saveSettings();
      Serial.println("Settings applied and saved.");

      // 用更新后的配置刷新 Characteristic 内容，方便客户端读回确认
      String responseJson = buildSettingsJson();
      pCharacteristic->setValue(responseJson.c_str());
    } else {
      Serial.println("Invalid setting JSON, ignored.");
    }
  }

  void onRead(BLECharacteristic* pCharacteristic) {
    String json = buildSettingsJson();
    pCharacteristic->setValue(json.c_str());
  }
};

class ResetCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    Serial.println("BLE write (reset): factory reset triggered.");
    resetSettings();  // 清空 NVS 并重启
  }
};

// =====================================================
// 初始化
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  // 从 NVS 加载持久化配置（必须在 BLE 初始化前完成，以获取 bleName）
  loadSettings();

  ledcAttach(RED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(YELLOW_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(BLUE_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(GREEN_PIN, PWM_FREQ, PWM_RESOLUTION);

  allOff();

  currentMode = cfgDefaultMode;
  modeStart = millis();

  Serial.println();
  Serial.print("Power on. Default mode: ");
  Serial.println(currentMode);
  Serial.println("ClaudeLight - BLE enhanced version with persistent settings.");
  Serial.print("BLE device name: ");
  Serial.println(cfgBleName);

  BLEDevice::init(cfgBleName.c_str());

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // numHandles 默认 15，3 个 Characteristic（含 NOTIFY descriptor）需要更多 handle，扩容到 20
  BLEService* pService = pServer->createService(BLEUUID(String(SERVICE_UUID)), 20);

  // Mode Characteristic（原有）
  pModeCharacteristic = pService->createCharacteristic(
    BLEUUID(String(MODE_CHAR_UUID)),
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pModeCharacteristic->setCallbacks(new ModeCharacteristicCallbacks());
  pModeCharacteristic->setValue(currentMode.c_str());
  pModeCharacteristic->addDescriptor(new BLE2902());

  // Setting Characteristic（新增：读写 JSON 配置）
  pSettingCharacteristic = pService->createCharacteristic(
    BLEUUID(String(SETTING_CHAR_UUID)),
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  pSettingCharacteristic->setCallbacks(new SettingCharacteristicCallbacks());
  pSettingCharacteristic->setValue(buildSettingsJson().c_str());

  // Reset Characteristic（新增：写入任意值触发出厂重置）
  pResetCharacteristic = pService->createCharacteristic(
    BLEUUID(String(RESET_CHAR_UUID)),
    BLECharacteristic::PROPERTY_WRITE
  );
  pResetCharacteristic->setCallbacks(new ResetCharacteristicCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID(String(SERVICE_UUID)));
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started.");
  Serial.println("Supported modes: demo / thinking / ai / busy / success / error / alarm / traffic / blue / off / red / yellow / green");
  Serial.println("BLE Characteristics:");
  Serial.println("  MODE_CHAR    : read/write/notify current mode");
  Serial.println("  SETTING_CHAR : read/write JSON config (persistent)");
  Serial.println("  RESET_CHAR   : write anything to factory reset & reboot");
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
  } else if (currentMode == "blue") {
    updateBlue();
  } else if (currentMode == "demo") {
    updateDemo();
  } else if (currentMode == "off") {
    allOff();
  }

  delay(5);
}
