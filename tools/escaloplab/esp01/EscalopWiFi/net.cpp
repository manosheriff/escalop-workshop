#include "net.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <algorithm>

#include "config.h"
#include "settings.h"
#include "util.h"

namespace Net {

namespace {
constexpr uint32_t JOIN_TIMEOUT      = 25 * SECONDS;
constexpr uint32_t BOOT_JOIN_TIMEOUT = 30 * SECONDS; // then the setup hotspot comes on
constexpr uint32_t HOTSPOT_AFTER_LOSS = 3 * MINUTES; // home Wi-Fi gone this long: setup hotspot on
constexpr uint32_t HOTSPOT_LINGER    = 90 * SECONDS; // keep the hotspot after connecting, so the phone sees "done"

const IPAddress AP_IP(192, 168, 4, 1);

DNSServer            g_dns;
bool                 g_hotspot      = false;
uint32_t             g_hotspotOffAt = 0;      // 0 = keep
bool                 g_joining      = false;
bool                 g_joinSave     = false;  // save the network when the connection works
String               g_joinSsid, g_joinPass;
uint32_t             g_joinStart    = 0;
bool                 g_connected    = false;
uint32_t             g_lostAt       = 0;      // when the home Wi-Fi went away (0 = never connected)
uint32_t             g_bootAt       = 0;
bool                 g_bootPhase    = true;   // the first connection attempt after power-on
bool                 g_mdns         = false;
bool                 g_scanning     = false;
std::vector<Network> g_networks;
uint32_t             g_apEmptySince = 0;      // no phone on the hotspot since
bool                 g_staPaused    = false;  // home Wi-Fi retries paused while a phone uses the hotspot
std::function<void(Event)> g_handler;

void emit(Event e)
{
    if (g_handler) g_handler(e);
}

String idSuffix()
{
    if (settings.serial[0]) return String(settings.serial);
    char chip[8];
    snprintf(chip, sizeof(chip), "%06X", ESP.getChipId());
    return chip;
}

void startMdns()
{
    if (g_mdns) MDNS.end();
    g_mdns = MDNS.begin(hostName());
    if (!g_mdns) return;
    MDNS.addService("http", "tcp", 80);
    // Escalop Workshop (OrcaSlicer) finds OctoPrint-compatible printers by this service
    MDNS.addService("octoprint", "tcp", 80);
    MDNS.addServiceTxt("octoprint", "tcp", "path", "/");
    MDNS.addServiceTxt("octoprint", "tcp", "api", "0.1");
    MDNS.addServiceTxt("octoprint", "tcp", "version", "1.10.0");
}

void stopHotspot()
{
    if (!g_hotspot) return;
    g_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    g_hotspot      = false;
    g_hotspotOffAt = 0;
    logf("wifi: setup hotspot off");
}

void beginSaved()
{
    if (!settings.ssid[0]) return;
    WiFi.begin(settings.ssid, settings.pass);
}
} // namespace

void onEvent(std::function<void(Event)> handler) { g_handler = handler; }

bool   connected() { return g_connected; }
bool   hotspotOn() { return g_hotspot; }
bool   joining() { return g_joining; }
String ssid() { return g_joining ? g_joinSsid : String(settings.ssid); }
String ip() { return g_connected ? WiFi.localIP().toString() : String(); }
int    rssi() { return g_connected ? WiFi.RSSI() : 0; }
String hotspotName() { return HOTSPOT_PREFIX + idSuffix(); }
String hostName()
{
    String name = HOST_PREFIX + idSuffix();
    name.toLowerCase();
    return name;
}
bool                        scanning() { return g_scanning; }
const std::vector<Network>& networks() { return g_networks; }

void begin()
{
    WiFi.persistent(false); // the module keeps its own copy (settings), so the SDK's flash isn't rewritten
    WiFi.setAutoReconnect(true);
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // fastest transfers
    WiFi.hostname(hostName());
    g_bootAt = millis();
    if (settings.ssid[0]) {
        WiFi.mode(WIFI_STA);
        g_joining   = true;
        g_joinSave  = false;
        g_joinSsid  = settings.ssid;
        g_joinPass  = settings.pass;
        g_joinStart = millis();
        beginSaved();
        logf("wifi: connecting to %s", settings.ssid);
    } else {
        startHotspot();
    }
}

void startHotspot()
{
    if (g_hotspot) {
        g_hotspotOffAt = 0;
        emit(Event::Hotspot);
        return;
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(hotspotName()); // open network, as decided: only the setup page is reachable through it
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.start(53, "*", AP_IP);
    g_hotspot      = true;
    g_hotspotOffAt = 0;
    g_apEmptySince = millis();
    logf("wifi: setup hotspot %s on", hotspotName().c_str());
    emit(Event::Hotspot);
}

void join(const String& ssidIn, const String& pass)
{
    if (ssidIn.length() == 0 || ssidIn.length() > 32 || pass.length() > 64) return;
    g_joining   = true;
    g_joinSave  = true;
    g_joinSsid  = ssidIn;
    g_joinPass  = pass;
    g_joinStart = millis();
    g_staPaused = false;
    WiFi.disconnect(false);
    WiFi.begin(g_joinSsid.c_str(), g_joinPass.length() ? g_joinPass.c_str() : nullptr);
    logf("wifi: joining %s", g_joinSsid.c_str());
    emit(Event::Joining);
}

bool joinScanned(int index, const String& pass)
{
    if (index < 0 || size_t(index) >= g_networks.size()) return false;
    join(g_networks[index].ssid, pass);
    return true;
}

void forget()
{
    logf("wifi: forgetting %s", settings.ssid);
    memset(settings.ssid, 0, sizeof(settings.ssid));
    memset(settings.pass, 0, sizeof(settings.pass));
    settingsSave();
    g_joining   = false;
    g_connected = false;
    WiFi.disconnect(true);
    startHotspot();
}

void scan()
{
    if (g_scanning) return;
    if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
    g_scanning = true;
    WiFi.scanNetworks(true, false);
}

void namesChanged()
{
    WiFi.hostname(hostName());
    if (g_connected) startMdns();
    if (g_hotspot) {
        WiFi.softAP(hotspotName());
        emit(Event::Hotspot);
    }
}

void loop()
{
    if (g_hotspot) g_dns.processNextRequest();
    if (g_mdns) MDNS.update();

    // Scan results
    if (g_scanning) {
        const int n = WiFi.scanComplete();
        if (n >= 0 || n == WIFI_SCAN_FAILED) {
            g_scanning = false;
            g_networks.clear();
            for (int i = 0; i < n; ++i) {
                const String name = WiFi.SSID(i);
                if (name.length() == 0) continue; // hidden
                auto same = std::find_if(g_networks.begin(), g_networks.end(), [&](const Network& x) { return x.ssid == name; });
                if (same != g_networks.end()) {
                    same->rssi = std::max(same->rssi, int(WiFi.RSSI(i)));
                    continue;
                }
                g_networks.push_back({name, WiFi.RSSI(i), WiFi.encryptionType(i) != ENC_TYPE_NONE});
            }
            std::sort(g_networks.begin(), g_networks.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
            if (g_networks.size() > 10) g_networks.resize(10);
            WiFi.scanDelete();
            emit(Event::ScanDone);
        }
    }

    const wl_status_t status = WiFi.status();

    // A connection attempt
    if (g_joining) {
        if (status == WL_CONNECTED) {
            g_joining = false;
            if (g_joinSave) {
                strlcpy(settings.ssid, g_joinSsid.c_str(), sizeof(settings.ssid));
                strlcpy(settings.pass, g_joinPass.c_str(), sizeof(settings.pass));
                settingsSave();
            }
            g_connected = false; // report it below, also when switching from another network
        } else if (status == WL_WRONG_PASSWORD || elapsed(g_joinStart) > (g_bootPhase ? BOOT_JOIN_TIMEOUT : JOIN_TIMEOUT)) {
            const bool wrongPassword = status == WL_WRONG_PASSWORD;
            logf("wifi: couldn't join %s (%s)", g_joinSsid.c_str(), wrongPassword ? "wrong password" : "no answer");
            g_joining = false;
            emit(wrongPassword && g_joinSave ? Event::WrongPassword : Event::Disconnected);
            if (g_joinSave) {
                WiFi.disconnect(false);
                beginSaved(); // back to the network that worked before, if any
            }
            g_bootPhase = false;
            if (!g_connected && !g_hotspot && (!settings.ssid[0] || !g_joinSave)) startHotspot();
        }
    }

    // Connected / lost
    if (status == WL_CONNECTED && !g_connected) {
        g_connected = true;
        g_lostAt    = 0;
        g_bootPhase = false;
        logf("wifi: connected to %s, %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        startMdns();
        if (g_hotspot) g_hotspotOffAt = millis() + HOTSPOT_LINGER;
        emit(Event::Connected);
    } else if (status != WL_CONNECTED && g_connected && !g_joining) {
        g_connected = false;
        g_lostAt    = millis();
        logf("wifi: lost the connection");
        emit(Event::Disconnected);
    }
    if (!g_connected && !g_hotspot && !g_joining && g_lostAt && elapsed(g_lostAt) > HOTSPOT_AFTER_LOSS) startHotspot();

    if (g_hotspot) {
        if (g_hotspotOffAt && int32_t(millis() - g_hotspotOffAt) >= 0 && g_connected) stopHotspot();
        // A phone on the hotspot: stop looking for the home network, its channel hopping drops the phone
        if (g_hotspot && !g_joining && !g_connected) {
            if (WiFi.softAPgetStationNum() > 0) {
                g_apEmptySince = millis();
                if (!g_staPaused && settings.ssid[0]) {
                    WiFi.setAutoReconnect(false);
                    WiFi.disconnect(false);
                    g_staPaused = true;
                }
            } else if (g_staPaused && elapsed(g_apEmptySince) > MINUTES) {
                g_staPaused = false;
                WiFi.setAutoReconnect(true);
                beginSaved();
            }
        }
    }
}

} // namespace Net
