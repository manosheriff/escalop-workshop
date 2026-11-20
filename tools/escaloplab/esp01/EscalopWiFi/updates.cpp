#include "updates.h"

#include <Updater.h>

#include "cloud.h"
#include "config.h"
#include "flasher.h"
#include "marlin.h"
#include "net.h"
#include "settings.h"
#include "stage.h"
#include "util.h"

namespace Updates {

namespace {
State    g_state    = State::Idle;
int      g_progress = 0;
String   g_error;
uint32_t g_nextCheck   = 0;     // 0 = not scheduled yet (waits for Wi-Fi)
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

bool stagedNewer()
{
    const String installed = Marlin::info().fw;
    return settings.stagedValid && settings.stagedVersion[0] && installed.length() &&
           compareVersions(settings.stagedVersion, installed) > 0;
}

void tellPrinter(const String& line)
{
    if (Marlin::protocol()) Marlin::send(line);
}

bool downloadFirmware(const String& url, const String& version, bool fromPrinter)
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
    settings.stagedSize = hex.size();
    settings.stagedCrc  = hex.crc();
    settingsSave();
    logf("update: firmware %s ready", version.c_str());
    return true;
}

void updateSelf(const String& url, const String& version)
{
    logf("update: installing Wi-Fi module software %s", version.c_str());
    // The module's update uses the staging space
    settings.stagedValid = 0;
    settingsSave();
    const uint32_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(space)) {
        logf("update: no room for the module's update");
        return;
    }
    String     err;
    const bool ok = Cloud::download(
        url, [](const uint8_t* d, size_t n) { return Update.write(const_cast<uint8_t*>(d), n) == n; },
        [](uint32_t, int32_t) { Marlin::poll(); }, err);
    if (ok && Update.end(true)) {
        logf("update: module updated, restarting");
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
    String version, url, err;

    if (!Net::connected()) {
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }

    // 1. This module's own software
    if (!Cloud::latestRelease(WIFI_TAG, ".bin.gz", version, url, err)) {
        logf("update: check failed: %s", err.c_str());
        g_error = "no connection to GitHub";
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }
    if (version.length() && compareVersions(version, WIFI_VERSION) > 0) updateSelf(url, version); // restarts on success

    // 2. The printer's firmware
    if (!Cloud::latestRelease(FIRMWARE_TAG, ".hex", version, url, err)) {
        logf("update: check failed: %s", err.c_str());
        g_error = "no connection to GitHub";
        if (fromPrinter) tellPrinter("M9108 NOINTERNET");
        g_state = State::Idle;
        return;
    }
    const String installed = Marlin::info().fw;
    if (version.length() == 0 || (installed.length() && compareVersions(version, installed) <= 0)) {
        logf("update: printer firmware %s is up to date", installed.c_str());
        if (fromPrinter) tellPrinter("M9108 UPTODATE");
        g_state = State::Idle;
        return;
    }
    if (!(settings.stagedValid && version == settings.stagedVersion) && !downloadFirmware(url, version, fromPrinter)) {
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
        ok = Flasher::install(settings.stagedSize, [](int p) { g_progress = p; }, err);
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
        g_state       = State::Failed;
        g_awaitResult = false;
        g_retryAt     = millis() + 2 * MINUTES;
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

    if (Net::connected() && g_nextCheck == 0) g_nextCheck = millis() + FIRST_CHECK_AFTER;
    if (g_checkWanted || (g_nextCheck && due(g_nextCheck) && Net::connected())) {
        const bool fromPrinter = g_checkWanted && g_checkFromPrinter;
        g_checkWanted = false;
        g_nextCheck   = millis() + UPDATE_CHECK_EVERY;
        runCheck(fromPrinter);
    }

    if (g_later && elapsed(g_laterAt) > OFFER_AGAIN_AFTER) {
        g_later   = false;
        g_offered = false;
    }
    if (stagedNewer() && Marlin::protocol() && !g_offered && !g_later && g_state != State::Installing && printerIdle()) {
        Marlin::send(String("M9104 ") + settings.stagedVersion);
        g_offered = true;
        logf("update: offered firmware %s", settings.stagedVersion);
    }
    if (g_state == State::Ready && !stagedNewer()) g_state = State::Idle;
}

void checkNow(bool fromPrinter)
{
    g_checkWanted      = true;
    g_checkFromPrinter = fromPrinter;
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
