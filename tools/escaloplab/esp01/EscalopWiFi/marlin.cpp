#include "marlin.h"

#include "cloud.h"
#include "config.h"
#include "util.h"

namespace Marlin {

namespace {
constexpr size_t   MAX_QUEUE      = 16;
constexpr uint32_t OK_TIMEOUT     = 3 * SECONDS;
constexpr uint32_t BUSY_TIMEOUT   = 60 * SECONDS; // while Marlin says "busy: processing" (homing, heating)

Info       g_info;
Temps      g_temps;
SdProgress g_sd;
bool       g_protocol  = false;
uint32_t   g_lastLine  = 0;
uint32_t   g_lastBoot  = 0;
bool       g_bootWait  = false; // "start" seen, waiting to know whether an INFO follows
uint32_t   g_infoCount = 0;
bool       g_paused    = false;
uint32_t   g_killButtonAt = 0;  // when Marlin reported the KILL button (0 = no)
uint32_t   g_quietUntil = 0;     // a computer is updating the board over USB: say nothing until then

// Link speed: 1000000 (Escaloplab Marlin 1.7+) or 250000 (older firmware, before the Wi-Fi module). Found by
// listening: a speed counts as confirmed once a line Marlin really prints arrives.
uint32_t g_baud          = MARLIN_BAUD;
bool     g_baudConfirmed = false;
uint32_t g_baudSince     = 0;

char   g_line[320];
size_t g_lineLen  = 0;
bool   g_lineLong = false;

std::vector<String> g_queue;
bool                g_waitingOk = false;
uint32_t            g_sentAt    = 0;
uint32_t            g_busyAt    = 0;

// File list capture (M20)
bool                    g_listing     = false;
bool                    g_listDone    = false;
std::vector<FileEntry>* g_listTarget  = nullptr;

std::function<void(const String&, const String&)> g_escHandler;
std::function<void(bool)>                         g_bootHandler;

bool startsWith(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

void parseInfo(const String& args)
{
    String parts[10];
    const int n = splitWords(args, parts, 10);
    bool      boot = false;
    for (int i = 0; i < n; ++i) {
        const int eq = parts[i].indexOf('=');
        if (eq < 0) continue;
        const String key = parts[i].substring(0, eq), value = parts[i].substring(eq + 1);
        if (key == "fw" && isVersion(value)) g_info.fw = value;
        else if (key == "variant" && Cloud::validVariant(value)) g_info.variant = value;
        else if (key == "sd") g_info.sd = value == "1";
        else if (key == "printing") g_info.printing = value == "1";
        else if (key == "busy") g_info.busy = value == "1";
        else if (key == "boot") boot = value == "1";
    }
    g_info.valid = true;
    g_info.at    = millis();
    g_protocol   = true;
    ++g_infoCount;
    if (boot) {
        g_bootWait = false;
        g_lastBoot = millis();
        logf("marlin: started, firmware %s", g_info.fw.c_str());
        if (g_bootHandler) g_bootHandler(true);
    }
}

// "T:210.12 /210.00 B:60.00 /60.00 @:127 B@:127", possibly after "ok "
void parseTemps(const char* s)
{
    const char* t = strstr(s, "T:");
    const char* b = strstr(s, "B:");
    if (!t) return;
    g_temps.hotend = strtof(t + 2, nullptr);
    if (const char* slash = strchr(t, '/')) g_temps.hotendTarget = strtof(slash + 1, nullptr);
    if (b) {
        g_temps.bed = strtof(b + 2, nullptr);
        if (const char* slash = strchr(b, '/')) g_temps.bedTarget = strtof(slash + 1, nullptr);
    }
    g_temps.at = millis();
}

// "Escaloplab-1.6" inside M115's FIRMWARE_NAME, for firmware older than the Wi-Fi protocol
void parseM115(const char* s)
{
    const char* v = strstr(s, "Escaloplab-");
    if (!v) return;
    v += strlen("Escaloplab-");
    String version;
    while (*v && (isDigit(*v) || *v == '.')) version += *v++;
    if (isVersion(version) && !g_protocol) {
        g_info.fw = version;
        logf("marlin: firmware %s (no Wi-Fi protocol)", version.c_str());
    }
}

void parseFileLine(const char* s)
{
    if (!g_listTarget) return;
    const String line(s);
    const int sp1 = line.indexOf(' ');
    FileEntry e;
    e.name = sp1 < 0 ? line : line.substring(0, sp1);
    if (e.name.endsWith("/")) return; // a folder
    if (sp1 >= 0) {
        const int sp2 = line.indexOf(' ', sp1 + 1);
        const String size = sp2 < 0 ? line.substring(sp1 + 1) : line.substring(sp1 + 1, sp2);
        e.size = strtoul(size.c_str(), nullptr, 10);
        if (sp2 >= 0) e.longName = line.substring(sp2 + 1);
    }
    e.name.toUpperCase();
    if (g_listTarget->size() < 200) g_listTarget->push_back(e);
}

// True for lines Marlin prints (so the link speed is right); false for noise
bool knownLine(const char* s)
{
    static const char* const prefixes[] = {"ok", "//", "echo:", "T:", " T:", "SD ", "Not SD", "Done printing", "Begin file list",
                                           "End file list", "start", "FIRMWARE_NAME", "Cap:", "Error:", "Resend:", "File ",
                                           "Writing to file", "Done saving", "Now fresh file", "busy:"};
    for (const char* p : prefixes)
        if (startsWith(s, p)) return true;
    return false;
}

void handleLine(const char* s)
{
    if (!g_listing && !knownLine(s)) return;
    g_lastLine      = millis();
    g_baudConfirmed = true;
    if (g_listing) {
        if (startsWith(s, "End file list")) {
            g_listing  = false;
            g_listDone = true;
        } else {
            parseFileLine(s);
        }
        return;
    }
    if (startsWith(s, "ok")) {
        g_waitingOk = false;
        if (strstr(s, "T:")) parseTemps(s);
        return;
    }
    if (startsWith(s, "//ESC:")) {
        const String rest(s + 6);
        const int    sp   = rest.indexOf(' ');
        const String cmd  = sp < 0 ? rest : rest.substring(0, sp);
        const String args = sp < 0 ? String() : rest.substring(sp + 1);
        if (cmd == "INFO") parseInfo(args);
        else if (g_escHandler) g_escHandler(cmd, args);
        return;
    }
    if (startsWith(s, "T:") || startsWith(s, " T:")) {
        parseTemps(s);
        return;
    }
    if (startsWith(s, "SD printing byte ")) {
        const char* p = s + strlen("SD printing byte ");
        g_sd.pos      = strtoul(p, nullptr, 10);
        if (const char* slash = strchr(p, '/')) g_sd.size = strtoul(slash + 1, nullptr, 10);
        g_sd.printing = true;
        g_sd.at       = millis();
        return;
    }
    if (startsWith(s, "Not SD printing") || startsWith(s, "Done printing file")) {
        g_sd.printing = false;
        g_sd.at       = millis();
        return;
    }
    if (startsWith(s, "echo:busy:")) {
        g_busyAt = millis();
        return;
    }
    if (startsWith(s, "Begin file list")) {
        g_listing = true;
        return;
    }
    if (strcmp(s, "start") == 0) {
        // Marlin restarted (reset, USB connect or a firmware update). The protocol firmware follows with an INFO.
        g_info       = Info();
        g_protocol   = false;
        g_bootWait   = true;
        g_lastBoot   = millis();
        g_waitingOk  = false;
        g_queue.clear();
        return;
    }
    if (strstr(s, "FIRMWARE_NAME:")) {
        parseM115(s);
        return;
    }
    if (strstr(s, "Unknown command") && strstr(s, "M9100")) {
        g_protocol = false;
        return;
    }
    if (startsWith(s, "Error:")) {
        logf("marlin: %s", s);
        if (strstr(s, "KILL caused by KILL button")) g_killButtonAt = millis();
    }
}
} // namespace

bool takeKillButton()
{
    if (!g_killButtonAt || elapsed(g_killButtonAt) < SECONDS) return false;
    g_killButtonAt = 0;
    return true;
}

bool quiet() { return g_quietUntil && int32_t(millis() - g_quietUntil) < 0; }

const Info&       info() { return g_info; }
const Temps&      temps() { return g_temps; }
const SdProgress& sdProgress() { return g_sd; }
bool              protocol() { return g_protocol; }
bool              alive() { return g_lastLine != 0 && elapsed(g_lastLine) < 30 * SECONDS; }
uint32_t          lastBoot() { return g_lastBoot; }

void onEsc(std::function<void(const String&, const String&)> handler) { g_escHandler = handler; }
void onBoot(std::function<void(bool)> handler) { g_bootHandler = handler; }

void begin()
{
    Serial.setRxBufferSize(512);
    Serial.begin(MARLIN_BAUD);
    Serial.setDebugOutput(false);
    g_baudSince = millis();
}

void relink()
{
    g_baud          = MARLIN_BAUD;
    g_baudConfirmed = false;
    g_baudSince     = millis();
    g_lastLine      = 0;
}

void poll()
{
    if (g_paused) return;
    while (Serial.available()) {
        const char c = char(Serial.read());
        // Control bytes (every bootloader answer starts with 0x1B) mean the board's bootloader is talking to a
        // computer over USB (Escalop Workshop or avrdude installing firmware). Anything sent now would garble
        // that update, so keep quiet a while. (Bytes >= 0x80 are fine: long file names can be UTF-8.)
        // (Only once the speed is right: at the wrong speed everything looks like control bytes.)
        if (g_baudConfirmed && uint8_t(c) < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            if (!quiet()) logf("marlin: USB firmware update going on, keeping quiet");
            g_quietUntil = millis() + 20 * SECONDS;
            g_queue.clear();
            g_waitingOk = false;
            g_lineLen   = 0;
            g_lineLong  = true; // drop the rest of this line
            continue;
        }
        if (c == '\n') {
            g_line[g_lineLen] = 0;
            if (!g_lineLong && g_lineLen > 0) handleLine(g_line);
            g_lineLen  = 0;
            g_lineLong = false;
        } else if (c != '\r') {
            if (g_lineLen < sizeof(g_line) - 1) g_line[g_lineLen++] = c;
            else g_lineLong = true;
        }
    }

    // Find the link speed: nothing understandable for 12 s, try the other one
    if (!g_baudConfirmed && elapsed(g_baudSince) > 12 * SECONDS) {
        g_baud      = g_baud == MARLIN_BAUD ? 250000 : MARLIN_BAUD;
        g_baudSince = millis();
        Serial.updateBaudRate(g_baud);
        logf("marlin: trying %lu baud", (unsigned long)g_baud);
    }
    // Silent for a minute although it's asked every 10 s: the firmware (and maybe its speed) may have changed
    if (g_baudConfirmed && g_lastLine && elapsed(g_lastLine) > MINUTES && !quiet()) {
        g_baudConfirmed = false;
        g_baudSince     = millis();
    }

    // Old firmware: no INFO came after "start". Ask for its version once.
    if (g_bootWait && elapsed(g_lastBoot) > 5 * SECONDS) {
        g_bootWait = false;
        logf("marlin: started without the Wi-Fi protocol");
        send("M115");
        if (g_bootHandler) g_bootHandler(false);
    }

    if (g_waitingOk) {
        const uint32_t limit = elapsed(g_busyAt) < 5 * SECONDS ? BUSY_TIMEOUT : OK_TIMEOUT;
        if (elapsed(g_sentAt) > limit) g_waitingOk = false; // never stall the queue
    }
    if (!g_waitingOk && !g_queue.empty() && !quiet()) {
        const String line = g_queue.front();
        g_queue.erase(g_queue.begin());
        Serial.print(line);
        Serial.print('\n');
        g_waitingOk = true;
        g_sentAt    = millis();
    }
}

void send(const String& line)
{
    if (quiet()) return;
    if (line.length() == 0 || line.length() > 95 || line.indexOf('\n') >= 0 || line.indexOf('\r') >= 0) return;
    if (g_queue.size() >= MAX_QUEUE) {
        logf("marlin: queue full, dropped %s", line.c_str());
        return;
    }
    g_queue.push_back(line);
}

bool requestInfo(uint32_t timeoutMs)
{
    const uint32_t before = g_infoCount;
    send("M9100");
    const uint32_t start = millis();
    while (elapsed(start) < timeoutMs) {
        poll();
        if (g_infoCount != before) return true;
        yield();
    }
    return false;
}

bool drain(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    while (elapsed(start) < timeoutMs) {
        poll();
        if (g_queue.empty() && !g_waitingOk) return true;
        yield();
    }
    return false;
}

bool listFiles(std::vector<FileEntry>& files, uint32_t timeoutMs)
{
    files.clear();
    g_listTarget = &files;
    g_listDone   = false;
    send("M20 L");
    const uint32_t start = millis();
    while (elapsed(start) < timeoutMs && !g_listDone) {
        poll();
        yield();
    }
    g_listTarget = nullptr;
    g_listing    = false;
    return g_listDone;
}

void pause()
{
    g_paused = true;
}

void resume()
{
    g_paused   = false;
    g_lineLen  = 0;
    g_lineLong = false;
    g_waitingOk = false;
}

} // namespace Marlin
