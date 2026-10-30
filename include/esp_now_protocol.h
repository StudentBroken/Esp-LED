#pragma once
#include <Arduino.h>
#include <math.h>

// ==============================================================================
// ESP-NOW Protocol: Suit Mesh (Webshooter + Gimbal Hub + Chest/Back LED Nodes)
// ==============================================================================
#define ESPNOW_WIFI_CHANNEL         1       // Fixed 2.4GHz Wi-Fi Channel
#define GIMBAL_MAX_FIRE_DURATION_MS 8000    // 8.0s hardware safety cutoff to prevent coil overheating
#define LED_SYNC_BROADCAST_MS       200     // 5Hz Mesh LED sync broadcast from Gimbal Hub
#define DEFAULT_ARMED_PERIOD_MS     1100    // 1.1s default soft armed flashing cycle
#define DEFAULT_WAVE_OFFSET_PCT     30      // 30% phase offset per vertical tier (Low -> Mid -> High)

// ==============================================================================
// BLE GATT & Wireless App Mode Definitions (Default: BLE Mode)
// ==============================================================================
#define SUIT_BLE_SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SUIT_BLE_CHAR_STATUS_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SUIT_BLE_CHAR_CMD_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"

enum SuitCommMode : uint8_t {
    COMM_MODE_BLE  = 0,  // Default: BLE GATT active for online leds.html (Wi-Fi SoftAP OFF, ESP-NOW ON)
    COMM_MODE_WIFI = 1   // Direct Wi-Fi SoftAP (http://192.168.4.1) + BLE GATT + ESP-NOW ON
};
#define DEFAULT_SUIT_COMM_MODE      COMM_MODE_BLE

enum GimbalCommand : uint8_t {
    CMD_NONE           = 0,
    CMD_CALIBRATE      = 1,  // Long press while disarmed -> calibrate gimbal
    CMD_DEPLOY         = 2,  // Single tap while disarmed -> deploy (arm) gimbal
    CMD_DISARM         = 3,  // Single tap while armed    -> disarm (retract) gimbal
    CMD_FIRE           = 4,  // Held while armed          -> energize electromagnet
    CMD_STOP_FIRE      = 5,  // Released / timeout        -> de-energize electromagnet
    CMD_TELEMETRY      = 6,  // Webshooter periodic telemetry broadcast
    CMD_LED_SYNC       = 7,  // Gimbal Master -> All Suit ESP32s LED Color & Breathing Sync
    CMD_LED_NODE_TELEM = 8,  // Dedicated Suit LED Node (Chest/Back) -> Gimbal Hub telemetry
    CMD_STEALTH_ON     = 9,  // Webshooter -> Entire Suit: Enable Stealth Mode (All LEDs OFF)
    CMD_STEALTH_OFF    = 10  // Webshooter -> Entire Suit: Disable Stealth Mode (All LEDs ON)
};

enum SuitLedState : uint8_t {
    SUIT_LED_STANDBY  = 0,   // Disarmed / Stowed standby breathing
    SUIT_LED_DEPLOYED = 1,   // Armed / Deployed (Either Webshooter OR Shoulder Gimbal armed)
    SUIT_LED_FIRING   = 2,   // Intense Red firing override
    SUIT_LED_FLASH    = 3    // High-priority temporary status flash (e.g. calibration cyan)
};

enum SuitLedPattern : uint8_t {
    PATTERN_BREATH_APPLE     = 0, // Perceptual Apple-style exp(sin(t)) + CIE 1931 slow breathing
    PATTERN_SOLID            = 1, // Constant solid illumination at max_brightness
    PATTERN_PULSE_FAST       = 2, // High-alert rapid combat sine pulse (2x speed)
    PATTERN_HEARTBEAT        = 3, // Spider-Sense / Arc Reactor double-pulse heartbeat
    PATTERN_STEALTH_OFF      = 4, // Stealth mode (All suit LEDs OFF)
    PATTERN_ARMED_SOFT_FLASH = 5, // Soft Armed Flash: smooth sin^3(pi*t) tactical red warning swell
    PATTERN_TACTICAL_STROBE  = 6, // Soft twin-peak tactical warning flash
    PATTERN_REACTOR_SURGE    = 7, // Smooth reactor charge ramp + fast release
    PATTERN_RAINBOW_FLOW     = 8  // Continuous 360-degree HSV color-wheel wave
};

enum SuitWaveMode : uint8_t {
    WAVE_UNISON      = 0,    // All suit nodes pulse in unison (0 phase offset)
    WAVE_LOW_TO_HIGH = 1,    // Traveling Wave: Webshooter (Low) -> Vest (Mid) -> Gimbal (High)
    WAVE_HIGH_TO_LOW = 2     // Traveling Wave: Gimbal (High) -> Vest (Mid) -> Webshooter (Low)
};

enum SuitWaveTier : uint8_t {
    TIER_WEBSHOOTER = 0,     // Low tier  (Wrist / Webshooter)
    TIER_VEST       = 1,     // Mid tier  (Chest / Back Esp-LED Vest)
    TIER_GIMBAL     = 2      // High tier (Shoulder Mounted Gimbal)
};

enum SuitNodeId : uint8_t {
    NODE_CHEST  = 1,         // Chest-mounted 3-MOSFET RGB LED controller
    NODE_BACK   = 2,         // Back-mounted 3-MOSFET RGB LED controller
    NODE_HELMET = 3,         // Helmet / Eyes LED controller
    NODE_AUX    = 4          // Auxiliary suit LED node
};

typedef struct __attribute__((packed)) {
    uint8_t command;        // GimbalCommand
    uint16_t duration_ms;   // Safety duration parameter (ms)
    uint32_t sequence;      // Monotonically increasing sequence number
} GimbalMessage;

typedef struct __attribute__((packed)) {
    uint8_t command;        // CMD_TELEMETRY (6)
    uint16_t vbat_mv;       // Battery voltage in millivolts (e.g. 4150 for 4.15V)
    uint8_t webshooter_pos; // 0 = Deployed (0°), 1 = Retracted (90°)
    uint8_t gimbal_armed;   // 0 = Disarmed, 1 = Armed
    uint8_t gimbal_firing;  // 0 = Idle, 1 = Firing
    uint8_t stealth_mode;   // 0 = LEDs Active, 1 = Suit Stealth Mode (All LEDs OFF)
    uint16_t breath_phase_ms; // Current breathing phase for synchronized rhythm
    uint32_t sequence;      // Monotonically increasing sequence number
} WebshooterTelemetry;

typedef struct __attribute__((packed)) {
    uint8_t  command;             // CMD_LED_SYNC (7)
    uint8_t  suit_state;          // SuitLedState (0=Standby, 1=Deployed/Armed, 2=Firing, 3=Flash)
    uint8_t  pattern;             // Standby SuitLedPattern (default: PATTERN_BREATH_APPLE)
    uint8_t  armed_pattern;       // Armed SuitLedPattern (default: PATTERN_ARMED_SOFT_FLASH)
    uint8_t  wave_mode;           // SuitWaveMode (0=Unison, 1=Low->High WS->Vest->Gimbal, 2=High->Low)
    uint8_t  wave_offset_pct;     // Phase delay per tier as % of period (10..50, default 30)
    uint8_t  stealth_mode;        // 0 = LEDs Active, 1 = Suit Stealth Mode (All LEDs OFF)
    uint8_t  gimbal_actual_state; // Actual Gimbal State (0=Folded, 1=Deploying, 2=Tracking, 3=Folding)
    uint8_t  gimbal_zeroed;       // 1 if zeroed & armable, 0 if blocked/uncalibrated
    uint8_t  standby_r;
    uint8_t  standby_g;
    uint8_t  standby_b;
    uint8_t  deployed_r;
    uint8_t  deployed_g;
    uint8_t  deployed_b;
    uint8_t  flash_r;
    uint8_t  flash_g;
    uint8_t  flash_b;
    uint8_t  min_brightness;
    uint8_t  max_brightness;
    uint16_t breath_period_ms;    // Standby breathing cycle period in ms (e.g. 3600)
    uint16_t armed_period_ms;     // Armed flashing cycle period in ms (e.g. 1100)
    uint16_t breath_phase_ms;     // Master's current standby phase in ms (0 .. breath_period_ms - 1)
    uint16_t armed_phase_ms;      // Master's current armed phase in ms (0 .. armed_period_ms - 1)
    uint32_t sequence;            // Monotonically increasing sequence number
} SuitLedSyncMessage;

typedef struct __attribute__((packed)) {
    uint8_t  command;           // CMD_LED_NODE_TELEM (8)
    uint8_t  node_id;           // SuitNodeId (1=Chest, 2=Back, 3=Helmet, 4=Aux)
    uint16_t vbat_mv;           // Battery voltage in millivolts (0 if USB powered)
    uint8_t  num_leds;          // Number of LEDs driven by this node (1 = 3-MOSFET RGB)
    uint32_t sequence;          // Monotonically increasing sequence number
} SuitNodeTelemetry;

// ==============================================================================
// Shared Suit Mesh Waveform & Traveling Wave Helpers
// ==============================================================================
static inline uint32_t applySuitWaveOffset(uint32_t basePhaseMs, uint16_t periodMs,
                                           uint8_t waveMode, uint8_t waveOffsetPct,
                                           uint8_t tierIndex) {
    if (periodMs < 200) periodMs = 200;
    if (waveMode == WAVE_UNISON || waveOffsetPct == 0) {
        return basePhaseMs % periodMs;
    }
    int32_t lagSteps = 0;
    if (waveMode == WAVE_LOW_TO_HIGH) {
        // Low (0: Webshooter) leads -> Mid (1: Vest) -> High (2: Gimbal)
        lagSteps = (int32_t)tierIndex;
    } else if (waveMode == WAVE_HIGH_TO_LOW) {
        // High (2: Gimbal) leads -> Mid (1: Vest) -> Low (0: Webshooter)
        lagSteps = 2 - (int32_t)tierIndex;
        if (lagSteps < 0) lagSteps = 0;
    }
    int32_t delayMs = ((int32_t)periodMs * (int32_t)waveOffsetPct * lagSteps) / 100;
    int32_t shifted = (int32_t)(basePhaseMs % periodMs) - delayMs;
    while (shifted < 0) shifted += periodMs;
    return (uint32_t)(shifted % periodMs);
}

static inline float evaluateSuitPatternNorm(uint8_t pattern, uint32_t phaseMs, uint16_t periodMs) {
    if (periodMs < 200) periodMs = 200;
    float t = (float)(phaseMs % periodMs) / (float)periodMs; // 0.0 .. 1.0

    switch (pattern) {
        case PATTERN_STEALTH_OFF:
            return 0.0f;

        case PATTERN_SOLID:
            return 1.0f;

        case PATTERN_PULSE_FAST: {
            uint16_t fastPeriod = (periodMs >= 600) ? (periodMs / 2) : 300;
            uint32_t fastPhase = phaseMs % fastPeriod;
            float angle = (2.0f * (float)PI * (float)fastPhase / (float)fastPeriod) - (0.5f * (float)PI);
            return (expf(sinf(angle)) - 0.36787944f) / 2.3504024f;
        }

        case PATTERN_HEARTBEAT: {
            float d1 = (t - 0.14f) / 0.055f;
            float d2 = (t - 0.30f) / 0.045f;
            float beat1 = expf(-(d1 * d1));
            float beat2 = 0.72f * expf(-(d2 * d2));
            return (beat1 > beat2) ? beat1 : beat2;
        }

        case PATTERN_ARMED_SOFT_FLASH: {
            // Soft Armed Flash: smooth single-lobe sin^3(pi*t) warning swell
            // Softly swells to peak red and dips to floor without harsh edges or double-beats
            float s = sinf((float)PI * t);
            if (s < 0.0f) s = 0.0f;
            return s * s * s;
        }

        case PATTERN_TACTICAL_STROBE: {
            // Soft twin-peak tactical warning flash
            float d1 = (t - 0.22f) / 0.065f;
            float d2 = (t - 0.50f) / 0.065f;
            float p1 = expf(-(d1 * d1));
            float p2 = expf(-(d2 * d2));
            return (p1 > p2) ? p1 : p2;
        }

        case PATTERN_REACTOR_SURGE: {
            // Smooth 70% charge-up ramp followed by 30% fast release
            if (t < 0.70f) {
                float u = t / 0.70f;
                return u * u * (3.0f - 2.0f * u);
            } else {
                float u = (1.0f - t) / 0.30f;
                return u * u;
            }
        }

        case PATTERN_RAINBOW_FLOW: {
            // Gentle breathing modulation on top of rainbow color rotation
            float angle = (2.0f * (float)PI * t) - (0.5f * (float)PI);
            return 0.55f + 0.45f * ((expf(sinf(angle)) - 0.36787944f) / 2.3504024f);
        }

        case PATTERN_BREATH_APPLE:
        default: {
            float angle = (2.0f * (float)PI * t) - (0.5f * (float)PI);
            return (expf(sinf(angle)) - 0.36787944f) / 2.3504024f;
        }
    }
}

static inline void suitHsvToRgb(float hDeg, float s, float v, uint8_t &outR, uint8_t &outG, uint8_t &outB) {
    while (hDeg < 0.0f) hDeg += 360.0f;
    while (hDeg >= 360.0f) hDeg -= 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(hDeg / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r1 = 0, g1 = 0, b1 = 0;
    if (hDeg < 60.0f)       { r1 = c; g1 = x; b1 = 0; }
    else if (hDeg < 120.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hDeg < 180.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hDeg < 240.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hDeg < 300.0f) { r1 = x; g1 = 0; b1 = c; }
    else                    { r1 = c; g1 = 0; b1 = x; }
    outR = (uint8_t)constrain((int)((r1 + m) * 255.0f), 0, 255);
    outG = (uint8_t)constrain((int)((g1 + m) * 255.0f), 0, 255);
    outB = (uint8_t)constrain((int)((b1 + m) * 255.0f), 0, 255);
}
