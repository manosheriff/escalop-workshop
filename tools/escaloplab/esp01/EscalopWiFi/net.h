#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

// Wi-Fi: the home network, the open setup hotspot (with a captive portal so phones open the setup page by
// themselves), scanning, and the printer's network name (escalop-v80-<serial>.local).

namespace Net {

struct Network
{
    String ssid;
    int    rssi   = 0;
    bool   secure = false;
};

enum class Event { Hotspot, Joining, Connected, Disconnected, WrongPassword, ScanDone };

void begin();
void loop();

bool   connected();            // to the home Wi-Fi
bool   hotspotOn();
bool   joining();
String ssid();                 // the home network (saved or being joined)
String ip();                   // on the home network
int    rssi();
String hotspotName();          // Escalop-v80-<serial>
String hostName();             // escalop-v80-<serial>

void startHotspot();
// Connects to a network; saves it only when the connection works
void join(const String& ssid, const String& pass);
// Joins network `index` of the last scan (from the printer's screen)
bool joinScanned(int index, const String& pass);
void forget();
// Starts a scan; Event::ScanDone follows. The results are kept until the next scan.
void scan();
bool scanning();
const std::vector<Network>& networks();

// After the serial number changed: new hotspot and network names
void namesChanged();

void onEvent(std::function<void(Event)> handler);

} // namespace Net
