#include "config.h"
#include "esp_now_protocol.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_coexist.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

Preferences prefs;
WebServer ledWebServer(WEB_SERVER_PORT_LED);
bool webServerStarted = false;

// ==============================================================================
// Node Identity, Wireless Mode & Stealth State (Persisted to NVS Flash)
// ==============================================================================
uint8_t cfgNodeId = DEFAULT_NODE_ID;       // 1 = NODE_CHEST, 2 = NODE_BACK
uint8_t cfgCommMode = DEFAULT_COMM_MODE;   // 0 = COMM_MODE_BLE (Default), 1 = COMM_MODE_WIFI
int8_t pendingCommModeSwitch = -1;
unsigned long pendingCommSwitchAtMs = 0;
bool suitStealthMode = false;              // True = All LEDs OFF (synced with Webshooter & Gimbal)

// ==============================================================================
// Dynamic Suit Mesh LED Settings (Synced with Gimbal Hub / BLE App & NVS)
// ==============================================================================
uint8_t cfgStandbyR = 255, cfgStandbyG = 255, cfgStandbyB = 255;
uint8_t cfgDeployedR = 255, cfgDeployedG = 0, cfgDeployedB = 0; // Pure Red when deployed/armed
uint8_t cfgLedPattern = PATTERN_BREATH_APPLE;
uint8_t cfgArmedPattern = PATTERN_ARMED_SOFT_FLASH; // Default: Soft Armed Red Flash when either WS or Gimbal is armed
uint8_t cfgWaveMode = WAVE_LOW_TO_HIGH;             // Default: Low (Webshooter) -> Mid (Vest) -> High (Gimbal)
uint8_t cfgWaveOffsetPct = DEFAULT_WAVE_OFFSET_PCT; // Default: 30% phase offset per vertical tier
uint8_t cfgMinBrightness = RGB_MIN_BRIGHTNESS;
uint8_t cfgMaxBrightness = RGB_MAX_BRIGHTNESS;
uint16_t cfgBreathPeriodMs = RGB_BREATH_PERIOD_MS;
uint16_t cfgArmedPeriodMs = DEFAULT_ARMED_PERIOD_MS;

// Mesh Synchronization State
int32_t breathPhaseOffset = 0;
int32_t armedPhaseOffset = 0;
bool hasSyncedPhase = false;
bool hasSyncedArmedPhase = false;

uint8_t gimbalReportedState = SUIT_LED_STANDBY;
uint8_t gimbalActualState = 0;
bool gimbalZeroed = false;
bool wsReportedArmed = false;
bool wsReportedFiring = false;
uint16_t wsReportedVbatMv = 0;
uint8_t localTestStateOverride = 0xFF; // 0xFF = Auto (follow mesh), 0..2 = manual/app override

unsigned long lastGimbalSyncMs = 0;
unsigned long lastWebshooterSyncMs = 0;
unsigned long lastLocalStealthToggleMs = 0;
unsigned long lastLocalBleEditMs = 0;
unsigned long lastNodeTelemMs = 0;
unsigned long lastBleNotifyMs = 0;
uint32_t nodeSequence = 0;
float nodeVbatFilteredMv = 0.0f;

// Temporary Visual Flash State
unsigned long rgbFlashEndTime = 0;
uint8_t flashR = 0, flashG = 0, flashB = 0;

// Onboard BOOT Button State (GPIO 9 - Active LOW)
bool lastButtonRaw = HIGH;
bool buttonIsPressed = false;
unsigned long lastDebounceTime = 0;
unsigned long buttonPressStartTime = 0;
bool buttonHoldHandled = false;

// BLE Objects
BLEServer *pBleServer = nullptr;
BLECharacteristic *pStatusChar = nullptr;
BLECharacteristic *pCmdChar = nullptr;
bool bleClientConnected = false;
String pendingBleCommand = "";

// Forward Declarations
void applyWirelessCommMode(uint8_t mode, bool saveToNvs = true);
void broadcastLedSyncFromNode();
void broadcastStealthCommand(bool stealthOn);
void sendGimbalActionFromNode(GimbalCommand cmd);
void cycleNodeRole();
uint8_t getEffectiveSuitState();
String buildBleStatusJson();

// ==============================================================================
// CIE 1931 Perceptual Lightness Curve Table
// ==============================================================================
const uint8_t cie1931_table[256] PROGMEM = {
    0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,
    2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,   4,
    4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,   7,
    7,   7,   7,   8,   8,   8,   8,   9,   9,   9,  10,  10,  10,  10,  11,  11,
   11,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  17,  17,
   17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  24,  24,  25,
   25,  26,  26,  27,  28,  28,  29,  29,  30,  31,  31,  32,  32,  33,  34,  34,
   35,  36,  37,  37,  38,  39,  39,  40,  41,  42,  43,  43,  44,  45,  46,  47,
   47,  48,  49,  50,  51,  52,  53,  54,  54,  55,  56,  57,  58,  59,  60,  61,
   62,  63,  64,  65,  66,  67,  68,  70,  71,  72,  73,  74,  75,  76,  77,  79,
   80,  81,  82,  83,  85,  86,  87,  88,  90,  91,  92,  94,  95,  96,  98,  99,
  100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
  124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
  152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
  183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
  218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255
};

const char* getNodeName(uint8_t id) {
  switch (id) {
    case NODE_CHEST:  return "CHEST LED";
    case NODE_BACK:   return "BACK LED";
    case NODE_HELMET: return "HELMET LED";
    default:          return "AUX LED";
  }
}

void triggerRgbFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms) {
  flashR = r;
  flashG = g;
  flashB = b;
  rgbFlashEndTime = millis() + duration_ms;
}

// ==============================================================================
// 3-MOSFET Hardware PWM Driver (GPIO 5 = R, GPIO 6 = G, GPIO 7 = B)
// Active HIGH: Duty 0 = LOW (OFF), Duty 255 = HIGH (Full ON)
// ==============================================================================
void initMosfetRgbPwm() {
  pinMode(PIN_MOSFET_R, OUTPUT);
  pinMode(PIN_MOSFET_G, OUTPUT);
  pinMode(PIN_MOSFET_B, OUTPUT);
  digitalWrite(PIN_MOSFET_R, LOW);
  digitalWrite(PIN_MOSFET_G, LOW);
  digitalWrite(PIN_MOSFET_B, LOW);

  ledcSetup(LEDC_CH_R, MOSFET_PWM_FREQ_HZ, MOSFET_PWM_RES_BITS);
  ledcSetup(LEDC_CH_G, MOSFET_PWM_FREQ_HZ, MOSFET_PWM_RES_BITS);
  ledcSetup(LEDC_CH_B, MOSFET_PWM_FREQ_HZ, MOSFET_PWM_RES_BITS);

  ledcWrite(LEDC_CH_R, 0);
  ledcWrite(LEDC_CH_G, 0);
  ledcWrite(LEDC_CH_B, 0);

  ledcAttachPin(PIN_MOSFET_R, LEDC_CH_R);
  ledcAttachPin(PIN_MOSFET_G, LEDC_CH_G);
  ledcAttachPin(PIN_MOSFET_B, LEDC_CH_B);
}

void writeMosfetRgb(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LEDC_CH_R, r);
  ledcWrite(LEDC_CH_G, g);
  ledcWrite(LEDC_CH_B, b);
}

// ==============================================================================
// Pattern & Traveling Wave Engine (Locked in Phase with Gimbal Hub / Webshooter)
// ==============================================================================
uint32_t getSyncedBreathPhaseMs() {
  uint16_t period = (cfgBreathPeriodMs >= 300) ? cfgBreathPeriodMs : 3600;
  int32_t rawSynced = (int32_t)(millis() % period) + breathPhaseOffset;
  while (rawSynced < 0) rawSynced += period;
  return (uint32_t)(rawSynced % period);
}

uint32_t getSyncedArmedPhaseMs() {
  uint16_t period = (cfgArmedPeriodMs >= 250) ? cfgArmedPeriodMs : DEFAULT_ARMED_PERIOD_MS;
  int32_t rawSynced = (int32_t)(millis() % period) + armedPhaseOffset;
  while (rawSynced < 0) rawSynced += period;
  return (uint32_t)(rawSynced % period);
}

uint8_t computePatternBrightness(uint8_t pattern, uint32_t phaseMs, uint16_t periodMs, uint8_t minB, uint8_t maxB) {
  if (pattern == PATTERN_STEALTH_OFF) return 0;
  if (pattern == PATTERN_SOLID) return maxB;

  float norm = evaluateSuitPatternNorm(pattern, phaseMs, periodMs);
  norm = constrain(norm, 0.0f, 1.0f);
  uint8_t index = (uint8_t)(norm * 255.0f);
  uint8_t corrected = pgm_read_byte(&cie1931_table[index]);
  return minB + (uint8_t)(((uint32_t)corrected * (maxB - minB)) / 255);
}

uint8_t getEffectiveSuitState() {
  if (localTestStateOverride <= SUIT_LED_FIRING) {
    return localTestStateOverride;
  }
  unsigned long now = millis();
  bool gimOnline = (lastGimbalSyncMs > 0) && (now - lastGimbalSyncMs < 2500);
  bool wsOnline  = (lastWebshooterSyncMs > 0) && (now - lastWebshooterSyncMs < 2500);

  if ((gimOnline && gimbalReportedState == SUIT_LED_FIRING) || (wsOnline && wsReportedFiring)) {
    return SUIT_LED_FIRING;
  }
  // Armed when EITHER Webshooter OR Shoulder Mounted Gimbal is armed!
  if ((gimOnline && gimbalReportedState == SUIT_LED_DEPLOYED) || (wsOnline && wsReportedArmed)) {
    return SUIT_LED_DEPLOYED;
  }
  return SUIT_LED_STANDBY;
}

void updateRgbLed() {
  unsigned long now = millis();

  // Rate-limit PWM updates to 100Hz (every 10ms) for ultra-smooth MOSFET fades
  static unsigned long lastLedFrameMs = 0;
  if (now - lastLedFrameMs < 10) {
    return;
  }
  lastLedFrameMs = now;

  // 1. High-priority flash (e.g. Zeroing cyan confirm or Node role switch)
  if (now < rgbFlashEndTime) {
    writeMosfetRgb(flashR, flashG, flashB);
    return;
  }

  uint8_t activeState = getEffectiveSuitState();

  // 2. Firing state: Intense bright Red
  if (activeState == SUIT_LED_FIRING) {
    writeMosfetRgb(255, 0, 0);
    return;
  }

  // 3. Suit-wide Stealth Mode
  if (suitStealthMode) {
    writeMosfetRgb(0, 0, 0);
    return;
  }

  // 4. Armed State (Either Webshooter OR Shoulder Gimbal is Armed)
  if (activeState == SUIT_LED_DEPLOYED) {
    if (cfgArmedPattern == PATTERN_STEALTH_OFF) {
      writeMosfetRgb(0, 0, 0);
      return;
    }
    uint16_t armedPeriod = (cfgArmedPeriodMs >= 250) ? cfgArmedPeriodMs : DEFAULT_ARMED_PERIOD_MS;
    uint32_t baseArmedPhase = getSyncedArmedPhaseMs();
    uint32_t nodePhase = applySuitWaveOffset(baseArmedPhase, armedPeriod, cfgWaveMode, cfgWaveOffsetPct, TIER_VEST);
    uint8_t level = computePatternBrightness(cfgArmedPattern, nodePhase, armedPeriod, cfgMinBrightness, cfgMaxBrightness);

    uint8_t baseR = cfgDeployedR, baseG = cfgDeployedG, baseB = cfgDeployedB;
    if (cfgArmedPattern == PATTERN_RAINBOW_FLOW) {
      float hue = (360.0f * (float)nodePhase) / (float)armedPeriod;
      suitHsvToRgb(hue, 1.0f, 1.0f, baseR, baseG, baseB);
    }

    uint8_t r = (uint8_t)(((uint32_t)baseR * level) / 255);
    uint8_t g = (uint8_t)(((uint32_t)baseG * level) / 255);
    uint8_t b = (uint8_t)(((uint32_t)baseB * level) / 255);
    writeMosfetRgb(r, g, b);
    return;
  }

  // 5. Standby / Disarmed State (with Low -> Mid -> High Traveling Wave support)
  if (cfgLedPattern == PATTERN_STEALTH_OFF) {
    writeMosfetRgb(0, 0, 0);
    return;
  }

  uint16_t period = (cfgBreathPeriodMs >= 300) ? cfgBreathPeriodMs : 3600;
  uint32_t basePhase = getSyncedBreathPhaseMs();
  // Vest is TIER_VEST (Mid = 1): pulses after Webshooter (Low = 0) and before Shoulder Gimbal (High = 2)
  uint32_t vestPhase = applySuitWaveOffset(basePhase, period, cfgWaveMode, cfgWaveOffsetPct, TIER_VEST);
  uint8_t level = computePatternBrightness(cfgLedPattern, vestPhase, period, cfgMinBrightness, cfgMaxBrightness);

  uint8_t baseR = cfgStandbyR, baseG = cfgStandbyG, baseB = cfgStandbyB;
  if (cfgLedPattern == PATTERN_RAINBOW_FLOW) {
    float hue = (360.0f * (float)vestPhase) / (float)period;
    suitHsvToRgb(hue, 1.0f, 1.0f, baseR, baseG, baseB);
  }

  uint8_t r = (uint8_t)(((uint32_t)baseR * level) / 255);
  uint8_t g = (uint8_t)(((uint32_t)baseG * level) / 255);
  uint8_t b = (uint8_t)(((uint32_t)baseB * level) / 255);
  writeMosfetRgb(r, g, b);
}

// ==============================================================================
// ESP-NOW Suit Mesh Receiver & Node Heartbeat Transmitter
// ==============================================================================
void syncPhaseToTarget(uint16_t targetPhaseMs, bool forceSnap) {
  uint16_t period = (cfgBreathPeriodMs >= 300) ? cfgBreathPeriodMs : 3600;
  int32_t localPhase = (int32_t)(millis() % period);
  int32_t targetPhase = (int32_t)(targetPhaseMs % period);
  int32_t targetOffset = targetPhase - localPhase;

  while (targetOffset > (int32_t)period / 2) targetOffset -= period;
  while (targetOffset < -(int32_t)period / 2) targetOffset += period;

  if (!hasSyncedPhase || forceSnap) {
    breathPhaseOffset = targetOffset;
    hasSyncedPhase = true;
  } else {
    breathPhaseOffset += (int32_t)((targetOffset - breathPhaseOffset) * 0.30f);
  }
}

void syncArmedPhaseToTarget(uint16_t targetArmedPhaseMs, bool forceSnap) {
  uint16_t period = (cfgArmedPeriodMs >= 250) ? cfgArmedPeriodMs : DEFAULT_ARMED_PERIOD_MS;
  int32_t localPhase = (int32_t)(millis() % period);
  int32_t targetPhase = (int32_t)(targetArmedPhaseMs % period);
  int32_t targetOffset = targetPhase - localPhase;

  while (targetOffset > (int32_t)period / 2) targetOffset -= period;
  while (targetOffset < -(int32_t)period / 2) targetOffset += period;

  if (!hasSyncedArmedPhase || forceSnap) {
    armedPhaseOffset = targetOffset;
    hasSyncedArmedPhase = true;
  } else {
    armedPhaseOffset += (int32_t)((targetOffset - armedPhaseOffset) * 0.30f);
  }
}

void broadcastStealthCommand(bool stealthOn) {
  GimbalMessage msg = {};
  msg.command = stealthOn ? CMD_STEALTH_ON : CMD_STEALTH_OFF;
  msg.duration_ms = 0;
  msg.sequence = ++nodeSequence;
  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddr, (const uint8_t *)&msg, sizeof(msg));
}

void sendGimbalActionFromNode(GimbalCommand cmd) {
  GimbalMessage msg = {};
  msg.command = (uint8_t)cmd;
  msg.duration_ms = (cmd == CMD_FIRE) ? 2000 : 0;
  msg.sequence = ++nodeSequence;
  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddr, (const uint8_t *)&msg, sizeof(msg));
}

void broadcastLedSyncFromNode() {
  uint16_t period = (cfgBreathPeriodMs >= 300) ? cfgBreathPeriodMs : 3600;
  uint16_t armedPeriod = (cfgArmedPeriodMs >= 250) ? cfgArmedPeriodMs : DEFAULT_ARMED_PERIOD_MS;

  SuitLedSyncMessage syncMsg = {};
  syncMsg.command = CMD_LED_SYNC;
  syncMsg.suit_state = getEffectiveSuitState();
  syncMsg.pattern = suitStealthMode ? PATTERN_STEALTH_OFF : cfgLedPattern;
  syncMsg.armed_pattern = suitStealthMode ? PATTERN_STEALTH_OFF : cfgArmedPattern;
  syncMsg.wave_mode = cfgWaveMode;
  syncMsg.wave_offset_pct = cfgWaveOffsetPct;
  syncMsg.stealth_mode = (suitStealthMode || cfgLedPattern == PATTERN_STEALTH_OFF) ? 1 : 0;
  syncMsg.gimbal_actual_state = gimbalActualState;
  syncMsg.gimbal_zeroed = gimbalZeroed ? 1 : 0;
  syncMsg.standby_r = cfgStandbyR;
  syncMsg.standby_g = cfgStandbyG;
  syncMsg.standby_b = cfgStandbyB;
  syncMsg.deployed_r = cfgDeployedR;
  syncMsg.deployed_g = cfgDeployedG;
  syncMsg.deployed_b = cfgDeployedB;
  syncMsg.min_brightness = cfgMinBrightness;
  syncMsg.max_brightness = cfgMaxBrightness;
  syncMsg.breath_period_ms = period;
  syncMsg.armed_period_ms = armedPeriod;
  syncMsg.breath_phase_ms = (uint16_t)getSyncedBreathPhaseMs();
  syncMsg.armed_phase_ms = (uint16_t)getSyncedArmedPhaseMs();
  syncMsg.sequence = ++nodeSequence;

  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddr, (const uint8_t *)&syncMsg, sizeof(syncMsg));
}

void saveLocalLedSettingsToNvs() {
  prefs.putBool("stealth", suitStealthMode);
  prefs.putUChar("sr", cfgStandbyR);
  prefs.putUChar("sg", cfgStandbyG);
  prefs.putUChar("sb", cfgStandbyB);
  prefs.putUChar("dr", cfgDeployedR);
  prefs.putUChar("dg", cfgDeployedG);
  prefs.putUChar("db", cfgDeployedB);
  prefs.putUChar("pattern", cfgLedPattern);
  prefs.putUChar("arm_pat", cfgArmedPattern);
  prefs.putUChar("wave_m", cfgWaveMode);
  prefs.putUChar("wave_o", cfgWaveOffsetPct);
  prefs.putUChar("min_b", cfgMinBrightness);
  prefs.putUChar("max_b", cfgMaxBrightness);
  prefs.putUShort("period", cfgBreathPeriodMs);
  prefs.putUShort("arm_per", cfgArmedPeriodMs);
}

void onEspNowRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  // 1. Primary Authority: Gimbal Hub SuitLedSyncMessage (CMD_LED_SYNC)
  if (len == sizeof(SuitLedSyncMessage) && incomingData[0] == CMD_LED_SYNC) {
    SuitLedSyncMessage syncMsg;
    memcpy(&syncMsg, incomingData, sizeof(SuitLedSyncMessage));
    lastGimbalSyncMs = millis();
    gimbalActualState = syncMsg.gimbal_actual_state;
    gimbalZeroed = (syncMsg.gimbal_zeroed != 0);

    if (syncMsg.suit_state == SUIT_LED_FLASH) {
      triggerRgbFlash(syncMsg.flash_r, syncMsg.flash_g, syncMsg.flash_b, 250);
    } else {
      gimbalReportedState = syncMsg.suit_state;
    }

    // Ignore incoming LED config overwrite for 1.5s if user just edited settings via local BLE on this Vest
    if (millis() - lastLocalBleEditMs < 1500) {
      return;
    }

    bool newStealth = suitStealthMode;
    if (millis() - lastLocalStealthToggleMs > 1200) {
      newStealth = (syncMsg.stealth_mode != 0) || (syncMsg.pattern == PATTERN_STEALTH_OFF);
    }

    uint16_t newPeriod = (syncMsg.breath_period_ms >= 300) ? syncMsg.breath_period_ms : 3600;
    uint16_t newArmedPeriod = (syncMsg.armed_period_ms >= 250) ? syncMsg.armed_period_ms : DEFAULT_ARMED_PERIOD_MS;
    uint8_t newWaveOffset = constrain(syncMsg.wave_offset_pct, 0, 50);

    bool settingsChanged =
        (suitStealthMode != newStealth) ||
        (cfgStandbyR != syncMsg.standby_r) || (cfgStandbyG != syncMsg.standby_g) || (cfgStandbyB != syncMsg.standby_b) ||
        (cfgDeployedR != syncMsg.deployed_r) || (cfgDeployedG != syncMsg.deployed_g) || (cfgDeployedB != syncMsg.deployed_b) ||
        (cfgLedPattern != syncMsg.pattern) || (cfgArmedPattern != syncMsg.armed_pattern) ||
        (cfgWaveMode != syncMsg.wave_mode) || (cfgWaveOffsetPct != newWaveOffset) ||
        (cfgMinBrightness != syncMsg.min_brightness) || (cfgMaxBrightness != syncMsg.max_brightness) ||
        (cfgBreathPeriodMs != newPeriod) || (cfgArmedPeriodMs != newArmedPeriod);

    suitStealthMode = newStealth;
    cfgStandbyR = syncMsg.standby_r;
    cfgStandbyG = syncMsg.standby_g;
    cfgStandbyB = syncMsg.standby_b;
    cfgDeployedR = syncMsg.deployed_r;
    cfgDeployedG = syncMsg.deployed_g;
    cfgDeployedB = syncMsg.deployed_b;
    cfgLedPattern = syncMsg.pattern;
    cfgArmedPattern = syncMsg.armed_pattern;
    cfgWaveMode = syncMsg.wave_mode;
    cfgWaveOffsetPct = newWaveOffset;
    cfgMinBrightness = syncMsg.min_brightness;
    cfgMaxBrightness = syncMsg.max_brightness;
    cfgBreathPeriodMs = newPeriod;
    cfgArmedPeriodMs = newArmedPeriod;

    syncPhaseToTarget(syncMsg.breath_phase_ms, settingsChanged);
    syncArmedPhaseToTarget(syncMsg.armed_phase_ms, settingsChanged);

    if (settingsChanged) {
      saveLocalLedSettingsToNvs();
      Serial.printf("[MESH SYNC] Standby:(%d,%d,%d) Pat:%d | Armed:(%d,%d,%d) ArmPat:%d (%dms) | Wave:%d (%d%%) | Stealth:%s\n",
                    cfgStandbyR, cfgStandbyG, cfgStandbyB, cfgLedPattern,
                    cfgDeployedR, cfgDeployedG, cfgDeployedB, cfgArmedPattern, cfgArmedPeriodMs,
                    cfgWaveMode, cfgWaveOffsetPct, suitStealthMode ? "ON" : "OFF");
    }
    return;
  }

  // 2. Webshooter Telemetry
  if (len == sizeof(WebshooterTelemetry) && incomingData[0] == CMD_TELEMETRY) {
    WebshooterTelemetry wsTelem;
    memcpy(&wsTelem, incomingData, sizeof(WebshooterTelemetry));
    lastWebshooterSyncMs = millis();

    wsReportedVbatMv = wsTelem.vbat_mv;
    wsReportedFiring = (wsTelem.gimbal_firing != 0);
    wsReportedArmed  = (wsTelem.webshooter_pos == 0) || (wsTelem.gimbal_armed != 0);

    if (lastGimbalSyncMs == 0 || (millis() - lastGimbalSyncMs > 1500)) {
      bool wsStealth = (wsTelem.stealth_mode != 0);
      if (suitStealthMode != wsStealth && (millis() - lastLocalStealthToggleMs > 1200)) {
        suitStealthMode = wsStealth;
        prefs.putBool("stealth", suitStealthMode);
      }
      syncPhaseToTarget(wsTelem.breath_phase_ms, false);
    }
    return;
  }

  // 3. Direct Instant Action Commands from Webshooter
  if (len == sizeof(GimbalMessage)) {
    GimbalMessage msg;
    memcpy(&msg, incomingData, sizeof(GimbalMessage));
    bool gimbalOnline = (lastGimbalSyncMs > 0) && (millis() - lastGimbalSyncMs < 1500);
    switch (msg.command) {
      case CMD_DEPLOY:
        if (!gimbalOnline || gimbalZeroed) {
          gimbalReportedState = SUIT_LED_DEPLOYED;
          wsReportedArmed = true;
        }
        break;
      case CMD_DISARM:
        gimbalReportedState = SUIT_LED_STANDBY;
        wsReportedArmed = false;
        break;
      case CMD_FIRE:
        gimbalReportedState = SUIT_LED_FIRING;
        wsReportedFiring = true;
        break;
      case CMD_STOP_FIRE:
        gimbalReportedState = SUIT_LED_DEPLOYED;
        wsReportedFiring = false;
        break;
      case CMD_CALIBRATE:
        triggerRgbFlash(0, 255, 255, 250);
        break;
      case CMD_STEALTH_ON:
        suitStealthMode = true;
        prefs.putBool("stealth", true);
        writeMosfetRgb(0, 0, 0);
        Serial.println("[STEALTH] CMD_STEALTH_ON received -> All LEDs OFF!");
        break;
      case CMD_STEALTH_OFF:
        suitStealthMode = false;
        prefs.putBool("stealth", false);
        Serial.println("[STEALTH] CMD_STEALTH_OFF received -> LEDs ACTIVE!");
        break;
      default:
        break;
    }
  }
}

void broadcastNodeTelemetry() {
  uint32_t rawMv = (uint32_t)(analogReadMilliVolts(PIN_BATTERY_ADC) * BATTERY_DIVIDER_RATIO);
  if (nodeVbatFilteredMv < 100.0f) {
    nodeVbatFilteredMv = rawMv;
  } else {
    nodeVbatFilteredMv = nodeVbatFilteredMv * 0.85f + (float)rawMv * 0.15f;
  }

  SuitNodeTelemetry telem = {};
  telem.command = CMD_LED_NODE_TELEM;
  telem.node_id = cfgNodeId;
  telem.vbat_mv = (uint16_t)nodeVbatFilteredMv;
  telem.num_leds = 1; // Single 3-MOSFET RGB LED assembly (GPIO 5, 6, 7)
  telem.sequence = ++nodeSequence;

  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddr, (const uint8_t *)&telem, sizeof(telem));
}

// ==============================================================================
// BLE Command Parser & Status JSON Builder (Shared Schema with leds.html)
// ==============================================================================
String buildBleStatusJson() {
  unsigned long now = millis();
  bool gimOk = (lastGimbalSyncMs > 0) && (now - lastGimbalSyncMs < 2500);
  bool wsOk  = (lastWebshooterSyncMs > 0) && (now - lastWebshooterSyncMs < 2500);
  uint8_t effState = getEffectiveSuitState();
  const char* gStateStr = (gimbalActualState == 2) ? "TRACKING" :
                          (gimbalActualState == 1) ? "DEPLOYING" :
                          (gimbalActualState == 3) ? "FOLDING" : "FOLDED";

  String json = "{";
  json += "\"node\":\"" + String(cfgNodeId == NODE_CHEST ? "VEST_CHEST" : "VEST_BACK") + "\",";
  json += "\"comm\":" + String(cfgCommMode) + ",";
  json += "\"g\":{\"st\":\"" + String(gStateStr) + "\",\"v\":" + String((int)nodeVbatFilteredMv) +
          ",\"y\":90.0,\"p\":110.0,\"nk\":" + String(gimOk ? 1 : 0) +
          ",\"sh\":" + String(gimOk ? 1 : 0) + ",\"z\":" + String(gimbalZeroed ? 1 : 0) +
          ",\"rc\":0,\"f\":" + String(effState == SUIT_LED_FIRING ? 1 : 0) + "},";
  json += "\"ws\":{\"c\":" + String(wsOk ? 1 : 0) + ",\"v\":" + String(wsReportedVbatMv) +
          ",\"pos\":" + String(wsReportedArmed ? 0 : 1) + ",\"arm\":" + String(wsReportedArmed ? 1 : 0) +
          ",\"f\":" + String(wsReportedFiring ? 1 : 0) + "},";
  json += "\"m\":{\"c\":" + String(cfgNodeId == NODE_CHEST ? 1 : 0) +
          ",\"b\":" + String(cfgNodeId == NODE_BACK ? 1 : 0) +
          ",\"g\":" + String(gimOk ? 1 : 0) + "},";
  json += "\"s\":{\"sr\":" + String(cfgStandbyR) + ",\"sg\":" + String(cfgStandbyG) + ",\"sb\":" + String(cfgStandbyB) +
          ",\"dr\":" + String(cfgDeployedR) + ",\"dg\":" + String(cfgDeployedG) + ",\"db\":" + String(cfgDeployedB) +
          ",\"pat\":" + String(suitStealthMode ? PATTERN_STEALTH_OFF : cfgLedPattern) +
          ",\"apat\":" + String(cfgArmedPattern) +
          ",\"wm\":" + String(cfgWaveMode) +
          ",\"wo\":" + String(cfgWaveOffsetPct) +
          ",\"min\":" + String(cfgMinBrightness) +
          ",\"max\":" + String(cfgMaxBrightness) +
          ",\"per\":" + String(cfgBreathPeriodMs) +
          ",\"aper\":" + String(cfgArmedPeriodMs) +
          ",\"ov\":" + String(localTestStateOverride) +
          ",\"st\":" + String(effState) + "}";
  json += "}";
  return json;
}

void handleBleCommandString(const String &cmdStr) {
  if (cmdStr.startsWith("L:")) {
    // Format: L:sr,sg,sb,dr,dg,db,pat,apat,wm,wo,min,max,per,aper,ov
    int vals[15] = {0};
    int idx = 0;
    int start = 2;
    while (idx < 15 && start <= (int)cmdStr.length()) {
      int comma = cmdStr.indexOf(',', start);
      if (comma == -1) comma = cmdStr.length();
      vals[idx++] = cmdStr.substring(start, comma).toInt();
      start = comma + 1;
    }
    if (idx >= 14) {
      cfgStandbyR = constrain(vals[0], 0, 255);
      cfgStandbyG = constrain(vals[1], 0, 255);
      cfgStandbyB = constrain(vals[2], 0, 255);
      cfgDeployedR = constrain(vals[3], 0, 255);
      cfgDeployedG = constrain(vals[4], 0, 255);
      cfgDeployedB = constrain(vals[5], 0, 255);
      cfgLedPattern = constrain(vals[6], 0, 8);
      suitStealthMode = (cfgLedPattern == PATTERN_STEALTH_OFF);
      cfgArmedPattern = constrain(vals[7], 0, 8);
      cfgWaveMode = constrain(vals[8], 0, 2);
      cfgWaveOffsetPct = constrain(vals[9], 0, 50);
      cfgMinBrightness = constrain(vals[10], 0, 255);
      cfgMaxBrightness = constrain(vals[11], 1, 255);
      cfgBreathPeriodMs = constrain(vals[12], 300, 8000);
      cfgArmedPeriodMs = constrain(vals[13], 250, 5000);
      if (idx >= 15) {
        localTestStateOverride = (vals[14] >= 0 && vals[14] <= 2) ? (uint8_t)vals[14] : 0xFF;
      }
      lastLocalBleEditMs = millis();
      saveLocalLedSettingsToNvs();
      broadcastLedSyncFromNode();
      Serial.printf("[BLE CMD] Updated Suit LEDs: Standby(%d,%d,%d) Pat:%d | Armed(%d,%d,%d) ArmPat:%d | Wave:%d (%d%%)\n",
                    cfgStandbyR, cfgStandbyG, cfgStandbyB, cfgLedPattern,
                    cfgDeployedR, cfgDeployedG, cfgDeployedB, cfgArmedPattern,
                    cfgWaveMode, cfgWaveOffsetPct);
    }
  } else if (cmdStr == "cmd:comm_ble") {
    pendingCommModeSwitch = COMM_MODE_BLE;
    pendingCommSwitchAtMs = millis() + 250;
  } else if (cmdStr == "cmd:comm_wifi") {
    pendingCommModeSwitch = COMM_MODE_WIFI;
    pendingCommSwitchAtMs = millis() + 250;
  } else if (cmdStr == "cmd:role_toggle") {
    cycleNodeRole();
  } else if (cmdStr == "cmd:deploy") {
    sendGimbalActionFromNode(CMD_DEPLOY);
  } else if (cmdStr == "cmd:fold") {
    sendGimbalActionFromNode(CMD_DISARM);
  } else if (cmdStr == "cmd:zero") {
    sendGimbalActionFromNode(CMD_CALIBRATE);
  } else if (cmdStr == "cmd:fire_start") {
    sendGimbalActionFromNode(CMD_FIRE);
  } else if (cmdStr == "cmd:fire_stop") {
    sendGimbalActionFromNode(CMD_STOP_FIRE);
  }
}

class SuitBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleClientConnected = true;
    Serial.println("[BLE] Web Bluetooth Client Connected!");
  }
  void onDisconnect(BLEServer* pServer) override {
    bleClientConnected = false;
    Serial.println("[BLE] Web Bluetooth Client Disconnected -> Restarting Advertising...");
    BLEDevice::startAdvertising();
  }
};

class SuitBleCmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string rxValue = pCharacteristic->getValue();
    if (!rxValue.empty()) {
      pendingBleCommand = String(rxValue.c_str());
    }
  }
};

void initBleGattServer() {
  const char* devName = (cfgNodeId == NODE_CHEST) ? "Suit-LED-Vest" : "Suit-LED-Back";
  BLEDevice::init(devName);
  BLEDevice::setMTU(517);

  pBleServer = BLEDevice::createServer();
  pBleServer->setCallbacks(new SuitBleServerCallbacks());

  BLEService *pService = pBleServer->createService(SUIT_BLE_SERVICE_UUID);

  pStatusChar = pService->createCharacteristic(
      SUIT_BLE_CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusChar->addDescriptor(new BLE2902());

  pCmdChar = pService->createCharacteristic(
      SUIT_BLE_CHAR_CMD_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCmdChar->setCallbacks(new SuitBleCmdCallbacks());

  String initJson = buildBleStatusJson();
  pStatusChar->setValue(initJson.c_str());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SUIT_BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.printf("[BLE GATT] Advertising as '%s' (Service: %s)\n", devName, SUIT_BLE_SERVICE_UUID);
}

// ==============================================================================
// Wi-Fi / BLE Coexistence & Runtime Mode Switching
// ==============================================================================
void setupLocalWebServer() {
  if (webServerStarted) return;

  ledWebServer.on("/api/status", HTTP_GET, []() {
    ledWebServer.sendHeader("Access-Control-Allow-Origin", "*");
    ledWebServer.send(200, "application/json", buildBleStatusJson());
  });

  ledWebServer.on("/api/cmd", HTTP_POST, []() {
    ledWebServer.sendHeader("Access-Control-Allow-Origin", "*");
    if (ledWebServer.hasArg("action")) {
      String act = ledWebServer.arg("action");
      if (act == "comm_mode") {
        int m = ledWebServer.arg("mode").toInt();
        pendingCommModeSwitch = (m == 1) ? COMM_MODE_WIFI : COMM_MODE_BLE;
        pendingCommSwitchAtMs = millis() + 350;
        ledWebServer.send(200, "application/json",
                          String("{\"status\":\"ok\",\"comm_mode\":") + pendingCommModeSwitch + "}");
        return;
      }
    }
    ledWebServer.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  ledWebServer.begin();
  webServerStarted = true;
}

void applyWirelessCommMode(uint8_t mode, bool saveToNvs) {
  cfgCommMode = (mode == COMM_MODE_WIFI) ? COMM_MODE_WIFI : COMM_MODE_BLE;
  if (saveToNvs) {
    prefs.putUChar("comm_v2", cfgCommMode);
  }

  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

  if (cfgCommMode == COMM_MODE_WIFI) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID_LED, WIFI_AP_PASS_LED, ESPNOW_WIFI_CHANNEL, 0, 4);
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    setupLocalWebServer();
    Serial.printf("\n[WIRELESS MODE] Switched to WI-FI AP + BLE (SSID: '%s' | http://192.168.4.1)\n", WIFI_AP_SSID_LED);
  } else {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    Serial.println("\n[WIRELESS MODE] Switched to BLE MODE (Wi-Fi SoftAP OFF | BLE GATT + ESP-NOW Ch 1 ON)");
  }

  // Re-bind broadcast peer to active interface
  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_del_peer(broadcastAddr);

  esp_now_peer_info_t peerInfo = {};
  memset(&peerInfo, 0, sizeof(peerInfo));
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = (cfgCommMode == COMM_MODE_WIFI) ? 0 : ESPNOW_WIFI_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = (cfgCommMode == COMM_MODE_WIFI) ? WIFI_IF_AP : WIFI_IF_STA;
  esp_now_add_peer(&peerInfo);

  if (pBleServer != nullptr && !bleClientConnected) {
    BLEDevice::startAdvertising();
  }
}

void initEspNowAndWireless() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Error: Initialization failed!");
    return;
  }

  esp_now_register_recv_cb(onEspNowRecv);
  initBleGattServer();
  applyWirelessCommMode(cfgCommMode, false);

  Serial.printf("[ESP-NOW] Suit LED Mesh Node Online (Role: %s | Tier: MID | Ch: %d)\n",
                getNodeName(cfgNodeId), ESPNOW_WIFI_CHANNEL);
}

// ==============================================================================
// BOOT Button & Serial CLI Configuration
// ==============================================================================
void cycleNodeRole() {
  cfgNodeId = (cfgNodeId == NODE_CHEST) ? NODE_BACK : NODE_CHEST;
  prefs.putUChar("node_id", cfgNodeId);

  if (cfgNodeId == NODE_CHEST) {
    triggerRgbFlash(0, 240, 255, 400); // Cyan flash = CHEST LED
  } else {
    triggerRgbFlash(180, 0, 255, 400); // Purple flash = BACK LED
  }
  Serial.printf("\n[NODE ROLE] Switched Node Role to: %s (ID=%d)\n", getNodeName(cfgNodeId), cfgNodeId);
  broadcastNodeTelemetry();
}

void processBootButton() {
  bool raw = digitalRead(PIN_BUTTON_BOOT);
  unsigned long now = millis();

  if (raw != lastButtonRaw) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > 35) {
    bool pressed = (raw == LOW);
    if (pressed != buttonIsPressed) {
      buttonIsPressed = pressed;
      if (buttonIsPressed) {
        buttonPressStartTime = now;
        buttonHoldHandled = false;
      } else if (!buttonHoldHandled) {
        suitStealthMode = !suitStealthMode;
        lastLocalStealthToggleMs = now;
        prefs.putBool("stealth", suitStealthMode);
        broadcastStealthCommand(suitStealthMode);
        Serial.printf("[BUTTON] Stealth Mode: %s (Broadcasted to Suit Mesh)\n",
                      suitStealthMode ? "ON (LEDs OFF)" : "OFF (LEDs ACTIVE)");
      }
    }

    if (buttonIsPressed && !buttonHoldHandled && (now - buttonPressStartTime >= 1000)) {
      buttonHoldHandled = true;
      cycleNodeRole();
    }
  }
  lastButtonRaw = raw;
}

void printStatusMenu() {
  bool gimOk = (lastGimbalSyncMs > 0) && (millis() - lastGimbalSyncMs < 2500);
  bool wsOk  = (lastWebshooterSyncMs > 0) && (millis() - lastWebshooterSyncMs < 2500);
  uint8_t effState = getEffectiveSuitState();
  const char* stateStr = (effState == SUIT_LED_FIRING) ? "FIRING (RED OVERRIDE)" :
                         (effState == SUIT_LED_DEPLOYED) ? "ARMED (SOFT FLASH)" : "STANDBY (WAVE/BREATH)";
  Serial.println("\n============================================================");
  Serial.printf("  ESP-LED SUIT MESH NODE // %s (ID: %d | Tier: MID)\n", getNodeName(cfgNodeId), cfgNodeId);
  Serial.println("============================================================");
  Serial.printf("  3-MOSFET RGB:    R=GPIO %d | G=GPIO %d | B=GPIO %d (Active HIGH, 5kHz PWM)\n",
                PIN_MOSFET_R, PIN_MOSFET_G, PIN_MOSFET_B);
  Serial.printf("  Wireless Mode:   %s (BLE Client: %s)\n",
                cfgCommMode == COMM_MODE_BLE ? "BLE ONLY (Default - leds.html)" : "WI-FI AP + BLE (192.168.4.1)",
                bleClientConnected ? "CONNECTED" : "ADVERTISING");
  Serial.printf("  Gimbal Hub Link: %s\n", gimOk ? "SYNCED (Channel 1)" : "WAITING FOR HUB");
  Serial.printf("  Webshooter Link: %s (Armed: %s)\n", wsOk ? "ONLINE (Channel 1)" : "OFFLINE", wsReportedArmed ? "YES" : "NO");
  Serial.printf("  Active State:    %s\n", stateStr);
  Serial.printf("  Stealth Mode:    %s\n", suitStealthMode ? "ON (All LEDs OFF)" : "OFF (Active)");
  Serial.printf("  Standby Setup:   RGB(%d, %d, %d) | Pat: %d | Period: %d ms\n",
                cfgStandbyR, cfgStandbyG, cfgStandbyB, cfgLedPattern, cfgBreathPeriodMs);
  Serial.printf("  Armed Setup:     RGB(%d, %d, %d) | Pat: %d | Period: %d ms\n",
                cfgDeployedR, cfgDeployedG, cfgDeployedB, cfgArmedPattern, cfgArmedPeriodMs);
  Serial.printf("  Wave Mode:       %d (0=Unison, 1=Low->High, 2=High->Low) | Offset: %d%%\n",
                cfgWaveMode, cfgWaveOffsetPct);
  Serial.println("------------------------------------------------------------");
  Serial.println("  Serial Commands:");
  Serial.println("    'b'     : Toggle Wireless Mode (BLE Mode <-> Wi-Fi AP Mode)");
  Serial.println("    'n'     : Toggle Node Role (CHEST LED <-> BACK LED)");
  Serial.println("    'x'     : Toggle Stealth Mode (LEDs OFF <-> ON)");
  Serial.println("    's'     : Preview Standby / Armed Soft Flash / Firing / Auto");
  Serial.println("    'h'/'?' : Print this status & help menu");
  Serial.println("============================================================\n");
}

void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') continue;

    switch (c) {
      case 'b':
      case 'B':
        applyWirelessCommMode(cfgCommMode == COMM_MODE_BLE ? COMM_MODE_WIFI : COMM_MODE_BLE, true);
        break;

      case 'n':
      case 'N':
        cycleNodeRole();
        break;

      case 'x':
      case 'X':
        suitStealthMode = !suitStealthMode;
        lastLocalStealthToggleMs = millis();
        prefs.putBool("stealth", suitStealthMode);
        broadcastStealthCommand(suitStealthMode);
        Serial.printf("[CONFIG] Stealth Mode: %s (Broadcasted to Suit Mesh)\n",
                      suitStealthMode ? "ON (LEDs OFF)" : "OFF (LEDs ACTIVE)");
        break;

      case 's':
      case 'S':
        if (localTestStateOverride == 0xFF) localTestStateOverride = SUIT_LED_STANDBY;
        else if (localTestStateOverride == SUIT_LED_STANDBY) localTestStateOverride = SUIT_LED_DEPLOYED;
        else if (localTestStateOverride == SUIT_LED_DEPLOYED) localTestStateOverride = SUIT_LED_FIRING;
        else localTestStateOverride = 0xFF;
        Serial.printf("[TEST] Local State Override: %s\n",
                      localTestStateOverride == 0xFF ? "AUTO (Follow Mesh)" :
                      localTestStateOverride == SUIT_LED_STANDBY ? "STANDBY" :
                      localTestStateOverride == SUIT_LED_DEPLOYED ? "ARMED (Soft Flash)" : "FIRING");
        break;

      case 'h':
      case 'H':
      case '?':
        printStatusMenu();
        break;

      default:
        break;
    }
  }
}

// ==============================================================================
// Setup & Loop
// ==============================================================================
void setup() {
  // 1. Initialize 3-MOSFET RGB PWM channels immediately (GPIO 5=R, 6=G, 7=B)
  initMosfetRgbPwm();

  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);

  // 2. Load saved Node Role, Wireless Mode (defaults to BLE!), Stealth state, and Suit LED colors
  prefs.begin(NVS_NAMESPACE, false);
  cfgNodeId = prefs.getUChar("node_id", DEFAULT_NODE_ID);
  cfgCommMode = prefs.getUChar("comm_v2", DEFAULT_COMM_MODE);
  suitStealthMode = prefs.getBool("stealth", false);
  cfgStandbyR = prefs.getUChar("sr", cfgStandbyR);
  cfgStandbyG = prefs.getUChar("sg", cfgStandbyG);
  cfgStandbyB = prefs.getUChar("sb", cfgStandbyB);
  cfgDeployedR = prefs.getUChar("dr", cfgDeployedR);
  cfgDeployedG = prefs.getUChar("dg", cfgDeployedG);
  cfgDeployedB = prefs.getUChar("db", cfgDeployedB);
  if ((cfgDeployedR == 0 && cfgDeployedG == 60 && cfgDeployedB == 255) ||
      (cfgDeployedR == 255 && cfgDeployedG == 42 && cfgDeployedB == 85)) {
    cfgDeployedR = 255; cfgDeployedG = 0; cfgDeployedB = 0;
    prefs.putUChar("dr", 255); prefs.putUChar("dg", 0); prefs.putUChar("db", 0);
  }
  cfgLedPattern = prefs.getUChar("pattern", cfgLedPattern);
  cfgArmedPattern = prefs.getUChar("arm_pat", PATTERN_ARMED_SOFT_FLASH);
  cfgWaveMode = prefs.getUChar("wave_m", WAVE_LOW_TO_HIGH);
  cfgWaveOffsetPct = prefs.getUChar("wave_o", DEFAULT_WAVE_OFFSET_PCT);
  cfgMinBrightness = prefs.getUChar("min_b", cfgMinBrightness);
  cfgMaxBrightness = prefs.getUChar("max_b", cfgMaxBrightness);
  cfgBreathPeriodMs = prefs.getUShort("period", cfgBreathPeriodMs);
  cfgArmedPeriodMs = prefs.getUShort("arm_per", DEFAULT_ARMED_PERIOD_MS);

  if (!suitStealthMode) {
    writeMosfetRgb(25, 25, 25); // Soft initial glow on boot
  }

  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);

  initEspNowAndWireless();
  printStatusMenu();
}

void loop() {
  if (cfgCommMode == COMM_MODE_WIFI && webServerStarted) {
    ledWebServer.handleClient();
  }

  if (pendingBleCommand.length() > 0) {
    String cmd = pendingBleCommand;
    pendingBleCommand = "";
    handleBleCommandString(cmd);
  }

  if (pendingCommModeSwitch >= 0 && millis() >= pendingCommSwitchAtMs) {
    uint8_t targetMode = (uint8_t)pendingCommModeSwitch;
    pendingCommModeSwitch = -1;
    applyWirelessCommMode(targetMode, true);
  }

  processBootButton();
  processSerialCommands();
  updateRgbLed();

  // Periodic BLE Status Notification (4Hz when connected to leds.html)
  if (bleClientConnected && pStatusChar != nullptr && (millis() - lastBleNotifyMs >= 250)) {
    lastBleNotifyMs = millis();
    String json = buildBleStatusJson();
    pStatusChar->setValue(json.c_str());
    pStatusChar->notify();
  }

  // Periodic Node Heartbeat to Gimbal Hub every 1.0s
  if (millis() - lastNodeTelemMs >= NODE_TELEMETRY_INTERVAL_MS) {
    lastNodeTelemMs = millis();
    broadcastNodeTelemetry();
  }

  delay(5);
}
