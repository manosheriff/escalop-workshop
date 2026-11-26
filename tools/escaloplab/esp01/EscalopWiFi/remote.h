#pragma once

#include <Arduino.h>

// Remote printing (models.escaloplab.com): every few seconds the module tells escaloplab.com how the printer is
// doing and asks for work. Until a customer links the printer to their account it shows a 6-digit code on the
// printer's screen; once linked, a print sent from a phone is downloaded straight onto the SD card and started.
// Each check is one HTTPS request, made only when there is memory for it (see Remote::loop).

namespace Remote {

// Boot task (EscalopWiFi.ino): a print is waiting. Downloading it needs a full-size secure connection, which only
// fits right after a restart, before the web server starts; the regular checks use a small one.
constexpr uint32_t BOOT_TASK = 8;

void loop();
// Right after a restart for BOOT_TASK (Wi-Fi connected, web server not started yet): fetch and start the print
void atStartup();

// For the printer's web page
const char* stateName(); // "off", "waiting", "unlinked", "linked", "printing", "low_memory", "error"
String      pairCode();  // the code to type on models.escaloplab.com ("" once linked)
bool        busy();      // a print is being downloaded onto the SD card

} // namespace Remote
