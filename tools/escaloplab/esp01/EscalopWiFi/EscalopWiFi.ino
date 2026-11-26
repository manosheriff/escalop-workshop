// Escaloplab v80 Wi-Fi module (ESP-01, ESP8266, 1 MB flash)
//
//  - Wi-Fi setup two ways: the open setup hotspot "Escalop-v80-<serial>" with a phone page, or the printer's screen
//  - Installs new printer firmware from GitHub automatically (after asking on the printer's screen), or a .hex the
//    printer reads from its SD card, through the Mega 2560's bootloader
//  - Fast printing over Wi-Fi: Escalop Workshop uploads like to OctoPrint; the file goes straight onto the SD card
//  - The printer's web page: status, temperatures, files, updates
//  - Updates itself from GitHub; gets its serial number from escaloplab.com
//  - Remote printing: prints sent from a phone on models.escaloplab.com download straight onto the SD card
//
// How it talks to Marlin: ../PROTOCOL.md. Wiring and first installation: ../README.md.
// Arduino IDE: board "Generic ESP8266 Module", Flash size "1MB (FS:none OTA:~502KB)", Flash mode "DOUT",
// CPU frequency "160 MHz", library "ArduinoJson" (7.x).

#include <ESP8266WiFi.h>
#include <user_interface.h> // reset reasons

#include "cloud.h"
#include "flasher.h"
#include "config.h"
#include "marlin.h"
#include "net.h"
#include "remote.h"
#include "settings.h"
#include "transfer.h"
#include "updates.h"
#include "util.h"
#include "web.h"

namespace {

// Requests from the printer's menu, handled from loop() (never inside Marlin::poll())
struct Request
{
    String cmd, args;
};
std::vector<Request> g_requests;

bool     g_attached     = false; // the printer's link is set up (state sent, auto-reports on)
bool     g_scanForPrinter = false;
uint32_t g_lastInfoAsk  = 0;
int      g_unanswered   = 0;     // M9100 without an INFO (older firmware)
uint32_t g_dailyAt      = 0;     // when the daily internet tasks are due

// Internet work, done right after a (re)start while memory is free (see runInternetTasks)
enum : uint32_t { TASK_CHECKIN = 1, TASK_UPDATES = 2, TASK_FROM_PRINTER = 4 };

void sendSerial()
{
    Marlin::send(String("M9107 ") + (settings.serial[0] ? settings.serial : "-"));
}

// M9101 with the current Wi-Fi state (PROTOCOL.md section 3)
void sendWifiState()
{
    if (!Marlin::protocol()) return;
    if (Net::joining()) Marlin::send("M9101 JOIN " + toHex(Net::ssid()) + " -");
    else if (Net::connected()) Marlin::send("M9101 ON " + toHex(Net::ssid()) + " " + Net::ip());
    else if (Net::hotspotOn()) Marlin::send("M9101 AP " + toHex(Net::hotspotName()) + " -");
    else Marlin::send("M9101 OFF " + toHex(settings.ssid) + " -");
}

// Once per Marlin start (or when this module starts next to a running printer)
void attachPrinter()
{
    g_attached = true;
    sendWifiState();
    sendSerial();
    Marlin::send("M155 S2"); // temperatures every 2 s
    Marlin::send("M27 S2");  // SD print progress every 2 s
}

void sendScanResults()
{
    const auto& list = Net::networks();
    Marlin::send("M9102 " + String(list.size()));
    for (size_t i = 0; i < list.size(); ++i)
        Marlin::send("M9103 " + String(i) + " " + String(list[i].rssi) + " " + (list[i].secure ? "1 " : "0 ") + toHex(list[i].ssid));
}

void handleRequest(const Request& r)
{
    if (r.cmd == "SCAN") {
        g_scanForPrinter = true;
        Net::scan();
    } else if (r.cmd == "JOIN") {
        String parts[2];
        String pass;
        if (splitWords(r.args, parts, 2) == 2 && fromHex(parts[1], pass) && !Net::joinScanned(parts[0].toInt(), pass))
            Marlin::send("M9101 OFF - -");
    } else if (r.cmd == "HOTSPOT") {
        Net::startHotspot();
    } else if (r.cmd == "RESETWIFI") {
        Net::forget();
    } else if (r.cmd == "STATE") {
        sendWifiState();
        sendSerial();
    } else if (r.cmd == "CHECK") {
        Updates::checkNow(true);
    } else if (r.cmd == "UPDATE") {
        String why;
        if (!Updates::install(why)) {
            logf("update: can't install now: %s", why.c_str());
            Marlin::send("M9106 FAIL " + (Marlin::info().fw.length() ? Marlin::info().fw : String("-")));
        }
    } else if (r.cmd == "LATER") {
        Updates::later();
    } else if (r.cmd == "SDFW") {
        String error;
        if (receiveSdFirmware(strtoul(r.args.c_str(), nullptr, 10), error)) Updates::installSoon();
        else logf("sdfw: %s", error.c_str());
    }
}

void onNetEvent(Net::Event e)
{
    switch (e) {
    case Net::Event::ScanDone:
        if (g_scanForPrinter) {
            g_scanForPrinter = false;
            sendScanResults();
        }
        return;
    case Net::Event::WrongPassword:
        Web::setJoinResult("badpw");
        Marlin::send("M9101 BADPW " + toHex(Net::ssid()) + " -");
        return;
    case Net::Event::Connected:
        Web::setJoinResult("ok");
        Cloud::startClock();
        break;
    case Net::Event::Disconnected:
        Web::setJoinResult("fail");
        break;
    case Net::Event::Joining:
    case Net::Event::Hotspot: break;
    }
    sendWifiState();
}

void checkin()
{
    Cloud::Checkin answer;
    String         error;
    if (!Cloud::checkin(Marlin::info().fw, answer, error)) {
        logf("checkin: %s", error.c_str());
        return;
    }
    bool changed = false;
    if (answer.serial != settings.serial) {
        logf("checkin: serial number %s", answer.serial.c_str());
        strlcpy(settings.serial, answer.serial.c_str(), sizeof(settings.serial));
        changed = true; // the hotspot and network names follow when Net starts
    }
    if (answer.variantSet && (!settings.siteVariantSet || answer.variant != settings.siteVariant)) {
        settings.siteVariantSet = 1;
        strlcpy(settings.siteVariant, answer.variant.c_str(), sizeof(settings.siteVariant));
        logf("checkin: firmware variant \"%s\"", settings.siteVariant);
        changed = true;
    }
    if (changed) settingsSave();
}

// HTTPS needs ~25 KB, more than is free once the web server, mDNS and the rest run. So the check-in and the update
// check run right after a start, before those start: at power-on, once a day, and when someone asks for an update
// check (the module restarts for it; the printer isn't affected, only Wi-Fi is away for ~30 s).
void runInternetTasks(uint32_t tasks)
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(Net::hostName());
    WiFi.begin(settings.ssid, settings.pass);
    // Say hello to the printer first: its "Check for updates" screen gives up on a silent module after 5 s
    uint32_t       lastHello = 0;
    const uint32_t start     = millis();
    while (WiFi.status() != WL_CONNECTED && elapsed(start) < 20 * SECONDS) {
        Marlin::poll();
        if (!Marlin::protocol() && elapsed(lastHello) > 2 * SECONDS) {
            lastHello = millis();
            Marlin::send("M9100");
        }
        if (Marlin::protocol() && !g_attached) attachPrinter();
        delay(10);
    }
    if (WiFi.status() != WL_CONNECTED) {
        logf("start-up tasks: no Wi-Fi");
        if (tasks & TASK_FROM_PRINTER) Updates::checkAtStartup(true); // answers "no internet"
        return;
    }
    Cloud::startClock();
    logf("start-up tasks (heap %lu)", (unsigned long)ESP.getFreeHeap());
    // The printer's firmware version, for the check-in and the update check
    for (int i = 0; i < 3 && !Marlin::info().fw.length(); ++i) Marlin::requestInfo(2000);
    if (tasks & TASK_CHECKIN) checkin();
    if (tasks & TASK_UPDATES) Updates::checkAtStartup(tasks & TASK_FROM_PRINTER);
}

// Restarts to run internet tasks (not in the middle of an upload or a firmware install)
void restartForTasks(uint32_t tasks)
{
    logf("restarting for internet tasks (%lu)", (unsigned long)tasks);
    setBootTasks(tasks);
    markPlannedRestart();
    delay(200);
    ESP.restart();
}

} // namespace

void setup()
{
    // First of all: never pull the printer's reset line. High before it becomes an output: the other way round
    // gives a microsecond low pulse, which is enough to restart the Mega.
    digitalWrite(RESET_PIN, HIGH);
    pinMode(RESET_PIN, OUTPUT);

    settingsLoad();
    Marlin::begin();
    const uint32_t bootReason = ESP.getResetInfoPtr()->reason;
    const bool     powerOn    = bootReason == REASON_DEFAULT_RST || bootReason == REASON_EXT_SYS_RST;
    const bool     woke       = powerOn && Flasher::wakePrinter();
    const bool crashedInTls = crashMemoryBegin(); // before anything is logged: logging overwrites the crash memory
    logf("Escaloplab v80 Wi-Fi %s starting (%s)", WIFI_VERSION, ESP.getResetReason().c_str());
    if (woke) logf("marlin: the printer's bootloader was waiting; started the firmware");
    Cloud::begin(crashedInTls);
    uint32_t tasks = takeBootTasks();
    if (powerOn) tasks |= TASK_CHECKIN | TASK_UPDATES;

    Marlin::onEsc([](const String& cmd, const String& args) {
        if (g_requests.size() < 8) g_requests.push_back({cmd, args});
    });
    Marlin::onBoot([](bool withProtocol) {
        g_unanswered = 0;
        Updates::onMarlinBoot(withProtocol);
        if (withProtocol) attachPrinter();
        else {
            g_attached = false;
            Marlin::send("M155 S2");
        }
    });

    g_lastInfoAsk = millis() - 8 * SECONDS; // first M9100 after 2 s

    if (tasks && settings.ssid[0]) runInternetTasks(tasks);
    g_dailyAt = millis() + UPDATE_CHECK_EVERY;

    Net::onEvent(onNetEvent);
    Net::begin();
    Web::begin();
    Updates::begin();
}

void loop()
{
    Marlin::poll();

    while (!g_requests.empty()) {
        const Request r = g_requests.front();
        g_requests.erase(g_requests.begin());
        handleRequest(r);
    }

    Net::loop();
    Web::loop();

    // The printer's state: ask every 10 s (INFO); older firmware without the protocol: its version once
    if (elapsed(g_lastInfoAsk) > 10 * SECONDS) {
        g_lastInfoAsk = millis();
        Marlin::send("M9100");
        if (!Marlin::protocol() && Marlin::alive() && ++g_unanswered == 3) {
            // Firmware older than the Wi-Fi protocol: learn its version, get temperatures for the web page
            Marlin::send("M115");
            Marlin::send("M155 S2");
        }
    }
    if (Marlin::protocol()) g_unanswered = 0;
    if (Marlin::protocol() && !g_attached) attachPrinter(); // this module started while the printer was running
    if (!Marlin::protocol()) g_attached = false;

    if (!Web::uploading()) Updates::loop();

    // Remote printing: report to escaloplab.com, fetch prints sent from a phone
    if (!Web::uploading()) Remote::loop();

    // The KILL button on the printer's screen halts Marlin ("Printer halted, please reset"): restart it
    if (Marlin::takeKillButton()) {
        logf("marlin: KILL button pressed, restarting the printer");
        Flasher::resetBoard();
    }

    // Internet tasks: once a day, or "Check for updates" (printer menu or web page). Never during an upload or an
    // install (Updates::loop() returns only when an install is over).
    if (!Web::uploading() && Net::connected()) {
        bool fromPrinter = false;
        if (Updates::takeCheckRequest(fromPrinter)) restartForTasks(TASK_UPDATES | (fromPrinter ? TASK_FROM_PRINTER : 0));
        if (int32_t(millis() - g_dailyAt) >= 0) restartForTasks(TASK_CHECKIN | TASK_UPDATES);
    }
}
