#pragma once

#include <Arduino.h>
#include <functional>

// Everything that needs the internet: GitHub releases (printer firmware and this module's software) and the
// check-in with escaloplab.com that hands out the printer's serial number. All HTTPS, with certificate checks.

namespace Cloud {

// At start-up: after a crash during a secure connection, pauses the internet a while (no restart loops)
void begin(bool crashedInTls);

// When Wi-Fi connects: starts setting the clock from time servers (once)
void startClock();

// Sets the clock from the internet (certificates need the date). True once the time is known.
bool ensureTime();

// The newest published (not draft, not pre-release) release whose tag is <tagPrefix><X.Y.Z>, and the URL of its
// asset ending in `extension` (".hex" or ".bin"). False with `error` on network problems; true with an empty
// `version` when there is no such release.
bool latestRelease(const char* tagPrefix, const char* extension, String& version, String& url, String& error);

// Downloads `url` (following GitHub's redirect), giving the body to `sink` piece by piece.
// progress(received, total or -1). False with `error` when anything fails, including `sink` returning false.
bool download(const String& url, const std::function<bool(const uint8_t*, size_t)>& sink,
              const std::function<void(uint32_t, int32_t)>& progress, String& error);

// What escaloplab.com answers to a check-in (PROTOCOL.md, "Check-in")
struct Checkin
{
    String serial;
    float  steps[4]   = {0, 0, 0, 0}; // X, Y, Z, E steps/mm for this printer; 0 = the firmware's own
    String fwChannel;                 // "" = standard printer firmware
};

// Tells escaloplab.com about this printer and gets its serial number and its own setup back
bool checkin(const String& printerFirmware, Checkin& answer, String& error);

} // namespace Cloud
