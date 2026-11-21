#pragma once

#include <Arduino.h>

// The web server: the setup page (through the setup hotspot), the printer's page and JSON API (on the home
// network), and an OctoPrint-compatible upload API so Escalop Workshop can send prints.

namespace Web {

void begin();
void loop();
bool uploading();

// While printer firmware is installed (Serial belongs to the bootloader): serves the status page only
void serviceDuringInstall();

// The outcome of a Wi-Fi connection attempt ("ok", "badpw", "fail"); kept when the setup page started it
void setJoinResult(const char* result);

} // namespace Web
