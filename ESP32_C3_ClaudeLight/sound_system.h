// =====================================================
// sound_system.h — ClaudeLight 音效子系统
//
// 职责：
//  - 音效库数据模型（id + 音符切片），存于 RAM 静态池
//  - NVS 分块持久化（命名空间 cl_snd，与灯效配置 cl_cfg 隔离）
//  - ledc 非阻塞播放引擎（音高=频率，音量=占空比）
//  - 出厂默认超级玛丽音效库
//  - 供 BLE 层调用的 list / upload / storage 接口
//
// 音符 JSON Schema：
//  { "version":1, "sounds":[ {"id":"coin","name":"Coin",
//    "notes":[[freqHz,ms], ... ] } ] }   // freq=0 表示休止
// =====================================================

#ifndef SOUND_SYSTEM_H
#define SOUND_SYSTEM_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_partition.h>

#define BUZZER_PIN 3
#define BUZZER_PWM_RES 8            // 8-bit：duty 0..255

// 数据层容量
#define MAX_SOUNDS 32
#define MAX_NOTES_PER_SOUND 200
#define NOTE_POOL 1024              // 全局音符池（所有音效共享）
#define SOUND_ID_LEN 16
#define SOUND_BUDGET 8192           // 整包 JSON 字节预算
#define SND_CHUNK_SIZE 1900         // 每块 NVS 存储字节
#define SND_LIST_PACKET 180         // 单包 notify 数据字节（MTU 安全）

// ---------------- 数据模型 ----------------

struct SNote
{
  uint16_t freq; // Hz，0 = 休止
  uint16_t ms;   // 时长
};

struct SSound
{
  char id[SOUND_ID_LEN + 1];
  uint16_t start; // 在音符池中的起始下标
  uint16_t count; // 音符数量
};

static SSound sSounds[MAX_SOUNDS];
static SNote sNotes[NOTE_POOL];
static uint8_t sSoundCount = 0;
static String sRawJson = ""; // 当前音效库的原始 JSON（供 list 回传）

// ---------------- 播放引擎（ledc，非阻塞） ----------------

static bool sPlaying = false;
static uint16_t sCurSound = 0;
static uint16_t sCurNote = 0;
static uint32_t sNoteEnd = 0;
static bool sEnabled = true;
static int sVolume = 50; // 0..100

static inline uint8_t dutyForVolume(int vol)
{
  // 将 0..100 音量线性映射到 0..127（50% 占空比为无源蜂鸣器上限）
  vol = constrain(vol, 0, 100);
  return (uint8_t)((vol * 127) / 100);
}

static void playCurrentNote()
{
  const SSound &snd = sSounds[sCurSound];
  if (sCurNote >= snd.count)
  {
    sPlaying = false;
    ledcWrite(BUZZER_PIN, 0);
    return;
  }
  const SNote &n = sNotes[snd.start + sCurNote];
  if (n.freq > 0 && sEnabled)
  {
    ledcChangeFrequency(BUZZER_PIN, n.freq, BUZZER_PWM_RES);
    ledcWrite(BUZZER_PIN, dutyForVolume(sVolume));
  }
  else
  {
    ledcWrite(BUZZER_PIN, 0); // 休止或静音
  }
  sNoteEnd = millis() + (uint32_t)n.ms;
}

static void soundStart(const char *id)
{
  if (!sEnabled || id == nullptr || id[0] == '\0')
  {
    return;
  }
  int idx = -1;
  for (int i = 0; i < sSoundCount; i++)
  {
    if (strcmp(sSounds[i].id, id) == 0)
    {
      idx = i;
      break;
    }
  }
  if (idx < 0)
  {
    Serial.print("Sound id not found: ");
    Serial.println(id);
    return;
  }
  sCurSound = idx;
  sCurNote = 0;
  sPlaying = true;
  playCurrentNote();
}

static void soundStop()
{
  sPlaying = false;
  ledcWrite(BUZZER_PIN, 0);
}

// 在 loop() 中每拍调用，推进音符，不阻塞
static void soundUpdate()
{
  if (!sPlaying)
  {
    return;
  }
  if (millis() >= sNoteEnd)
  {
    sCurNote++;
    playCurrentNote();
  }
}

static void soundSetEnabled(bool en) { sEnabled = en; if (!en) soundStop(); }
static void soundSetVolume(int v) { sVolume = constrain(v, 0, 100); }

// ---------------- NVS 分块存储 ----------------

static Preferences sPref;

// 解析 JSON -> RAM 数据模型。成功返回 true 并更新 sRawJson
static bool parseSounds(const String &json)
{
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, json))
  {
    Serial.println("parseSounds: JSON parse error.");
    return false;
  }
  JsonArray arr = doc["sounds"].as<JsonArray>();
  uint8_t sc = 0;
  uint16_t np = 0;
  for (JsonObject so : arr)
  {
    if (sc >= MAX_SOUNDS)
      break;
    const char *id = so["id"];
    if (!id)
      continue;
    // id 合法性：仅 [a-z0-9_]，长度 <= SOUND_ID_LEN
    size_t il = strlen(id);
    if (il == 0 || il > SOUND_ID_LEN)
      continue;
    bool idOk = true;
    for (size_t k = 0; k < il; k++)
    {
      char c = id[k];
      bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
      if (!ok)
      {
        idOk = false;
        break;
      }
    }
    if (!idOk)
      continue;

    JsonArray notes = so["notes"].as<JsonArray>();
    int cnt = notes.size();
    if (cnt > MAX_NOTES_PER_SOUND)
      cnt = MAX_NOTES_PER_SOUND;
    if (np + cnt > NOTE_POOL)
      break;

    strncpy(sSounds[sc].id, id, SOUND_ID_LEN);
    sSounds[sc].id[SOUND_ID_LEN] = '\0';
    sSounds[sc].start = np;
    sSounds[sc].count = cnt;
    for (int j = 0; j < cnt; j++)
    {
      JsonArray pair = notes[j].as<JsonArray>();
      uint32_t f = (pair.size() >= 1) ? (uint32_t)pair[0] : 0;
      uint32_t m = (pair.size() >= 2) ? (uint32_t)pair[1] : 0;
      sNotes[np + j].freq = (f > 65535UL) ? 65535 : (uint16_t)f;
      sNotes[np + j].ms = (m > 65535UL) ? 65535 : (uint16_t)m;
    }
    np += cnt;
    sc++;
  }
  sSoundCount = sc;
  sRawJson = json;
  Serial.print("parseSounds ok: sounds=");
  Serial.print(sSoundCount);
  Serial.print(" notes=");
  Serial.println(np);
  return true;
}

// 将 JSON 分块写入 NVS（先清旧键再写）
static bool persistSounds(const String &json)
{
  sPref.begin("cl_snd", false);

  // 读取旧的块数并删除，避免残留
  String oldMeta = sPref.getString("meta", "");
  if (oldMeta.length())
  {
    int c1 = oldMeta.indexOf('|');
    int c2 = oldMeta.indexOf('|', c1 + 1);
    int chunks = oldMeta.substring(c2 + 1).toInt();
    char key[8];
    for (int i = 0; i < chunks; i++)
    {
      snprintf(key, sizeof(key), "c%03d", i);
      sPref.remove(key);
    }
  }

  int chunks = 0;
  size_t pos = 0;
  char key[8];
  while (pos < json.length())
  {
    String sub = json.substring(pos, pos + SND_CHUNK_SIZE);
    snprintf(key, sizeof(key), "c%03d", chunks);
    sPref.putString(key, sub);
    pos += SND_CHUNK_SIZE;
    chunks++;
  }
  char meta[32];
  snprintf(meta, sizeof(meta), "%u|%d|%d", (unsigned)json.length(), chunks, chunks);
  sPref.putString("meta", meta);
  sPref.end();
  return true;
}

// 从 NVS 读回 JSON；不存在返回空串
static String loadSoundsJson()
{
  sPref.begin("cl_snd", true);
  String meta = sPref.getString("meta", "");
  String json = "";
  if (meta.length())
  {
    int c1 = meta.indexOf('|');
    int c2 = meta.indexOf('|', c1 + 1);
    unsigned len = meta.substring(0, c1).toInt();
    int chunks = meta.substring(c2 + 1).toInt();
    char key[8];
    for (int i = 0; i < chunks; i++)
    {
      snprintf(key, sizeof(key), "c%03d", i);
      json += sPref.getString(key, "");
    }
    if (json.length() != len)
    {
      Serial.println("loadSoundsJson: length mismatch, ignore.");
      json = "";
    }
  }
  sPref.end();
  return json;
}

// ---------------- 出厂默认玛丽音效库 ----------------

static const char DEFAULT_SOUNDS_JSON[] =
    "{\"version\":1,\"sounds\":["
    "{\"id\":\"coin\",\"name\":\"Coin\",\"notes\":[[1319,60],[1976,420]]},"
    "{\"id\":\"powerup\",\"name\":\"Power Up\",\"notes\":[[523,50],[659,50],[784,50],[1047,50],[1319,50],[1568,50],[2093,50],[2637,180]]},"
    "{\"id\":\"oneup\",\"name\":\"1-Up\",\"notes\":[[659,90],[1047,90],[784,90],[880,90],[988,90],[831,90],[659,90],[1047,90],[784,120],[587,200]]},"
    "{\"id\":\"bump\",\"name\":\"Block Bump\",\"notes\":[[300,40],[240,40],[180,60]]},"
    "{\"id\":\"grow\",\"name\":\"Mushroom Grow\",\"notes\":[[1047,70],[988,70],[1047,70],[988,70],[1047,70],[988,70],[659,70],[587,70],[523,140]]},"
    "{\"id\":\"star\",\"name\":\"Star Power\",\"notes\":[[1047,60],[1047,60],[831,60],[1047,60],[1319,60],[1568,120],[1319,60],[1047,60],[831,60],[659,120],[784,60],[831,60],[659,120],[0,60],[523,120],[784,120],[659,120],[523,180]]},"
    "{\"id\":\"clear\",\"name\":\"Stage Clear\",\"notes\":[[784,110],[988,110],[1319,110],[1568,110],[2093,220],[1568,160],[1976,300]]},"
    "{\"id\":\"gameover\",\"name\":\"Game Over\",\"notes\":[[392,200],[330,200],[262,200],[294,200],[247,200],[196,450]]},"
    "{\"id\":\"jump\",\"name\":\"Jump\",\"notes\":[[300,40],[480,40],[680,40],[880,40],[1080,40],[820,40],[560,40],[360,60]]},"
    "{\"id\":\"fireball\",\"name\":\"Fireball\",\"notes\":[[1400,30],[1200,30],[1000,30],[820,30],[640,30],[460,40],[300,40]]}"
    "]}";

static void installDefaultSounds()
{
  String json = String(DEFAULT_SOUNDS_JSON);
  parseSounds(json);
  persistSounds(json);
  Serial.println("Installed default Mario sound library.");
}

// 初始化：挂载 buzzer ledc + 载入音效库（无则装默认）
static void soundInit()
{
  ledcAttach(BUZZER_PIN, 1000, BUZZER_PWM_RES);
  ledcWrite(BUZZER_PIN, 0);

  String json = loadSoundsJson();
  if (json.length() == 0 || !parseSounds(json))
  {
    installDefaultSounds();
  }
}

// ---------------- 供 BLE 层使用的接口 ----------------

// 摘要：{"count":N,"bytes":..,"ids":["coin",..]}
static void soundSummaryJson(String &out)
{
  DynamicJsonDocument doc(1024);
  doc["count"] = sSoundCount;
  doc["bytes"] = sRawJson.length();
  JsonArray ids = doc.createNestedArray("ids");
  for (int i = 0; i < sSoundCount; i++)
    ids.add(sSounds[i].id);
  serializeJson(doc, out);
}

static const String &soundRawJson() { return sRawJson; }

// 存储状态：{"partitionTotal":..,"budget":..,"used":..,"free":..,"chunks":..,"count":..}
static void soundStorageJson(String &out)
{
  uint32_t partTotal = 0;
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "nvs");
  if (p)
    partTotal = p->size;

  uint32_t used = sRawJson.length();
  int chunks = (int)((used + SND_CHUNK_SIZE - 1) / SND_CHUNK_SIZE);

  DynamicJsonDocument doc(256);
  doc["partitionTotal"] = partTotal;
  doc["budget"] = SOUND_BUDGET;
  doc["used"] = used;
  doc["free"] = (used < SOUND_BUDGET) ? (SOUND_BUDGET - used) : 0;
  doc["chunks"] = chunks;
  doc["count"] = sSoundCount;
  serializeJson(doc, out);
}

// 上传接收（begin/data/commit）
static String sRxJson = "";
static bool sRxActive = false;

static bool soundRxBegin(uint32_t size, String &err)
{
  if (size > SOUND_BUDGET)
  {
    err = "size exceeds budget";
    return false;
  }
  sRxJson = "";
  sRxActive = true;
  return true;
}

static bool soundRxData(const String &data, String &err)
{
  if (!sRxActive)
  {
    err = "no active upload";
    return false;
  }
  if (sRxJson.length() + data.length() > SOUND_BUDGET)
  {
    err = "buffer overflow";
    sRxActive = false;
    return false;
  }
  sRxJson += data;
  return true;
}

// 提交：校验 + 落盘 + 热重载 RAM
static bool soundRxCommit(String &err)
{
  if (!sRxActive)
  {
    err = "no active upload";
    return false;
  }
  sRxActive = false;

  // 先在副本上解析，失败则保留旧库
  String candidate = sRxJson;
  // 备份现有数据指针无法直接回滚，这里用临时解析验证合法性
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, candidate))
  {
    err = "invalid json";
    return false;
  }
  if (!doc.containsKey("sounds"))
  {
    err = "missing sounds array";
    return false;
  }

  // 解析进 RAM（会覆盖现有模型；已通过 JSON 校验）
  if (!parseSounds(candidate))
  {
    err = "parse failed";
    return false;
  }
  // 落盘
  persistSounds(candidate);
  err = "ok";
  return true;
}

#endif // SOUND_SYSTEM_H
