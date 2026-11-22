#pragma once

#include <Arduino.h>

// What the module remembers across restarts (kept in the ESP's emulated EEPROM sector)
struct Settings
{
    char ssid[33];       // home Wi-Fi; empty = not set up
    char pass[65];
    char serial[13];     // from escaloplab.com, e.g. "00123"; empty = not registered yet

    // Printer firmware waiting in the staging area (see stage.h)
    uint8_t  stagedValid;
    char     stagedVersion[16]; // "" when it came from the SD card (version unknown until it runs)
    uint32_t stagedSize;
    uint32_t stagedCrc;
    uint8_t  pendingInstall;    // 1 while installing: finish the job after a power cut

    // Per-printer setup from escaloplab.com (check-in answer). Added in 1.0.14 after the fields above, so modules
    // updated from older versions keep their Wi-Fi; extMagic tells whether these fields were ever written.
    uint32_t extMagic;
    float    steps[4];          // X, Y, Z, E steps/mm; 0 = use the printer firmware's own value
    char     fwChannel[17];     // "" = standard firmware; else releases tagged fw-<channel>-vX.Y.Z
};

extern Settings settings;

void settingsLoad();
void settingsSave();
