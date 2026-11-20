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
};

extern Settings settings;

void settingsLoad();
void settingsSave();
