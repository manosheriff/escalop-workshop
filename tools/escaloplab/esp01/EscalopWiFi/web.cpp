#include "web.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

#include "config.h"
#include "flasher.h"
#include "marlin.h"
#include "net.h"
#include "settings.h"
#include "transfer.h"
#include "updates.h"
#include "util.h"
#include "web_ui.h"

namespace Web {

namespace {
ESP8266WebServer server(80);

SdUpload g_upload;
String   g_uploadError;
String   g_uploadName;
String   g_uploadLong;
uint32_t g_uploadStart = 0;

String   g_joinResult;
String   g_pendingSsid, g_pendingPass;
uint32_t g_joinAt    = 0;
uint32_t g_forgetAt  = 0;
uint32_t g_restartAt = 0;
uint32_t g_lastScan  = 0;
String   g_lastFile; // what we last started, for the status page
bool     g_moduleUpdateOk = false;
bool     g_installing     = false; // printer firmware being installed: only status requests are served

// Requests that would talk to the printer or change things wait while firmware is installed
bool installingRefuse()
{
    if (!g_installing) return false;
    server.sendHeader("Cache-Control", "no-store");
    server.send(503, "text/plain", "Printer firmware is being installed. Try again in a minute.");
    return true;
}

bool due(uint32_t at) { return at && int32_t(millis() - at) >= 0; }

// Requests that came in through the setup hotspot only get the setup page
bool fromHotspot() { return Net::hotspotOn() && server.client().localIP() == WiFi.softAPIP(); }

void redirectToSetup()
{
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

// Guard for everything that controls the printer
bool homeNetworkOnly()
{
    if (!fromHotspot()) return true;
    redirectToSetup();
    return false;
}

void sendJson(int code, const JsonDocument& doc)
{
    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", out);
}

void sendError(int code, const String& message)
{
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "text/plain", message);
}

// An 8.3 name from the SD card listing, safe to put in a G-code line
bool validSdName(const String& name)
{
    const int dot = name.indexOf('.');
    if (dot < 1 || dot > 8 || name.length() - dot - 1 < 1 || name.length() - dot - 1 > 3) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        if (int(i) == dot) continue;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '~' || c == '-')) return false;
    }
    return true;
}

bool isGcodeName(String name)
{
    name.toLowerCase();
    return name.endsWith(".gcode") || name.endsWith(".gco") || name.endsWith(".g");
}

bool printing() { return Marlin::info().printing || Marlin::sdProgress().printing; }

// ---- pages ------------------------------------------------------------------------------------------------------

void handleRoot()
{
    if (fromHotspot()) {
        if (server.hostHeader() != WiFi.softAPIP().toString()) return redirectToSetup();
        server.send_P(200, "text/html", PAGE_SETUP);
        return;
    }
    server.send_P(200, "text/html", PAGE_MAIN);
}

void handleCss()
{
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "text/css", APP_CSS);
}

void handleNotFound()
{
    if (fromHotspot()) return redirectToSetup(); // captive portal: every address leads to the setup page
    sendError(404, "Not found");
}

// ---- setup (hotspot and home network) -----------------------------------------------------------------------------

void handleScan()
{
    if (installingRefuse()) return;
    if (!Net::scanning() && (Net::networks().empty() || elapsed(g_lastScan) > 15 * SECONDS)) {
        Net::scan();
        g_lastScan = millis();
    }
    JsonDocument doc;
    doc["scanning"] = Net::scanning();
    JsonArray list  = doc["networks"].to<JsonArray>();
    for (const Net::Network& n : Net::networks()) {
        JsonObject o = list.add<JsonObject>();
        o["ssid"]    = n.ssid;
        o["rssi"]    = n.rssi;
        o["secure"]  = n.secure;
    }
    sendJson(200, doc);
}

void handleSetup()
{
    if (installingRefuse()) return;
    const String ssid = server.arg("ssid"), pass = server.arg("pass");
    if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 || (pass.length() > 0 && pass.length() < 8))
        return sendError(400, "Check the network name and password (8 to 64 characters).");
    g_pendingSsid = ssid;
    g_pendingPass = pass;
    g_joinResult  = "joining";
    g_joinAt      = millis() + 300; // answer first: joining may move the hotspot to another channel
    sendError(200, "ok");
}

void handleSetupState()
{
    JsonDocument doc;
    doc["result"] = g_joinResult;
    doc["ssid"]   = Net::ssid();
    doc["ip"]     = Net::ip();
    doc["host"]   = Net::hostName();
    sendJson(200, doc);
}

// ---- the printer --------------------------------------------------------------------------------------------------

void handleStatus()
{
    if (!homeNetworkOnly()) return;
    const Marlin::Info&       info = Marlin::info();
    const Marlin::Temps&      t    = Marlin::temps();
    const Marlin::SdProgress& sd   = Marlin::sdProgress();
    JsonDocument doc;
    JsonObject   p  = doc["printer"].to<JsonObject>();
    p["online"]     = Marlin::alive();
    p["protocol"]   = Marlin::protocol();
    p["fw"]         = info.fw;
    p["sd"]         = info.sd;
    p["printing"]   = printing();
    p["paused"]     = info.printing && !sd.printing && elapsed(sd.at) < 6 * SECONDS;
    p["busy"]       = info.busy;
    p["file"]       = g_lastFile;
    if (sd.printing && sd.size) p["progress"] = int(uint64_t(sd.pos) * 100 / sd.size);
    else p["progress"] = nullptr;
    JsonArray h = p["hotend"].to<JsonArray>();
    h.add(t.at ? t.hotend : 0);
    h.add(t.hotendTarget);
    JsonArray b = p["bed"].to<JsonArray>();
    b.add(t.at ? t.bed : 0);
    b.add(t.bedTarget);

    JsonObject w = doc["wifi"].to<JsonObject>();
    w["ssid"]    = Net::ssid();
    w["ip"]      = Net::ip();
    w["host"]    = Net::hostName();
    w["rssi"]    = Net::rssi();

    JsonObject m = doc["module"].to<JsonObject>();
    m["version"] = WIFI_VERSION;
    m["serial"]  = settings.serial;
    m["heap"]    = ESP.getFreeHeap();
    m["uptime"]  = millis() / 1000;

    JsonObject u   = doc["update"].to<JsonObject>();
    u["state"]     = Updates::stateName();
    u["progress"]  = Updates::progress();
    u["available"] = Updates::availableVersion();
    u["error"]     = Updates::lastError();
    sendJson(200, doc);
}

void handleFiles()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    JsonDocument doc;
    JsonArray    list = doc["files"].to<JsonArray>();
    if (!Marlin::alive()) {
        doc["error"] = "The printer isn't answering";
    } else if (Marlin::protocol() && Marlin::info().valid && !Marlin::info().sd) {
        doc["error"] = "No SD card in the printer";
    } else {
        std::vector<Marlin::FileEntry> files;
        if (!Marlin::listFiles(files)) doc["error"] = "The printer didn't list its files";
        for (const Marlin::FileEntry& f : files) {
            if (!isGcodeName(f.name) && !isGcodeName(f.longName)) continue;
            JsonObject o = list.add<JsonObject>();
            o["name"]    = f.name;
            o["long"]    = f.longName;
            o["size"]    = f.size;
        }
    }
    sendJson(200, doc);
}

void handleJob()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    const String action = server.arg("action");
    String       name   = server.arg("name");
    name.toUpperCase();
    if (!validSdName(name)) return sendError(400, "Unknown file");
    if (printing()) return sendError(409, "The printer is printing");
    if (action == "print") {
        Marlin::send("M23 " + name);
        Marlin::send("M24");
        g_lastFile = name;
    } else if (action == "delete") {
        Marlin::send("M30 " + name);
        Marlin::drain(3000);
    } else {
        return sendError(400, "Unknown action");
    }
    sendError(200, "ok");
}

void handleControl()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    const String a = server.arg("action");
    if (a == "pause") Marlin::send("M25");
    else if (a == "resume") Marlin::send("M24");
    else if (a == "cancel") Marlin::send("M524");
    else if (printing()) return sendError(409, "Not while printing");
    else if (a == "home") Marlin::send("G28");
    else if (a == "preheat_pla") { Marlin::send("M104 S205"); Marlin::send("M140 S60"); }
    else if (a == "preheat_petg") { Marlin::send("M104 S235"); Marlin::send("M140 S80"); }
    else if (a == "cooldown") { Marlin::send("M104 S0"); Marlin::send("M140 S0"); }
    else if (a == "restart") Flasher::resetBoard(); // also a quick test of the reset wire
    else return sendError(400, "Unknown action");
    sendError(200, "ok");
}

void handleUpdateCheck()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    Updates::checkNow(false);
    sendError(200, "ok");
}

void handleUpdateInstall()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    String why;
    if (!Updates::install(why)) return sendError(409, why);
    sendError(200, "ok");
}

void handleForget()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    g_forgetAt = millis() + 500;
    sendError(200, "ok");
}

void handleLog()
{
    if (!homeNetworkOnly()) return;
    server.send(200, "text/plain", logText());
}

// ---- uploads (OctoPrint API: POST /api/files/local, fields "file", "print") ------------------------------------------

void handleVersion()
{
    if (!homeNetworkOnly()) return;
    JsonDocument doc;
    doc["api"]    = "0.1";
    doc["server"] = "1.10.0";
    doc["text"]   = "OctoPrint 1.10.0 (Escaloplab v80 Wi-Fi " WIFI_VERSION ")"; // the slicer checks "OctoPrint"
    sendJson(200, doc);
}

void handleUploadChunk()
{
    HTTPUpload& up = server.upload();
    String      err;
    switch (up.status) {
    case UPLOAD_FILE_START: {
        g_uploadError = g_installing ? "Printer firmware is being installed. Try again in a minute." : "";
        g_uploadLong  = up.filename;
        g_uploadName  = sdNameFor(up.filename);
        g_uploadStart = millis();
        if (fromHotspot()) {
            g_uploadError = "Connect the printer to your Wi-Fi first";
            return;
        }
        if (!isGcodeName(up.filename)) {
            g_uploadError = "Only G-code files (.gcode) can be printed";
            return;
        }
        if (!Marlin::protocol() || !Marlin::requestInfo(2500)) {
            g_uploadError = Marlin::alive() ? "Update the printer's firmware to print over Wi-Fi" : "The printer isn't answering";
            return;
        }
        if (!Marlin::info().sd) {
            Marlin::send("M9108 NOSD");
            g_uploadError = "Insert the SD card in the printer, then send the print again";
            return;
        }
        if (Marlin::info().printing) {
            Marlin::send("M9108 PRINTING");
            g_uploadError = "The printer is busy printing";
            return;
        }
        const long length   = server.header("Content-Length").toInt();
        const uint32_t hint = length > 1024 ? uint32_t(length - 512) : 0; // minus the form's own text
        if (!g_upload.begin(hint, g_uploadName, err)) g_uploadError = err;
        break;
    }
    case UPLOAD_FILE_WRITE:
        if (g_uploadError.length() == 0 && g_upload.active() && !g_upload.write(up.buf, up.currentSize, err)) g_uploadError = err;
        break;
    case UPLOAD_FILE_END:
        if (g_uploadError.length() == 0 && g_upload.active() && !g_upload.finish(err)) g_uploadError = err;
        if (g_uploadError.length() == 0) {
            const uint32_t ms = std::max<uint32_t>(1, elapsed(g_uploadStart));
            logf("upload: %lu bytes in %lu ms (%lu KB/s)", (unsigned long)g_upload.sent(), (unsigned long)ms,
                 (unsigned long)(uint64_t(g_upload.sent()) * 1000 / ms / 1024));
        }
        break;
    case UPLOAD_FILE_ABORTED:
        g_upload.abort();
        g_uploadError = "The upload was interrupted";
        break;
    }
}

void handleUploadDone()
{
    if (g_upload.active()) g_upload.abort(); // the request ended without the file's end
    if (g_uploadError.length()) {
        logf("upload: %s", g_uploadError.c_str());
        return sendError(409, g_uploadError);
    }
    if (g_uploadName.length() == 0) return sendError(400, "No file in the request");
    if (server.arg("print") == "true") {
        Marlin::send("M23 " + g_uploadName);
        Marlin::send("M24");
        g_lastFile = g_uploadLong;
    } else {
        Marlin::send("M9108 RECEIVED");
    }
    JsonDocument doc;
    doc["done"]                                = true;
    doc["files"]["local"]["name"]              = g_uploadName;
    doc["files"]["local"]["origin"]            = "local";
    doc["files"]["local"]["refs"]["resource"] = "http://" + Net::ip() + "/api/files/local/" + g_uploadName;
    sendJson(201, doc);
    g_uploadName = "";
}

// ---- the module's own software, by hand ---------------------------------------------------------------------------

void handleModulePage()
{
    if (!homeNetworkOnly()) return;
    server.send_P(200, "text/html", PAGE_MODULE_UPDATE);
}

void handleModuleChunk()
{
    HTTPUpload& up = server.upload();
    if (fromHotspot() || g_installing) return;
    if (up.status == UPLOAD_FILE_START) {
        g_moduleUpdateOk = false;
        settings.stagedValid = 0; // the update uses the staging space
        settingsSave();
        Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        g_moduleUpdateOk = Update.end(true);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end(false);
    }
}

void handleModuleDone()
{
    if (installingRefuse()) return;
    if (!homeNetworkOnly()) return;
    if (!g_moduleUpdateOk) return sendError(500, "Not installed: " + Update.getErrorString());
    logf("web: module software installed by hand");
    sendError(200, "Installed. The Wi-Fi module restarts now; reload this page in 20 seconds.");
    g_restartAt = millis() + 1000;
}
} // namespace

void begin()
{
    server.collectHeaders("Content-Length");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/app.css", HTTP_GET, handleCss);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/setup", HTTP_POST, handleSetup);
    server.on("/api/setup/state", HTTP_GET, handleSetupState);

    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/files", HTTP_GET, handleFiles);
    server.on("/api/job", HTTP_POST, handleJob);
    server.on("/api/control", HTTP_POST, handleControl);
    server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
    server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
    server.on("/api/wifi/forget", HTTP_POST, handleForget);
    server.on("/log", HTTP_GET, handleLog);

    server.on("/api/version", HTTP_GET, handleVersion);
    server.on("/api/files/local", HTTP_POST, handleUploadDone, handleUploadChunk);

    server.on("/update", HTTP_GET, handleModulePage);
    server.on("/update", HTTP_POST, handleModuleDone, handleModuleChunk);

    server.onNotFound(handleNotFound);
    server.begin();
}

void loop()
{
    server.handleClient();
    if (due(g_joinAt)) {
        g_joinAt = 0;
        Net::join(g_pendingSsid, g_pendingPass);
        g_pendingPass = "";
    }
    if (due(g_forgetAt)) {
        g_forgetAt = 0;
        Net::forget();
    }
    if (due(g_restartAt)) {
        markPlannedRestart();
        ESP.restart();
    }
}

bool uploading() { return g_upload.active(); }

void serviceDuringInstall()
{
    g_installing = true;
    server.handleClient();
    g_installing = false;
}

void setJoinResult(const char* result)
{
    if (g_joinResult == "joining") g_joinResult = result; // only the attempt the setup page started
}

} // namespace Web
