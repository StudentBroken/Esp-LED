#pragma once
#include <Arduino.h>
#include "esp_now_protocol.h"

// ==============================================================================
// Esp-LED: Dedicated Suit Mesh 3-MOSFET RGB LED Node (Chest / Back / Helmet)
// Board: ESP32-C3 SuperMini
// ==============================================================================

// ------------------------------------------------------------------------------
// Default Node Identity
// (Can also be switched at runtime via BOOT button 1.0s hold or USB Serial 'n')
// ------------------------------------------------------------------------------
#define DEFAULT_NODE_ID           NODE_CHEST  // NODE_CHEST (1) or NODE_BACK (2)
#define DEFAULT_COMM_MODE         COMM_MODE_BLE // COMM_MODE_BLE (0 = Default BLE) or COMM_MODE_WIFI (1 = Wi-Fi AP)
#define WIFI_AP_SSID_LED          "Suit-LED-Vest"
#define WIFI_AP_PASS_LED          "12345678"
#define WEB_SERVER_PORT_LED       80

// ------------------------------------------------------------------------------
// 3-MOSFET RGB LED Hardware Pinout (HYG038N03 Low-Side N-Channel MOSFET Modules)
//   - Pad B (Top-Right, 1 via, 102 1k gate resistor) -> ESP32-C3 GPIO 5 / 6 / 7
//   - Pad A (Top-Left, 3 vias, MOSFET Drain)         -> 5V LED Strip R / G / B (-)
//   - Pad C & D (Bottom edge, MOSFET Source + 103 R) -> 5V Supply (-) & ESP32 GND
//   - 5V Power Supply (+)                            -> LED Strip +5V Common Anode
//   - Logic: Active HIGH (Duty 0 = LOW/OFF, Duty 255 = HIGH/Full ON)
// ------------------------------------------------------------------------------
#define PIN_MOSFET_R              5           // GPIO 5: Red MOSFET Pad B (LEDC Ch 0)
#define PIN_MOSFET_G              6           // GPIO 6: Green MOSFET Pad B (LEDC Ch 1)
#define PIN_MOSFET_B              7           // GPIO 7: Blue MOSFET Pad B (LEDC Ch 2)

#define LEDC_CH_R                 0           // Hardware PWM Channel 0 (Red)
#define LEDC_CH_G                 1           // Hardware PWM Channel 1 (Green)
#define LEDC_CH_B                 2           // Hardware PWM Channel 2 (Blue)
#define MOSFET_PWM_FREQ_HZ        5000        // 5kHz smooth, flicker-free MOSFET PWM
#define MOSFET_PWM_RES_BITS       8           // 8-bit duty cycle (0 = LOW/OFF, 255 = HIGH/ON)

// ------------------------------------------------------------------------------
// Control Button & Battery ADC
// ------------------------------------------------------------------------------
#define PIN_BUTTON_BOOT           9           // Onboard BOOT Button (Active LOW):
                                              //   - Short Tap: Toggle Stealth Mode
                                              //   - Hold 1.0s: Switch Node Role (CHEST <-> BACK)
#define PIN_BATTERY_ADC           4           // Optional Battery Voltage Divider (GPIO 4, 100k:100k)
#define BATTERY_DIVIDER_RATIO     2.0f

// ------------------------------------------------------------------------------
// Default Lighting Parameters (Overridden automatically by Gimbal Hub Sync)
// ------------------------------------------------------------------------------
#define RGB_MIN_BRIGHTNESS        12          // Floor brightness (never drops to 0% in breathing modes)
#define RGB_MAX_BRIGHTNESS        220         // Peak brightness (0-255)
#define RGB_BREATH_PERIOD_MS      3600        // 3.6s Apple-style breathing cycle

// ------------------------------------------------------------------------------
// Telemetry & Timing
// ------------------------------------------------------------------------------
#define NODE_TELEMETRY_INTERVAL_MS 1000       // Send node heartbeat to Gimbal Hub every 1.0s
#define SERIAL_BAUD_RATE           115200
#define NVS_NAMESPACE              "espled"
