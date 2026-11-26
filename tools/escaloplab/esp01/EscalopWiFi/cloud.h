#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
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

// The newest published (not draft, not pre-release) release whose tag is <tagPrefix><X.Y.Z> and that has a wanted
// asset. rank(assetName, version) says how much an asset is wanted: 0 best, 1 next, ...; negative = not at all.
// Gives the best asset's URL and name. False with `error` on network problems; true with an empty `version` when
// there is no such release.
bool latestRelease(const char* tagPrefix, const std::function<int(const String&, const String&)>& rank, String& version,
                   String& url, String& assetName, String& error);

// Variant names (Marlin repo escaloplab/variants): lowercase letters, digits and '-', at most 16 characters
bool validVariant(const String& name);

// Downloads `url` (following GitHub's redirect), giving the body to `sink` piece by piece.
// progress(received, total or -1). False with `error` when anything fails, including `sink` returning false.
bool download(const String& url, const std::function<bool(const uint8_t*, size_t)>& sink,
              const std::function<void(uint32_t, int32_t)>& progress, String& error);

// POSTs JSON to escaloplab.com (device header included) and reads the JSON answer. False only on network problems;
// `status` is the HTTP status (the answer may be an error).
bool postJson(const String& url, const String& body, JsonDocument& answer, int& status, String& error);

// What escaloplab.com answers to a check-in (PROTOCOL.md, "Check-in")
struct Checkin
{
    String serial;
    bool   variantSet = false; // the answer says which firmware variant this printer gets
    String variant;            // "" = standard
};

// Tells escaloplab.com about this printer and gets its serial number and its own setup back
bool checkin(const String& printerFirmware, Checkin& answer, String& error);

} // namespace Cloud
