// =====================================================
// 【临时测试固件】超级玛丽经典音效 - GPIO3 蜂鸣器
//
// 接线：
// ESP32 GND -> 蜂鸣器 GND
// ESP32 IO3 -> 蜂鸣器 IO（无源蜂鸣器直接驱动；有源也能响）
//
// 开机即循环播放：金币、变身升级、1-Up、跳跃、火球等音效。
// 测试完成后用 `git checkout -- ESP32_C3_ClaudeLight/ESP32_C3_ClaudeLight.ino`
// 即可恢复正式版固件。
// =====================================================

#define BUZZER_PIN 3

// 灯（顺带跟着节奏闪，营造氛围）
#define GREEN_PIN 0
#define YELLOW_PIN 1
#define RED_PIN 2

typedef struct
{
  unsigned int freq; // Hz，0 = 休止
  int ms;            // 时长（毫秒）
} Note;

// ---------------- 音效定义 ----------------

// 吃金币：经典 E6 -> B6 双音
const Note sfxCoin[] = {
    {1319, 60},  // E6
    {1976, 420}, // B6（余音延长）
};

// 获得道具/变身：快速上行琶音
const Note sfxPowerUp[] = {
    {523, 50},  // C5
    {659, 50},  // E5
    {784, 50},  // G5
    {1047, 50}, // C6
    {1319, 50}, // E6
    {1568, 50}, // G6
    {2093, 50}, // C7
    {2637, 180} // E7（收尾拖长）
};

// 1-Up（多吃一条命）
const Note sfxOneUp[] = {
    {659, 90},   // E5
    {1047, 90},  // C6
    {784, 90},   // G5
    {880, 90},   // A5
    {988, 90},   // B5
    {831, 90},   // G#5
    {659, 90},   // E5
    {1047, 90},  // C6
    {784, 120}, // G5
    {587, 200}  // D5（收尾）
};

// 顶砖块“蹦”：快速下滑低音
const Note sfxBump[] = {
    {300, 40},
    {240, 40},
    {180, 60},
};

// 吃蘑菇/小花 grow 主题的开头片段（下行音阶点缀）
const Note sfxGrow[] = {
    {1047, 70}, // C6
    {988, 70},  // B5
    {1047, 70},
    {988, 70},
    {1047, 70},
    {988, 70},
    {659, 70}, // E5
    {587, 70}, // D5
    {523, 140} // C5
};

// 星星无敌主题（快节奏小片段）
const Note sfxStar[] = {
    {1047, 60}, {1047, 60}, {831, 60}, {1047, 60},
    {1319, 60}, {1568, 120}, {1319, 60}, {1047, 60},
    {831, 60}, {659, 120}, {784, 60}, {831, 60},
    {659, 120}, {0, 60}, {523, 120}, {784, 120},
    {659, 120}, {523, 180}
};

// 关卡通过小号旋律（经典 "Here we go" 后的 clear fanfare 开头）
const Note sfxStageClear[] = {
    {784, 110},  // G5
    {988, 110},  // B5
    {1319, 110}, // E6
    {1568, 110}, // G6
    {2093, 220}, // C7
    {1568, 160}, // G6
    {1976, 300}  // B6（收尾长音）
};

// 游戏结束低音下行
const Note sfxGameOver[] = {
    {392, 200}, // G4
    {330, 200}, // E4
    {262, 200}, // C4
    {294, 200}, // D4
    {247, 200}, // B3
    {196, 450}  // G3
};

// ---------------- 播放引擎 ----------------

void playSequence(const Note *seq, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    const Note &n = seq[i];
    if (n.freq > 0)
    {
      tone(BUZZER_PIN, n.freq);
    }
    else
    {
      noTone(BUZZER_PIN);
    }

    // 音符间留一点间隙，模拟原音效的顿挫感
    int gate = (n.ms > 30) ? (n.ms - 20) : n.ms;
    delay(gate);
    if (n.ms > 30)
    {
      noTone(BUZZER_PIN);
      delay(n.ms - gate);
    }
  }
  noTone(BUZZER_PIN);
}

// 上滑音：马里奥跳跃“嘣~”
void playJump()
{
  for (unsigned int f = 250; f <= 1100; f += 60)
  {
    tone(BUZZER_PIN, f);
    delay(10);
  }
  for (unsigned int f = 1100; f >= 300; f -= 100)
  {
    tone(BUZZER_PIN, f);
    delay(8);
  }
  noTone(BUZZER_PIN);
}

// 下滑音：火球飞过
void playFireball()
{
  for (unsigned int f = 1400; f >= 300; f -= 80)
  {
    tone(BUZZER_PIN, f);
    delay(12);
  }
  noTone(BUZZER_PIN);
}

// 当前音效期间LED指示
void ledFor(int index)
{
  // index 循环对应不同颜色，纯氛围灯
  static const int vals[9][3] = {
      {0, 1, 0}, // coin 绿
      {0, 1, 0}, // powerup 绿
      {1, 0, 0}, // 1up 红
      {0, 1, 0}, // bump 绿
      {1, 1, 0}, // grow 黄
      {0, 0, 1}, // star 蓝绿->用绿灯代替策略
      {1, 0, 0}, // clear 红
      {0, 1, 0}, // jump 绿
      {0, 0, 1}, // gameover 红
  };
  digitalWrite(RED_PIN, vals[index % 9][0]);
  digitalWrite(YELLOW_PIN, vals[index % 9][1]);
  digitalWrite(GREEN_PIN, vals[index % 9][2]);
}

// ---------------- 主流程 ----------------

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("ClaudeLight Mario SFX test firmware (buzzer on GPIO3)");

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);

  // 开机提示音
  playSequence(sfxCoin, sizeof(sfxCoin) / sizeof(Note));
}

void loop()
{
  struct Item
  {
    const char *name;
    const Note *seq;
    size_t len;
    void (*rampFn)();
  };

  static const Item items[] = {
      {"Coin x2", sfxCoin, sizeof(sfxCoin) / sizeof(Note), nullptr},
      {"Jump", nullptr, 0, playJump},
      {"PowerUp", sfxPowerUp, sizeof(sfxPowerUp) / sizeof(Note), nullptr},
      {"Bump + Coin", sfxBump, sizeof(sfxBump) / sizeof(Note), nullptr},
      {"1-Up", sfxOneUp, sizeof(sfxOneUp) / sizeof(Note), nullptr},
      {"Grow", sfxGrow, sizeof(sfxGrow) / sizeof(Note), nullptr},
      {"Fireball", nullptr, 0, playFireball},
      {"Star", sfxStar, sizeof(sfxStar) / sizeof(Note), nullptr},
      {"Stage Clear", sfxStageClear, sizeof(sfxStageClear) / sizeof(Note), nullptr},
      {"Game Over", sfxGameOver, sizeof(sfxGameOver) / sizeof(Note), nullptr},
  };

  static size_t idx = 0;

  const Item &it = items[idx % (sizeof(items) / sizeof(Item))];
  Serial.print("Playing: ");
  Serial.println(it.name);

  ledFor((int)idx);

  if (it.rampFn)
  {
    it.rampFn();
  }
  if (it.seq)
  {
    playSequence(it.seq, it.len);
    // “Bump + Coin”连招：顶一下立刻吃金币
    if (idx % (sizeof(items) / sizeof(Item)) == 3)
    {
      playSequence(sfxCoin, sizeof(sfxCoin) / sizeof(Note));
    }
  }

  digitalWrite(RED_PIN, LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);

  idx++;

  // 音效之间留一拍安静
  delay(900);
}
