#pragma once

#include <Arduino.h>

// Printer firmware updates (from GitHub or the SD card) and this module's own updates (PROTOCOL.md section 7).

namespace Updates {

enum class State { Idle, Checking, Downloading, Ready, Installing, Failed };

void begin();
void loop();

// "Check for updates" from the printer's menu (fromPrinter: answer with M9108/M9104) or the web page.
// The check itself runs after a restart (checkAtStartup), where there is memory for HTTPS.
void checkNow(bool fromPrinter);
bool takeCheckRequest(bool& fromPrinter);
// GitHub check (and download / own update) right after a start, Wi-Fi already connected
void checkAtStartup(bool fromPrinter);
// The user accepted the offer (printer screen or web page). False with the reason when it can't start now.
bool install(String& why);
// The user pressed "Later" on the printer
void later();
// A valid image arrived from the SD card: install it in a moment
void installSoon();
// Marlin (re)started
void onMarlinBoot(bool withProtocol);

State       state();
int         progress();          // while downloading/installing
String      availableVersion();  // newer than the printer's, staged and ready ("" if none)
String      lastError();
const char* stateName();

} // namespace Updates
