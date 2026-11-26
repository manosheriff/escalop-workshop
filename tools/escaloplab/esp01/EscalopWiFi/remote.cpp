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
// A secure connection needs one ~17 KB block for its receive buffer plus a few KB more (see cloud.cpp)
constexpr uint32_t NEED_BLOCK = 17800;
constexpr uint32_t NEED_HEAP  = 24000;
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
    if (!Cloud::postJson(JOB_URL, payload, answer, status, err) || (status >= 500)) {
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

void rememberJob(const String& id)
{
    strlcpy(settings.remoteJob, id.c_str(), sizeof(settings.remoteJob));
    settingsSave();
}

void finishJob(const char* event, const String& error = String())
{
    if (!g_job.length()) return;
    sendEvent(g_job, event, error);
    g_job = "";
    g_jobFile = "";
    rememberJob("");
}

// Downloads the G-code onto the SD card and starts it
void runJob(const String& id, const String& url, uint32_t size, const String& title)
{
    if (!Marlin::requestInfo(2500)) return; // the printer isn't answering: try again at the next check
    if (!Marlin::info().sd) {
        Marlin::send("M9108 NOSD");
        return; // the job waits until a card is in
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
        sendEvent(id, "failed", error.length() ? error : String("The download failed"));
        return;
    }
    logf("remote: %s on the SD card in %lu s, starting", name.c_str(), (unsigned long)(elapsed(t0) / 1000));
    Marlin::send("M23 " + name);
    Marlin::send("M24");
    g_job          = id;
    g_jobFile      = name;
    g_jobStarted   = millis();
    g_doneBefore   = Marlin::printsDone();
    g_seenPrinting = false;
    rememberJob(id);
    sendEvent(id, "printing");
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
    if (!Cloud::postJson(POLL_URL, payload, answer, status, error)) {
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
        if (validId(id) && url.startsWith("https://")) runJob(id, url, size, title);
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
            g_jobStarted   = millis();
            g_doneBefore   = Marlin::printsDone();
            g_seenPrinting = false;
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
    poll();
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
