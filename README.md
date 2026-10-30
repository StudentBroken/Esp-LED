# Esp-LED // Suit Mesh 3-MOSFET RGB LED Node (Chest & Back)

Dedicated ESP32-C3 wireless RGB LED node driving a single RGB LED module/strip via **3 MOSFETs on GPIOs 5, 6, 7** (`R = GPIO 5`, `G = GPIO 6`, `B = GPIO 7`, Active `HIGH` = ON, `LOW` = OFF) using 5 kHz hardware PWM. Automatically synchronizes its colors, CIE-1931 breathing curve, combat state, and Stealth Mode with the **Shoulder Mounted Gimbal** (Web App Master Hub) and **Webshooter** over **ESP-NOW Channel 1**.

## Features
- **3-MOSFET Hardware PWM (`GPIO 5, 6, 7`)**: Flicker-free 5 kHz hardware LEDC PWM with CIE 1931 perceptual lightness curve matching the exact breathing rhythm and colors of the Gimbal and Webshooter.
- **Suit-Wide Stealth Mode Sync**: Holding the Webshooter button while disarmed (800ms) toggles Stealth Mode and immediately turns **OFF** all LEDs across the Webshooter, Gimbal, Chest LED, and Back LED.
- **One-Button Role Assignment (Chest vs Back)**:
  - **Hold `BOOT` button (`GPIO 9`) for 1 second**: Switches node identity between **Chest LED** (flashes Cyan) and **Back LED** (flashes Purple), saving to Flash and reporting its status (`● CHEST` / `● BACK`) to the Gimbal Web App.
  - **Tap `BOOT` button**: Toggles local Stealth Mode (LEDs off/on).

## Pinout (ESP32-C3 SuperMini)
| Function | GPIO | Description |
| :--- | :--- | :--- |
| **Red MOSFET (`R`)** | `GPIO 5` | Active HIGH (`HIGH` = ON, `LOW` = OFF), LEDC Ch 0 (5 kHz PWM) → Red Module **Pad B** |
| **Green MOSFET (`G`)** | `GPIO 6` | Active HIGH (`HIGH` = ON, `LOW` = OFF), LEDC Ch 1 (5 kHz PWM) → Green Module **Pad B** |
| **Blue MOSFET (`B`)** | `GPIO 7` | Active HIGH (`HIGH` = ON, `LOW` = OFF), LEDC Ch 2 (5 kHz PWM) → Blue Module **Pad B** |
| **BOOT Button** | `GPIO 9` | Short tap = Stealth toggle, 1.0s hold = Toggle Chest / Back role |
| **Battery ADC** | `GPIO 4` | Optional 100k:100k battery voltage divider |

---

## Hardware Wiring (`HYG038N03` MOSFET Modules × 3)

```text
       [  TOP EDGE  ]
  (Load -)          (Signal In)
 [ Pad A ]          [  Pad B  ]
  (3 vias)          (1 big via)
   ▲                  ▲
   |                  |  <-- Traces / Arrows point forward
 +-----------------------------+
 | [Pad A]             [Pad B] |
 |                             |
 | [ FET ]             [102 R] |
 | HYG038N03           [103 R] |
 |                             |
 | [Pad C]             [Pad D] |
 +-----------------------------+
       [ BOTTOM EDGE ]
   (GND)               (GND)
```

### Pad Identification
1. **Top-Right Pad (`Pad B`, single via next to `102` resistor) — `PWM / Signal Input (+)`**
   - Traces lead into the **`102`** ($1\text{ k}\Omega$) gate resistor.
   - **Connect to:** **ESP32-C3 GPIO Pin** (`GPIO 5` = Red, `GPIO 6` = Green, `GPIO 7` = Blue).
2. **Top-Left Pad (`Pad A`, 3 vertical vias) — `Load Negative / Drain (-)`**
   - Traces lead directly to the drain tab and pins 5–8 of the `HYG038N03` MOSFET.
   - **Connect to:** **LED Strip Color Wire** (Red, Green, or Blue cathode `-`).
3. **Bottom Two Pads (`Pad C` under FET + `Pad D` under `MOS` label) — `System Ground / Source (-)`**
   - Connects directly to Pins 1–3 (Source) of the FET and the ground plane / `103` ($10\text{ k}\Omega$) pull-down resistor.
   - **Connect to:** **5V Supply (`-`) AND ESP32-C3 `GND`** (bridge or solder both grounds across the bottom pads).

### Complete Wiring Map (Repeat across 3 modules for R, G, B)

| Module Pad | Where It Connects |
| :--- | :--- |
| **Top-Right (`Pad B`, 1 via, near `102` R)** | **ESP32-C3 GPIO** (`GPIO 5` = R, `GPIO 6` = G, `GPIO 7` = B) |
| **Top-Left (`Pad A`, 3 vias, near FET)** | **5V LED Strip R, G, or B Wire (`-`)** |
| **Bottom Pads (`Pad C` / `Pad D`)** | **5V Power Supply (`-`) AND ESP32-C3 `GND`** |
| *(Direct wire — bypasses modules)* | **5V Power Supply (`+`) $\rightarrow$ LED Strip `+5V` Common Anode** |
