#pragma once
// =====================================================================
// Agent Buddy — board configuration (Seeed Studio XIAO ESP32-S3)
// =====================================================================
//
// This file is the single place to describe what hardware is wired up.
// Today only the on-board LED is used; flip the HAS_* flags to 1 as you
// connect the LCD and the servo, and the matching modules light up.

#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "0.1.0"
#endif

// ---- Serial (PC <-> device bridge) ----------------------------------
#define BUDDY_SERIAL_BAUD 115200

// ---- On-board status LED --------------------------------------------
// XIAO ESP32-S3 user LED is on GPIO21 and is ACTIVE-LOW (LOW = lit).
#define BUDDY_LED_PIN         21
#define BUDDY_LED_ACTIVE_LOW  1

// ---- Optional peripherals -------------------------------------------
// Set to 1 once the part is physically connected and pins below confirmed.
#define BUDDY_HAS_DISPLAY 1
#define BUDDY_HAS_SERVO   0

// ---- SPI TFT display pins (provisional — confirm when wiring) --------
// XIAO silk -> GPIO :  D8=7(SCK)  D9=8(MISO)  D10=9(MOSI)
//                      D0=1 D1=2 D2=3 D3=4 D4=5 D5=6
#define BUDDY_TFT_SCK   7   // D8
#define BUDDY_TFT_MOSI  9   // D10
#define BUDDY_TFT_CS    2   // D1
#define BUDDY_TFT_DC    3   // D2
#define BUDDY_TFT_RST   4   // D3
#define BUDDY_TFT_BL    5   // D4
#define BUDDY_TFT_W     240
#define BUDDY_TFT_H     320

// ---- Servo pin (head nod/turn) --------------------------------------
#define BUDDY_SERVO_PIN 1   // D0
#define BUDDY_SERVO_MIN_US 500
#define BUDDY_SERVO_MAX_US 2500
