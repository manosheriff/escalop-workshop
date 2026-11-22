#include "updates.h"

#include <Updater.h>

#include "cloud.h"
#include "config.h"
#include "flasher.h"
#include "marlin.h"
#include <ESP8266WiFi.h>
#include <vector>
#include "settings.h"
#include "stage.h"
#include "util.h"
#include "web.h"

namespace Updates {

namespace {
State    g_state    = State::Idle;
int      g_progress = 0;
String   g_error;
bool     g_checkWanted = false; // checkNow() asked for one
bool     g_checkFromPrinter = false;
bool     g_offered     = false; // M9104 sent since Marlin last started
bool     g_later       = false;
uint32_t g_laterAt     = 0;
bool     g_installWanted = false;
uint32_t g_installAt     = 0;
bool     g_awaitResult   = false; // installed: report M9106 when the new firmware says hello
String   g_expected;              // the version just installed ("" = from the SD card, unknown)
bool     g_pendingChecked = false;
uint32_t g_retryAt        = 0;

bool due(uint32_t at) { return int32_t(millis() - at) >= 0; }

// Safe to take the printer over: not printing, heaters off, nothing moving
bool printerIdle()
{
    const Marlin::Info& i = Marlin::info();
    if (Marlin::protocol()) return i.valid && !i.printing && !i.busy && elapsed(i.at) < 30 * SECONDS;
    // Older firmware without the protocol: judge from its reports
    const Marlin::Temps& t = Marlin::temps();
    return Marlin::alive() && !Marlin::sdProgress().printing && t.hotendTarget == 0 && t.bedTarget == 0 && t.hotend < 50 &&
           t.bed < 45;
}

// Staged firmware worth installing: a newer version, or the same version for another variant (staff changed it)
bool stagedNewer()
{
    const String installed = Marlin::info().fw;
    if (!settings.stagedValid || !settings.stagedVersion[0] || !installed.length()) return false;
    const int cmp = compareVersions(settings.stagedVersion, installed);
    return cmp > 0 || (cmp == 0 && Marlin::info().variant != settings.stagedVariant);
}

// The variant this printer should run: what escaloplab.com says, else what it runs now ("" = standard).
// A printer's own file (named after its serial number) comes first when a release has one; see firmwareRank.
String wantedVariant()
{
    if (settings.siteVariantSet) return settings.siteVariant;
    const String running = Marlin::info().variant;
    return running == settings.serial ? String() : running; // its own file was removed: back to standard
}

// "EscalopV80_Firmware_1.7.5.hex" -> "", "EscalopV80_Firmware_1.7.5_300-bowden.hex" -> "300-bowden"; "?" otherwise
String assetVariant(const String& name, const String& version)
{
    const String head = "EscalopV80_Firmware_" + version;
    if (!name.startsWith(head) || !name.endsWith(".hex")) return "?";
    const String rest = name.substring(head.length(), name.length() - 4);
    if (rest.length() == 0) return "";
    return rest[0] == '_' && rest.length() > 1 && Cloud::validVariant(rest.substring(1)) ? rest.substring(1) : String("?");
}

// Lines for the printer that come before it answered (start-up checks) wait until it has
std::vector<String> g_deferred;

void tellPrinter(const String& line)
{
    if (Marlin::protocol()) Marlin::send(line);
    else if (!line.startsWith("M9105") && g_deferred.size() < 4) g_deferred.push_back(line);
}

bool downloadFirmware(const String& url, const String& version, const String& variant, bool fromPrinter)
{
    g_state    = State::Downloading;
    g_progress = 0;
    settings.stagedValid = 0;
    settingsSave();
    if (!Stage::available() || !Stage::erase()) {
        g_error = "no room for the firmware";
        return false;
    }
    Stage::HexWriter& hex = Stage::hexWriter();
    hex.begin();
    int    lastReported = -10;
    String err;
    settings.stagedVariant[0] = 0;
    const bool ok = Cloud::download(
        url, [&hex](const uint8_t* d, size_t n) { return hex.feed(d, n); },
        [&](uint32_t received, int32_t total) {
            g_progress = total > 0 ? int(uint64_t(received) * 100 / uint32_t(total)) : 0;
            if (fromPrinter && g_progress >= lastReported + 10) {
                lastReported = g_progress;
                tellPrinter("M9105 DOWNLOAD " + String(g_progress));
            }
            Marlin::poll();
        },
        err);
    if (!ok || !hex.finish()) {
        g_error = "download failed: " + (ok ? hex.error() : err);
        logf("update: %s", g_error.c_str());
        return false;
    }
    settings.stagedValid = 1;
    strlcpy(settings.stagedVersion, version.c_str(), sizeof(settings.stagedVersion));
    strlcpy(settings.stagedVariant, variant.c_str(), sizeof(settings.stagedVariant));
    settings.stagedSize = hex.size();
    settings.stagedCrc  = hex.crc();
    settingsSave();
    logf("update: firmware %s%s%s ready", version.c_str(), variant.length() ? " " : "", variant.c_str());
    return true;
}

void updateSelf(const String& url, const String& version)
{
    logf("update: installing Wi-Fi module software %s", version.c_str());
    // The module's update uses the staging space
    settings.stagedValid = 0;
    settingsSave();
    // Update.begin() takes a 4 KB buffer. Taken before the download, the secure connection then runs out of memory
    // and the module crashes (1.0.18 did), so it starts with the first bytes, once the connection is up.
    const uint32_t space  = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    bool           begun  = false;
    String         err;
    const bool     ok = Cloud::download(
        url,
        [&begun, space](const uint8_t* d, size_t n) {
            if (!begun) {
                if (!Update.begin(space)) {
                    logf("update: no room for the module's update (heap %lu)", (unsigned long)ESP.getFreeHeap());
                    return false;
                }
                begun = true;
            }
            return Update.write(const_cast<uint8_t*>(d), n) == n;
        },
        [](uint32_t, int32_t) { Marlin::poll(); }, err);
    if (!begun) {
        logf("update: module update failed: %s", err.c_str());
        return;
    }
    if (ok && Update.end(true)) {
        logf("update: module updated, restarting");
        setBootTasks(1 | 2); // check in with the new version, then look for printer firmware
        markPlannedRestart();
        delay(300);
        ESP.restart(); // the printer keeps running; only Wi-Fi blinks off for a few seconds
    }
    Update.end(false);
    logf("update: module update failed: %s", ok ? Update.getErrorString().c_str() : err.c_str());
}

void runCheck(bool fromPrinter)
{
    g_state = State::Checking;
    g_error = "";
    String version, url, asset, err;

    if (WiFi.status() != WL_CONNECTED) {
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }

    // 1. This module's own software
    const auto wifiRank = [](const String& name, const String&) { return name.endsWith(".bin.gz") ? 0 : -1; };
    if (!Cloud::latestRelease(WIFI_TAG, wifiRank, version, url, asset, err)) {
        logf("update: check failed: %s", err.c_str());
        g_error = "no connection to GitHub";
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }
    if (version.length() && compareVersions(version, WIFI_VERSION) > 0) updateSelf(url, version); // restarts on success

    // 2. The printer's firmware: its own file (named after its serial number), else its variant, else standard
    const String serial = settings.serial, variant = wantedVariant();
    const auto   fwRank = [&serial, &variant](const String& name, const String& v) {
        const String a = assetVariant(name, v);
        if (a == "?") return -1;
        if (serial.length() && a == serial) return 0;
        return a == variant ? 1 : -1;
    };
    if (!Cloud::latestRelease(FIRMWARE_TAG, fwRank, version, url, asset, err)) {
        logf("update: check failed: %s", err.c_str());
        g_error = "no connection to GitHub";
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }
    const String installed = Marlin::info().fw, chosen = version.length() ? assetVariant(asset, version) : String();
    const int    cmp       = installed.length() && version.length() ? compareVersions(version, installed) : 1;
    if (version.length() == 0 || cmp < 0 || (cmp == 0 && chosen == Marlin::info().variant)) {
        logf("update: printer firmware %s is up to date", installed.c_str());
        if (fromPrinter) tellPrinter("M9108 UPTODATE");
        g_state = State::Idle;
        return;
    }
    if (!(settings.stagedValid && version == settings.stagedVersion && chosen == settings.stagedVariant) &&
        !downloadFirmware(url, version, chosen, fromPrinter)) {
        if (fromPrinter) tellPrinter("M9108 DLFAIL");
        g_state = State::Failed;
        return;
    }
    g_state = State::Ready;
    if (fromPrinter) {
        g_offered = false; // offer it now, even after "Later"
        g_later   = false;
    }
}

void runInstall()
{
    g_installWanted = false;
    if (!settings.stagedValid) return;
    g_state    = State::Installing;
    g_progress = 0;
    g_error    = "";
    if (Stage::crc(settings.stagedSize) != settings.stagedCrc) {
        settings.stagedValid = 0;
        settingsSave();
        g_error = "the downloaded firmware was damaged; it will be downloaded again";
        g_state = State::Failed;
        tellPrinter("M9106 FAIL " + (Marlin::info().fw.length() ? Marlin::info().fw : String("-")));
        return;
    }
    tellPrinter("M9105 CHECK 100");
    tellPrinter("M9105 INSTALL 0");
    Marlin::drain(3000);
    delay(800); // let the printer show "Installing - don't turn off the printer"

    settings.pendingInstall = 1;
    settingsSave();
    g_expected    = settings.stagedVersion;
    g_awaitResult = true;

    String err;
    bool   ok = false;
    Marlin::pause();
    for (int attempt = 0; attempt < 3 && !ok; ++attempt)
        ok = Flasher::install(settings.stagedSize,
                              [](int p) {
                                  g_progress = p;
                                  Web::serviceDuringInstall(); // the web page shows the percentage meanwhile
                              },
                              err);
    Marlin::resume();
    Marlin::relink();

    if (ok) {
        settings.pendingInstall = 0;
        settingsSave();
        g_state   = State::Idle;
        g_offered = false;
    } else {
        // The bootloader is intact: try again in a while, and after the next power-on
        g_error       = "installing failed: " + err;
        // If the board never restarted (no reset wire), Marlin still runs and waits on "Installing": tell it
        tellPrinter("M9106 FAIL " + (Marlin::info().fw.length() ? Marlin::info().fw : String("-")));
        g_state       = State::Failed;
        g_awaitResult = false;
        g_retryAt     = millis() + 2 * MINUTES;
        // Did the board never restart (old firmware still running)? Then there's nothing to finish later.
        const uint32_t start = millis();
        while (elapsed(start) < 6 * SECONDS && !Marlin::alive()) {
            if (elapsed(start) % 2000 < 20) Marlin::send("M9100");
            Marlin::poll();
            delay(10);
        }
        if (Marlin::alive()) {
            settings.pendingInstall = 0;
            settingsSave();
            logf("update: the old firmware is still running");
        }
    }
}
} // namespace

void begin() {}

void loop()
{
    if (g_installWanted && due(g_installAt)) runInstall();

    // Power was cut while installing: finish the job (a half-written printer has no working firmware)
    if (settings.pendingInstall && !g_pendingChecked && millis() > 30 * SECONDS) { // time to find the link speed
        g_pendingChecked = true;
        if (Marlin::alive() && Marlin::info().fw.length() &&
            (!settings.stagedVersion[0] || Marlin::info().fw == settings.stagedVersion)) {
            settings.pendingInstall = 0; // it had finished
            settingsSave();
        } else if (!Marlin::alive() && settings.stagedValid) {
            logf("update: the printer doesn't answer after an unfinished install, installing again");
            runInstall();
        } else {
            settings.pendingInstall = 0; // the old firmware is still there and running
            settingsSave();
        }
    }
    if (g_state == State::Failed && settings.pendingInstall && settings.stagedValid && !Marlin::alive() && due(g_retryAt))
        runInstall();

    if (Marlin::protocol() && !g_deferred.empty()) {
        for (const String& line : g_deferred) Marlin::send(line);
        g_deferred.clear();
    }

    if (g_later && elapsed(g_laterAt) > OFFER_AGAIN_AFTER) {
        g_later   = false;
        g_offered = false;
    }
    if (stagedNewer() && Marlin::protocol() && !g_offered && !g_later && g_state != State::Installing && printerIdle()) {
        Marlin::send(String("M9104 ") + settings.stagedVersion);
        g_offered = true;
        g_error   = ""; // an earlier hiccup doesn't matter any more
        logf("update: offered firmware %s", settings.stagedVersion);
    }
    if (g_state == State::Ready && !stagedNewer()) g_state = State::Idle;
}

void checkNow(bool fromPrinter)
{
    g_checkWanted      = true;
    g_checkFromPrinter = fromPrinter;
}

bool takeCheckRequest(bool& fromPrinter)
{
    if (!g_checkWanted) return false;
    g_checkWanted = false;
    fromPrinter   = g_checkFromPrinter;
    return true;
}

void checkAtStartup(bool fromPrinter)
{
    runCheck(fromPrinter);
    if (fromPrinter) {
        g_offered = false; // offer what was found as soon as the printer answers
        g_later   = false;
    }
}

bool install(String& why)
{
    if (!settings.stagedValid) {
        why = "No new firmware is ready";
        return false;
    }
    if (g_state == State::Installing || g_installWanted) {
        why = "Already installing";
        return false;
    }
    if (!printerIdle()) {
        why = "The printer is busy (printing, heating or moving). Try again when it's idle.";
        return false;
    }
    g_installWanted = true;
    g_installAt     = millis() + 300;
    return true;
}

void later()
{
    g_later   = true;
    g_laterAt = millis();
}

void installSoon()
{
    g_installWanted = true;
    g_installAt     = millis() + 1500;
}

void onMarlinBoot(bool withProtocol)
{
    g_offered = false; // a pending offer disappeared with the restart
    if (!g_awaitResult) return;
    g_awaitResult = false;
    const String fw = Marlin::info().fw;
    const bool   ok = withProtocol && (g_expected.length() == 0 || fw == g_expected);
    logf("update: printer restarted with firmware %s (%s)", fw.c_str(), ok ? "ok" : "unexpected");
    if (withProtocol) Marlin::send(String("M9106 ") + (ok ? "OK " : "FAIL ") + (fw.length() ? fw : String("-")));
    if (!ok) g_error = "the printer started with firmware " + (fw.length() ? fw : String("unknown"));
}

State  state() { return g_state; }
int    progress() { return g_progress; }
String availableVersion() { return stagedNewer() ? String(settings.stagedVersion) : String(); }
String lastError() { return g_error; }

const char* stateName()
{
    switch (g_state) {
    case State::Idle: return "idle";
    case State::Checking: return "checking";
    case State::Downloading: return "downloading";
    case State::Ready: return "ready";
    case State::Installing: return "installing";
    case State::Failed: return "failed";
    }
    return "idle";
}

} // namespace Updates
