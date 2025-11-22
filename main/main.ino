#include <NS_Rainbow.h>
#include <math.h>

#define PIN 9
#define N_CELL 8

NS_Rainbow ns_stick = NS_Rainbow(N_CELL, PIN);

// 레인 매핑: lane 0~3 각각 2개 LED
const uint8_t laneStart[4] = {0, 2, 4, 6};
const uint8_t ledsPerLane = 2;

// === BPM / OFFSET 상태 ===
float bpm = 180.0f;
float beatMs = 60000.0f / bpm;

unsigned long flashDurationMs = beatMs / 3.0f;
unsigned long dualRainbowDurationMs = beatMs * 2.0f;
float breathingPeriodBaseMs = beatMs * 2.0f;

int ledOffsetMs = 0;

// === FPS 설정 ===
const int targetFPS = 60; // 인게임 FPS에 맞춰 조정

unsigned long calculateFrameDelay(int targetFPS)
{
  return 1000 / targetFPS;
}

unsigned long frameDelayMs = calculateFrameDelay(targetFPS);

void setBpm(float newBpm)
{
  if (newBpm <= 0)
    return;
  bpm = newBpm;
  beatMs = 60000.0f / bpm;

  flashDurationMs = beatMs / 3.0f;
  dualRainbowDurationMs = beatMs * 2.0f;
  breathingPeriodBaseMs = beatMs * 2.0f;

  Serial.print(F("[BPM] set to "));
  Serial.print(bpm);
  Serial.print(F(", beatMs="));
  Serial.println(beatMs);
}

void setOffset(int ms)
{
  if (ms < 0)
    ms = 0;
  ledOffsetMs = ms;
}

// === 레인 상태 ===
enum LaneEffectType
{
  EFFECT_NONE,
  EFFECT_HIT_FLASH,
  EFFECT_BREATHING
};

struct LaneState
{
  LaneEffectType effect;
  unsigned long startTime;
  unsigned long lastPressTime;
  uint8_t baseHue;
  uint8_t repeatCount;
};

LaneState lanes[4];

// === Dual rainbow 상태 ===
bool dualRainbowActive = false;
unsigned long dualRainbowStart = 0;
float dualBaseHue = 0.0f;

// === HSV → RGB ===
void hsvToRgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b)
{
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
  float m = v - c;

  float r_, g_, b_;

  if (h < 60)
  {
    r_ = c;
    g_ = x;
    b_ = 0;
  }
  else if (h < 120)
  {
    r_ = x;
    g_ = c;
    b_ = 0;
  }
  else if (h < 180)
  {
    r_ = 0;
    g_ = c;
    b_ = x;
  }
  else if (h < 240)
  {
    r_ = 0;
    g_ = x;
    b_ = c;
  }
  else if (h < 300)
  {
    r_ = x;
    g_ = 0;
    b_ = c;
  }
  else
  {
    r_ = c;
    g_ = 0;
    b_ = x;
  }

  r = (r_ + m) * 255;
  g = (g_ + m) * 255;
  b = (b_ + m) * 255;
}

// === 레인 초기화 ===
void initLanes()
{
  for (int i = 0; i < 4; i++)
  {
    lanes[i].effect = EFFECT_NONE;
    lanes[i].startTime = 0;
    lanes[i].baseHue = 60 * i; // 레인별 Hue 다르게
    lanes[i].repeatCount = 0;
  }
}

// === Dual rainbow 트리거 ===
void triggerDualRainbow()
{
  dualRainbowActive = true;
  dualRainbowStart = millis();
}

// === 렌더링: Dual rainbow ===
void renderDualRainbow(unsigned long now)
{
  unsigned long elapsed = now - dualRainbowStart;
  if (elapsed >= dualRainbowDurationMs)
  {
    dualRainbowActive = false;
    return;
  }

  dualBaseHue = fmod(dualBaseHue + 0.8f, 360.0f);

  for (uint8_t i = 0; i < N_CELL; i++)
  {
    float center = (N_CELL - 1) / 2.0f; // 3.5
    float dist = fabs(i - center);      // 0 ~ 3.5

    float hue = fmod(dualBaseHue + dist * 35.0f, 360.0f);

    uint8_t r, g, b;
    hsvToRgb(hue, 1.0, 1.0, r, g, b);
    ns_stick.setColor(i, ns_stick.RGBtoColor(r, g, b));
  }
}

// === 렌더링: Hit flash ===
void renderLaneHitFlash(uint8_t lane, unsigned long now)
{
  unsigned long duration = flashDurationMs;
  unsigned long elapsed = now - lanes[lane].startTime;

  float t = (elapsed >= duration) ? 1.0f : (float)elapsed / duration;
  float brightness = 1.0f - t;

  if (elapsed >= duration)
  {
    lanes[lane].effect = EFFECT_NONE;
    brightness = 0.0f;
  }

  float hue = lanes[lane].baseHue;
  uint8_t r, g, b;
  hsvToRgb(hue, 1.0f, brightness, r, g, b);

  for (uint8_t i = 0; i < ledsPerLane; i++)
  {
    uint8_t ledIndex = laneStart[lane] + i;
    ns_stick.setColor(ledIndex, ns_stick.RGBtoColor(r, g, b));
  }
}

// === 렌더링: Breathing rainbow ===
void renderLaneBreathing(uint8_t lane, unsigned long now)
{
  unsigned long elapsed = now - lanes[lane].startTime;

  int rc = lanes[lane].repeatCount;
  if (rc < 1)
    rc = 1;
  float factor = 1.0f / rc;

  float periodMs = breathingPeriodBaseMs * factor;
  if (periodMs < beatMs * 0.5f)
    periodMs = beatMs * 0.5f;

  float phase = 2.0f * PI * ((float)elapsed / periodMs);

  float brightness = 0.3f + 0.7f * ((sin(phase) + 1.0f) * 0.5f);

  float hue = fmod(lanes[lane].baseHue + elapsed * 0.03f, 360.0f);

  uint8_t r, g, b;
  hsvToRgb(hue, 1.0f, brightness, r, g, b);

  for (uint8_t i = 0; i < ledsPerLane; i++)
  {
    uint8_t ledIndex = laneStart[lane] + i;
    ns_stick.setColor(ledIndex, ns_stick.RGBtoColor(r, g, b));
  }
}

// === 효과 업데이트 ===
void updateEffects()
{
  unsigned long now = millis();

  if (dualRainbowActive)
  {
    renderDualRainbow(now);
    ns_stick.show();
    return;
  }

  for (uint8_t lane = 0; lane < 4; lane++)
  {
    switch (lanes[lane].effect)
    {
    case EFFECT_HIT_FLASH:
      renderLaneHitFlash(lane, now);
      break;
    case EFFECT_BREATHING:
      renderLaneBreathing(lane, now);
      break;
    case EFFECT_NONE:
    default:
      for (uint8_t i = 0; i < ledsPerLane; i++)
      {
        uint8_t ledIndex = laneStart[lane] + i;
        ns_stick.setColor(ledIndex, ns_stick.RGBtoColor(0, 0, 0));
      }
      break;
    }
  }

  ns_stick.show();
}

// === 키 코드 → 레인 인덱스 매핑 ===
int keyToLane(char key)
{
  switch (key)
  {
  case 'D':
    return 0;
  case 'F':
    return 1;
  case 'K':
    return 2;
  case 'L':
    return 3;
  default:
    return -1;
  }
}

// === 키 프레스 이벤트 처리 ===
void onKeyPress(char key)
{
  unsigned long now = millis();
  const float repeatWindowBeats = 1.0f; // "연타"로 볼 최대 간격
  const unsigned long repeatWindowMs = (unsigned long)(beatMs * repeatWindowBeats);

  if (key == 'E')
  {
    triggerDualRainbow();
    return;
  }

  int lane = keyToLane(key);
  if (lane < 0)
    return;

  LaneState &st = lanes[lane];

  // 이전 프레스와의 간격으로 연타 판단
  if (now - st.lastPressTime < repeatWindowMs)
  {
    st.repeatCount++;
  }
  else
  {
    st.repeatCount = 1;
  }
  st.lastPressTime = now;

  st.startTime = now + ledOffsetMs;

  if (st.repeatCount >= 2)
  {
    Serial.print("[BREATH] lane=");
    Serial.print(lane);
    Serial.print(" rc=");
    Serial.println(st.repeatCount);
    st.effect = EFFECT_BREATHING;
  }
  else
  {
    st.effect = EFFECT_HIT_FLASH;
  }
}

// === 키 릴리즈 이벤트 처리 ===
void onKeyRelease(char key)
{
  if (key == 'E')
  {
    dualRainbowActive = false;
    for (uint8_t i = 0; i < N_CELL; i++)
    {
      ns_stick.setColor(i, ns_stick.RGBtoColor(0, 0, 0));
    }
    ns_stick.show();
    return;
  }

  int lane = keyToLane(key);
  if (lane < 0)
    return;

  lanes[lane].effect = EFFECT_NONE;
  for (uint8_t i = 0; i < ledsPerLane; i++)
  {
    uint8_t ledIndex = laneStart[lane] + i;
    ns_stick.setColor(ledIndex, ns_stick.RGBtoColor(0, 0, 0));
  }
}

void processLine(char *line)
{
  if (strncmp(line, "DOWN ", 5) == 0)
  {
    char k = line[5];
    if (k == 'D' || k == 'F' || k == 'K' || k == 'L' || k == 'E')
    {
      onKeyPress(k);
      return;
    }
  }

  if (strncmp(line, "UP ", 3) == 0)
  {
    char k = line[3];
    if (k == 'D' || k == 'F' || k == 'K' || k == 'L' || k == 'E')
    {
      onKeyRelease(k);
      return;
    }
  }

  if (strncmp(line, "BPM ", 4) == 0)
  {
    int val = atoi(line + 4);
    if (val > 0)
      setBpm((float)val);
    return;
  }

  if (strncmp(line, "OFFSET ", 7) == 0)
  {
    int val = atoi(line + 7);
    setOffset(val);
    return;
  }
}

void handleSerialInput()
{
  static char buf[32];
  static uint8_t pos = 0;

  while (Serial.available() > 0)
  {
    char c = Serial.read();
    if (c == '\r' || c == '\n')
    {
      if (pos > 0)
      {
        buf[pos] = '\0';
        processLine(buf);
        pos = 0;
      }
    }
    else if (pos < sizeof(buf) - 1)
    {
      buf[pos++] = c;
    }
  }
}

// === setup / loop ===
void setup()
{
  Serial.begin(115200);
  ns_stick.begin();
  initLanes();
}

void loop()
{
  handleSerialInput();
  updateEffects();
  delay(frameDelayMs);
}
