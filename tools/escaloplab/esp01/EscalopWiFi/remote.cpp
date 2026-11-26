#include "remote.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "cloud.h"
#include "config.h"
#include "marlin.h"
#include "net.h"
#include "settings.h"
#include "transfer.h"
#include "updates.h"
#include "util.h"
#include "web.h"

namespace Remote {

namespace {

// How often to ask: idle printers a little faster (a print should start soon after "Print" on the phone)
constexpr uint32_t POLL_IDLE     = 15 * SECONDS;
constexpr uint32_t POLL_PRINTING = 30 * SECONDS;
constexpr uint32_t BACKOFF_MAX   = 5 * MINUTES;
// The regular checks use a secure connection with an 8 KB receive buffer (our answers are a few hundred bytes):
// ~22 KB in all (receive buffer 8.5 KB, BearSSL's own stack 6.2 KB, TLS and certificate state ~7 KB), which fits next
// to the web server. Below this, the round is skipped instead of risking a crash.
constexpr uint16_t SMALL_RX   = 8192 + 325;
constexpr uint32_t NEED_BLOCK = 9000;
constexpr uint32_t NEED_HEAP  = 25000;
// How long after M24 a print may take to show up as printing (heating, homing, leveling)
constexpr uint32_t START_GRACE = 4 * MINUTES;

enum class State { Off, Waiting, Unlinked, Linked, Printing, LowMemory, Error };

State    g_state     = State::Off;
uint32_t g_nextAt    = 20 * SECONDS; // the first check soon after start-up
uint32_t g_backoff   = 0;
String   g_code;                     // pairing code while not linked
uint32_t g_codeShown = 0;            // last time the code went to the printer's screen
bool     g_linked    = false;
bool     g_busy      = false;
uint32_t g_lowLogged = 0;
uint32_t g_holdJobsUntil = 0; // after a failed fetch at start-up: don't restart for the job again right away
bool     g_atStartup = false; // full-size connections (right after a restart)
bool     g_reported  = true;  // the watched job's "printing" reached the website

// The print this module started for the website
String   g_job;          // its id; "" = none
String   g_jobFile;      // its 8.3 name on the SD card
uint32_t g_jobStarted = 0;
uint32_t g_doneBefore = 0;
bool     g_seenPrinting = false;

// A job event that couldn't be sent yet (sent again at the next check)
String g_pendingJob, g_pendingEvent, g_pendingError;

bool printing() { return Marlin::info().printing || Marlin::sdProgress().printing; }

bool validToken(const String& t)
{
    if (t.length() != 48) return false;
    for (size_t i = 0; i < t.length(); ++i)
        if (!isHexadecimalDigit(t[i]) || isUpperCase(t[i])) return false;
    return true;
}

bool validId(const String& id)
{
    if (id.length() != 36) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!(isDigit(c) || (c >= 'a' && c <= 'f') || c == '-')) return false;
    }
    return true;
}

void identify(JsonDocument& body)
{
    String mac = WiFi.macAddress();
    mac.toLowerCase();
    char chip[8];
    snprintf(chip, sizeof(chip), "%06x", ESP.getChipId());
    body["mac"]   = mac;
    body["chip"]  = chip;
    body["token"] = settings.remoteToken;
}

bool sendEvent(const String& job, const char* event, const String& error = String())
{
    JsonDocument body;
    identify(body);
    body["job"]   = job;
    body["event"] = event;
    if (error.length()) body["error"] = error.substring(0, 180);
    String payload;
    serializeJson(body, payload);
    JsonDocument answer;
    int          status = 0;
    String       err;
    if (!Cloud::postJson(JOB_URL, payload, answer, status, err, SMALL_RX) || (status >= 500)) {
        logf("remote: %s for %s not sent (%s)", event, job.substring(0, 8).c_str(), err.length() ? err.c_str() : String(status).c_str());
        g_pendingJob   = job;
        g_pendingEvent = event;
        g_pendingError = error;
        return false;
    }
    logf("remote: job %s %s", job.substring(0, 8).c_str(), event);
    return true;
}

void showOnPrinter(const String& text)
{
    if (Marlin::protocol() && !printing()) Marlin::send("M117 " + text);
}

void rememberJob(const String& id, const String& file = String())
{
    strlcpy(settings.remoteJob, id.c_str(), sizeof(settings.remoteJob));
    strlcpy(settings.remoteFile, file.c_str(), sizeof(settings.remoteFile));
    settingsSave();
}

// Website prints don't stay on the card: delete the file once the print is over (or never got going)
void deleteFile(const String& name)
{
    if (!name.length()) return;
    for (int i = 0; i < 3 && !Marlin::protocol(); ++i) Marlin::requestInfo(2000);
    Marlin::send("M30 " + name);
    logf("remote: deleted %s from the SD card", name.c_str());
}

// The failure as a short code the website shows in the customer's language ("code: details")
String failureFor(const String& error)
{
    if (error.indexOf("SD card couldn't") >= 0 || error.indexOf("couldn't create the file") >= 0 ||
        error.indexOf("incomplete") >= 0)
        return "sd_write: " + error;
    if (error.indexOf("Insert the SD card") >= 0) return "sd_missing: " + error;
    if (error.indexOf("stopped answering") >= 0 || error.indexOf("didn't answer") >= 0) return "printer_restarted: " + error;
    if (error.indexOf("busy") >= 0) return "printer_busy: " + error;
    return "download: " + (error.length() ? error : String("The download failed"));
}

void finishJob(const char* event, const String& error = String())
{
    if (!g_job.length()) return;
    sendEvent(g_job, event, error);
    deleteFile(g_jobFile);
    g_job = "";
    g_jobFile = "";
    rememberJob("");
}

// Downloads the G-code onto the SD card and starts it
void runJob(const String& id, const String& url, uint32_t size, const String& title)
{
    if (!Marlin::requestInfo(2500)) return; // the printer isn't answering: try again at the next check
    if (!Marlin::info().sd) {
        static uint32_t told = 0;
        if (!told || elapsed(told) > 2 * MINUTES) { // the printer's screen says "insert the SD card"
            told = millis();
            Marlin::send("M9108 NOSD");
        }
        return; // the job waits until a card is in (the website shows "No SD card")
    }
    if (printing() || Marlin::info().busy) return;
    if (!sendEvent(id, "sending")) return;

    g_busy = true;
    const String name  = sdNameFor(title + "-" + id);
    const uint32_t t0  = millis();
    SdUpload       up;
    String         error;
    bool           ok = up.begin(size, name, error);
    if (ok) {
        ok = Cloud::download(
            url, [&](const uint8_t* data, size_t len) { return up.write(data, len, error); }, [](uint32_t, int32_t) {}, error);
        if (ok) ok = up.finish(error);
        else up.abort();
    }
    g_busy = false;
    if (!ok) {
        logf("remote: job %s failed: %s", id.substring(0, 8).c_str(), error.c_str());
        const String why = failureFor(error);
        sendEvent(id, "failed", why);
        if (why.startsWith("sd_write")) {
            showOnPrinter("SD card error: format it");
            delay(300);
        }
        deleteFile(name); // a half-written (or empty) file is useless
        return;
    }
    logf("remote: %s on the SD card in %lu s, starting", name.c_str(), (unsigned long)(elapsed(t0) / 1000));
    g_job          = id;
    g_jobFile      = name;
    g_jobStarted   = millis();
    g_doneBefore   = Marlin::printsDone();
    g_seenPrinting = false;
    g_reported     = false;
    rememberJob(id, name); // before anything else can go wrong: a restart keeps watching it
    // Start it and make sure it started (the printer may still be closing the file): up to 3 tries
    bool started = false;
    for (int attempt = 0; attempt < 3 && !started; ++attempt) {
        Marlin::requestInfo(2000);
        Marlin::send("M23 " + name);
        Marlin::send("M24");
        const uint32_t s = millis();
        while (elapsed(s) < 8 * SECONDS && !(started = printing())) {
            Marlin::poll();
            delay(20);
            if (elapsed(s) % 2000 < 20) Marlin::send("M9100");
        }
        if (!started) logf("remote: the print didn't start (try %d)", attempt + 1);
    }
    if (!started) {
        logf("remote: %s is on the SD card; start it from the printer's screen", name.c_str());
        showOnPrinter("Print on SD: " + name);
    }
    // "printing" is reported from loop() once the printer runs it (a small connection, after this download's)
}

// Is the website's print still going? Done, stopped on the printer, or still running.
void watchJob()
{
    if (!g_job.length()) return;
    if (Marlin::printsDone() != g_doneBefore) return finishJob("done");
    if (printing()) {
        g_seenPrinting = true;
        return;
    }
    // Not printing: stopped from the printer's screen (or never started)
    if (g_seenPrinting || elapsed(g_jobStarted) > START_GRACE) finishJob("canceled", "Stopped on the printer");
}

void poll()
{
    JsonDocument body;
    identify(body);
    JsonObject st = body["state"].to<JsonObject>();
    const Marlin::Info&       info = Marlin::info();
    const Marlin::SdProgress& sd   = Marlin::sdProgress();
    const Marlin::Temps&      t    = Marlin::temps();
    st["printing"] = printing();
    st["paused"]   = info.printing && !sd.printing && elapsed(sd.at) < 6 * SECONDS;
    st["sd"]       = info.sd;
    if (sd.printing && sd.size) st["progress"] = int(uint64_t(sd.pos) * 100 / sd.size);
    if (t.at) {
        st["hotend"] = t.hotend;
        st["bed"]    = t.bed;
    }
    if (g_job.length()) st["job"] = g_job;
    String payload;
    serializeJson(body, payload);

    JsonDocument answer;
    int          status = 0;
    String       error;
    const uint32_t t0 = millis();
    if (!Cloud::postJson(POLL_URL, payload, answer, status, error, g_atStartup ? 0 : SMALL_RX)) {
        logf("remote: no answer (%s)", error.c_str());
        g_state   = State::Error;
        g_backoff = g_backoff ? std::min(g_backoff * 2, BACKOFF_MAX) : 30 * SECONDS;
        return;
    }
    if (status == 401) {
        // The website doesn't know this token (staff reset the printer): ask for a new one
        logf("remote: token refused, starting over");
        settings.remoteToken[0] = 0;
        settingsSave();
        g_state = State::Error;
        return;
    }
    if (status == 404) { // not checked in yet (the check-in at start-up registers it)
        g_state   = State::Waiting;
        g_backoff = 10 * MINUTES;
        return;
    }
    if (status != 200) {
        logf("remote: HTTP %d", status);
        g_state   = State::Error;
        g_backoff = g_backoff ? std::min(g_backoff * 2, BACKOFF_MAX) : 30 * SECONDS;
        return;
    }
    g_backoff = 0;

    const String token = answer["token"] | "";
    if (token.length() && validToken(token)) {
        strlcpy(settings.remoteToken, token.c_str(), sizeof(settings.remoteToken));
        settingsSave();
        logf("remote: got this printer's token");
    }

    const bool   wasLinked = g_linked;
    const String code      = answer["pair_code"] | "";
    g_linked               = answer["linked"] | false;
    if (code.length() == 6) {
        g_linked = false;
        if (code != g_code || elapsed(g_codeShown) > 2 * MINUTES) {
            showOnPrinter("Link code " + code);
            g_codeShown = millis();
        }
        g_code  = code;
        g_state = State::Unlinked;
    } else {
        g_code = "";
        if (g_linked && !wasLinked && g_codeShown) showOnPrinter("Printer linked");
        g_state = g_job.length() ? State::Printing : State::Linked;
    }
    logf("remote: %s in %lu ms (heap %lu)", stateName(), (unsigned long)elapsed(t0), (unsigned long)ESP.getFreeHeap());

    const String cancel = answer["cancel"] | "";
    if (cancel.length() && cancel == g_job) {
        logf("remote: canceled from the website");
        Marlin::send("M524");
        finishJob("canceled");
        g_state = State::Linked;
        return;
    }

    JsonObject job = answer["job"];
    if (!job.isNull() && !g_job.length()) {
        const String id    = job["id"] | "";
        const String url   = job["url"] | "";
        const uint32_t size = job["size"] | 0;
        const String title = job["title"] | "print";
        if (!validId(id) || !url.startsWith("https://")) return;
        if (g_atStartup) {
            runJob(id, url, size, title);
        } else if (int32_t(millis() - g_holdJobsUntil) >= 0) {
            // The download needs the full-size connection: restart (Wi-Fi only, ~15 s; the printer keeps running)
            logf("remote: a print is waiting, restarting to fetch it");
            setBootTasks(BOOT_TASK);
            markPlannedRestart();
            delay(200);
            ESP.restart();
        }
    }
}

} // namespace

void loop()
{
    // After a restart: keep watching the website print that was running (its file name isn't needed for that)
    static bool restored = false;
    if (!restored) {
        restored = true;
        if (validId(settings.remoteJob)) {
            g_job          = settings.remoteJob;
            g_jobFile      = settings.remoteFile;
            g_jobStarted   = millis();
            g_doneBefore   = Marlin::printsDone();
            g_seenPrinting = false;
            g_reported     = false; // say "printing" again once it runs (the last report may not have arrived)
            logf("remote: still watching job %s", g_job.substring(0, 8).c_str());
        }
    }
    watchJob();
    if (int32_t(millis() - g_nextAt) < 0) return;
    g_nextAt = millis() + (g_backoff ? g_backoff : printing() ? POLL_PRINTING : POLL_IDLE);

    // Only with the home Wi-Fi, a printer that speaks the Wi-Fi protocol, and nothing else using the link
    if (!Net::connected() || Net::hotspotOn() || !Marlin::protocol() || Marlin::quiet() || Web::uploading()) return;
    const Updates::State u = Updates::state();
    if (u == Updates::State::Installing || u == Updates::State::Downloading || u == Updates::State::Checking) return;
    if (!settings.serial[0]) {
        g_state = State::Waiting; // not registered with escaloplab.com yet
        return;
    }

    // Not enough memory in one piece for a secure connection: skip this round rather than risk a crash
    if (ESP.getMaxFreeBlockSize() < NEED_BLOCK || ESP.getFreeHeap() < NEED_HEAP) {
        g_state = State::LowMemory;
        if (!g_lowLogged || elapsed(g_lowLogged) > 10 * MINUTES) {
            g_lowLogged = millis();
            logf("remote: not enough memory for HTTPS (heap %lu, block %lu)", (unsigned long)ESP.getFreeHeap(),
                 (unsigned long)ESP.getMaxFreeBlockSize());
        }
        return;
    }

    if (g_pendingJob.length()) {
        const String job = g_pendingJob, event = g_pendingEvent, error = g_pendingError;
        g_pendingJob = "";
        if (!sendEvent(job, event.c_str(), error)) return;
    }
    // The website print runs on the printer: say so (once; a failed send is retried as a pending event)
    if (g_job.length() && !g_reported && printing()) {
        g_reported = true;
        if (!sendEvent(g_job, "printing")) return;
    }
    poll();
}

void atStartup()
{
    g_atStartup = true;
    const String jobBefore = g_job;
    logf("remote: fetching the waiting print (heap %lu)", (unsigned long)ESP.getFreeHeap());
    for (int i = 0; i < 3 && !Marlin::protocol(); ++i) Marlin::requestInfo(2000);
    if (g_pendingJob.length()) {
        const String job = g_pendingJob, event = g_pendingEvent, error = g_pendingError;
        g_pendingJob = "";
        sendEvent(job, event.c_str(), error);
    }
    if (Marlin::protocol()) poll();
    g_atStartup = false;
    // Nothing started (no answer, card missing, printer busy): wait before trying the restart again
    if (g_job == jobBefore) g_holdJobsUntil = millis() + 5 * MINUTES;
    g_nextAt = millis() + POLL_IDLE;
}

const char* stateName()
{
    switch (g_state) {
    case State::Off: return "off";
    case State::Waiting: return "waiting";
    case State::Unlinked: return "unlinked";
    case State::Linked: return "linked";
    case State::Printing: return "printing";
    case State::LowMemory: return "low_memory";
    case State::Error: return "error";
    }
    return "off";
}

String pairCode() { return g_code; }
bool   busy() { return g_busy; }

} // namespace Remote
