#pragma once

#include <Arduino.h>

// Escalop v80 Wi-Fi module (ESP-01). See ../PROTOCOL.md for how it talks to the printer's Marlin firmware.

// This module's own software version. A GitHub release tagged wifi-vX.Y.Z with a newer X.Y.Z updates it.
#define WIFI_VERSION "1.0.15"

#define PRINTER_MODEL  "v80"
#define HOTSPOT_PREFIX "Escalop-v80-"  // + serial number, e.g. Escalop-v80-00123
#define HOST_PREFIX    "escalop-v80-"  // network name: escalop-v80-00123.local

#define GITHUB_REPO        "manosheriff/escalop-workshop"
#define FIRMWARE_TAG       "firmware-v"   // printer firmware releases: one .hex asset
#define WIFI_TAG           "wifi-v"       // this module's releases: one .bin asset
#define CHECKIN_URL        "https://escaloplab.com/api/printer-units/checkin"
#define USER_AGENT         "EscalopWiFi/" WIFI_VERSION

// Wiring (see ../README.md): ESP-01 TX/RX <-> Mega Serial0 (RAMPS AUX-1 D0/D1), GPIO0 -> 1N4148 -> Mega RESET.
// Not GPIO2: the ESP's boot ROM pulses GPIO2 at every start, which restarted the printer with the module.
constexpr uint8_t  RESET_PIN       = 0;        // low = the printer's board is held in reset
constexpr uint32_t MARLIN_BAUD     = 1000000;  // Marlin's BAUDRATE
constexpr uint32_t BOOTLOADER_BAUD = 115200;   // the Mega 2560's STK500v2 bootloader

// The ATmega2560's program space below its 8 KB bootloader
constexpr uint32_t AVR_APP_SIZE = 0x3E000;

constexpr uint32_t SECONDS = 1000;
constexpr uint32_t MINUTES = 60 * SECONDS;
constexpr uint32_t HOURS   = 60 * MINUTES;

constexpr uint32_t UPDATE_CHECK_EVERY = 24 * HOURS;  // GitHub (firmware + this module) and escaloplab.com check-in
constexpr uint32_t FIRST_CHECK_AFTER  = 90 * SECONDS; // after the first Wi-Fi connection
constexpr uint32_t OFFER_AGAIN_AFTER  = 24 * HOURS;  // after the user pressed "Later"
