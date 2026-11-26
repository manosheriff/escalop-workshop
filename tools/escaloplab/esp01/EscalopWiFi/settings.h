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

    // Printer variants (Marlin repo escaloplab/variants). Added after the fields above, so modules updated from older
    // versions keep their Wi-Fi; extMagic tells whether these fields were ever written.
    uint32_t extMagic;
    uint8_t  siteVariantSet;    // escaloplab.com said which variant this printer gets (check-in answer)
    char     siteVariant[17];   // that variant; "" = standard
    char     stagedVariant[17]; // variant of the firmware in the staging area; "" = standard

    // Remote printing (1.0.22+): the printer's own token from escaloplab.com, given on its first poll.
    // remoteMagic tells whether this field was ever written.
    uint32_t remoteMagic;
    char     remoteToken[49];   // 48 hex digits; "" = none yet
    char     remoteJob[37];     // the website print it started and is watching (survives the daily restart)
};

extern Settings settings;

void settingsLoad();
void settingsSave();
