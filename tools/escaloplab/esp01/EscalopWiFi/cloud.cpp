#include "cloud.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <algorithm>
#include <memory>
#include <sys/time.h>
#include <time.h>
#include <vector>

#include "certs.h"
#include "config.h"
#include "settings.h"
#include "util.h"

namespace Cloud {

namespace {

constexpr time_t VALID_TIME = 1735689600; // 2025-01-01: anything earlier means the clock isn't set

bool g_clockStarted = false;

// The date this was compiled: the last-resort clock (certificates still check out for a while)
time_t buildTime()
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char*       date     = __DATE__; // "Nov 20 2026"
    struct tm         t        = {};
    t.tm_mon  = int((strstr(months, String(date).substring(0, 3).c_str()) - months) / 3);
    t.tm_mday = atoi(date + 4);
    t.tm_year = atoi(date + 7) - 1900;
    t.tm_hour = 12;
    return mktime(&t);
}

// The clock from the "Date:" header of a plain HTTP answer (when time servers are blocked)
bool timeFromHttp()
{
    WiFiClient client;
    client.setTimeout(5000);
    if (!client.connect("escaloplab.com", 80)) return false;
    client.print(F("HEAD / HTTP/1.0\r\nHost: escaloplab.com\r\nUser-Agent: " USER_AGENT "\r\n\r\n"));
    const uint32_t start = millis();
    while (client.connected() && elapsed(start) < 5000) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break; // end of the headers
        if (!line.startsWith("Date: ") && !line.startsWith("date: ")) continue;
        // "Date: Sat, 21 Nov 2026 19:01:02 GMT"
        static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        struct tm         t        = {};
        char              mon[4]   = "";
        if (sscanf(line.c_str() + 6, "%*3s, %d %3s %d %d:%d:%d", &t.tm_mday, mon, &t.tm_year, &t.tm_hour, &t.tm_min,
                   &t.tm_sec) != 6)
            return false;
        const char* m = strstr(months, mon);
        if (!m) return false;
        t.tm_mon = int(m - months) / 3;
        t.tm_year -= 1900;
        const timeval tv = {mktime(&t), 0}; // the clock runs on UTC (configTime(0, 0, ...))
        settimeofday(&tv, nullptr);
        logf("cloud: clock set from escaloplab.com");
        return time(nullptr) >= VALID_TIME;
    }
    return false;
}

// Which root certificate a host needs: GitHub's own sites use Sectigo/USERTrust, Supabase (print files for remote
// printing) Google Trust Services, everything else (release downloads on githubusercontent.com, escaloplab.com)
// Let's Encrypt. One root per connection saves memory.
const char* rootFor(const String& url)
{
    const int start = url.indexOf("://") + 3;
    int       end   = url.indexOf('/', start);
    if (end < 0) end = url.length();
    const String host = url.substring(start, end);
    if (host == "github.com" || host.endsWith(".github.com")) return ROOT_GITHUB;
    if (host.endsWith(".supabase.co")) return ROOT_GTS;
    return ROOT_LETSENCRYPT;
}

// A TLS client that trusts one root certificate (certs.h). Made per connection so its memory is freed after.
struct Secure
{
    BearSSL::X509List         anchors;
    BearSSL::WiFiClientSecure client;

    explicit Secure(const char* root) : anchors(root)
    {
        client.setTrustAnchors(&anchors);
        const time_t now = time(nullptr);
        client.setX509Time(now >= VALID_TIME ? now : buildTime());
        client.setTimeout(15000);
    }
};

// A secure connection needs ~25 KB, with one 16.7 KB block for its receive buffer: more than the ESP-01 has
// once the web server and the rest run. So all internet work happens right after a (re)start, before they
// start (see EscalopWiFi.ino, runInternetTasks).
uint32_t g_pausedUntil = 0; // after a crash during a secure connection: no internet for a while

// One secure connection, marked open for the crash memory
struct Session
{
    std::unique_ptr<Secure> tls;
    explicit Session(const String& url)
    {
        setInTls(true);
        tls.reset(new Secure(rootFor(url)));
        logf("tls: %s, heap %lu, block %lu", url.substring(8, 40).c_str(), (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMaxFreeBlockSize());
    }
    ~Session()
    {
        tls.reset();
        setInTls(false);
    }
};

bool paused(String& error)
{
    if (g_pausedUntil && int32_t(millis() - g_pausedUntil) < 0) {
        error = "internet paused after a restart";
        return true;
    }
    return false;
}

bool ready(String& error)
{
    if (WiFi.status() != WL_CONNECTED) {
        error = "no internet";
        return false;
    }
    if (paused(error)) return false;
    ensureTime();
    return true;
}

void prepare(HTTPClient& http)
{
    http.setUserAgent(USER_AGENT);
    http.useHTTP10(true); // no chunked encoding, so the body can be streamed straight into the JSON parser
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
}

// Why a request failed; for connection problems also BearSSL's reason and the memory, in the log
String failure(Secure& tls, int status, HTTPClient& http)
{
    if (status >= 0) return "HTTP " + String(status);
    char      ssl[80] = "";
    const int code    = tls.client.getLastSSLError(ssl, sizeof(ssl));
    logf("tls: %s, ssl %d %s, heap %lu, block %lu", http.errorToString(status).c_str(), code, ssl,
         (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxFreeBlockSize());
    return http.errorToString(status);
}

bool getJson(const String& url, JsonDocument& doc, const JsonDocument& filter, int& status, String& error)
{
    if (!ready(error)) return false;
    Session    session(url);
    HTTPClient http;
    prepare(http);
    if (!http.begin(session.tls->client, url)) {
        error = "bad URL";
        return false;
    }
    http.addHeader("Accept", "application/vnd.github+json");
    status = http.GET();
    if (status != 200) {
        error = failure(*session.tls, status, http);
        http.end();
        return status == 404; // "not found" is an answer, not a network problem
    }
    const DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        error = String("bad answer: ") + err.c_str();
        return false;
    }
    return true;
}

bool endsWithIgnoreCase(const String& s, const char* suffix)
{
    String a = s, b = suffix;
    a.toLowerCase();
    b.toLowerCase();
    return a.endsWith(b);
}

} // namespace

void begin(bool crashedInTls)
{
    if (crashedInTls) {
        g_pausedUntil = millis() + 30 * MINUTES;
        logf("cloud: the last restart came during a secure connection; internet paused for 30 min");
    }
}

void startClock()
{
    if (g_clockStarted) return;
    g_clockStarted = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

bool ensureTime()
{
    if (time(nullptr) >= VALID_TIME) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    startClock(); // only once: starting it again restarts the wait for an answer
    const uint32_t start = millis();
    while (elapsed(start) < 10000) {
        if (time(nullptr) >= VALID_TIME) return true;
        delay(100);
    }
    if (timeFromHttp()) return true;
    logf("cloud: no time from the internet, using the build date");
    return false;
}

bool validVariant(const String& name)
{
    if (name.length() > 16) return false;
    for (size_t i = 0; i < name.length(); ++i)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'z') || name[i] == '-')) return false;
    return true;
}

bool latestRelease(const char* tagPrefix, const std::function<int(const String&, const String&)>& rank, String& version,
                   String& url, String& assetName, String& error)
{
    version   = "";
    url       = "";
    assetName = "";

    // 1. All tags with the prefix (a short list, unlike the releases of the whole repository)
    std::vector<String> versions;
    {
        JsonDocument filter;
        filter[0]["ref"] = true;
        JsonDocument doc;
        int          status = 0;
        if (!getJson(String("https://api.github.com/repos/" GITHUB_REPO "/git/matching-refs/tags/") + tagPrefix, doc,
                     filter, status, error))
            return false;
        if (status != 200) return true; // no such tags yet
        const String head = String("refs/tags/") + tagPrefix;
        for (JsonObject ref : doc.as<JsonArray>()) {
            const String name = ref["ref"] | "";
            if (!name.startsWith(head)) continue;
            const String v = name.substring(head.length());
            if (isVersion(v)) versions.push_back(v);
        }
    }
    std::sort(versions.begin(), versions.end(), [](const String& a, const String& b) { return compareVersions(a, b) > 0; });

    // 2. The newest that is a published release with the right file (a tag alone isn't a release)
    for (size_t i = 0; i < versions.size() && i < 4; ++i) {
        JsonDocument filter;
        filter["draft"]                             = true;
        filter["prerelease"]                        = true;
        filter["assets"][0]["name"]                 = true;
        filter["assets"][0]["browser_download_url"] = true;
        JsonDocument doc;
        int          status = 0;
        if (!getJson(String("https://api.github.com/repos/" GITHUB_REPO "/releases/tags/") + tagPrefix + versions[i], doc,
                     filter, status, error))
            return false;
        if (status != 200 || (doc["draft"] | false) || (doc["prerelease"] | false)) continue;
        int best = -1;
        for (JsonObject asset : doc["assets"].as<JsonArray>()) {
            const String name = asset["name"] | "";
            const int    r    = rank(name, versions[i]);
            if (r < 0 || (best >= 0 && r >= best)) continue;
            best      = r;
            assetName = name;
            url       = asset["browser_download_url"] | "";
        }
        if (best < 0) {
            logf("cloud: %s%s has no file for this printer", tagPrefix, versions[i].c_str());
            continue;
        }
        version = versions[i];
        logf("cloud: newest %s%s: %s", tagPrefix, version.c_str(), assetName.c_str());
        return url.startsWith("https://");
    }
    return true;
}

bool download(const String& urlIn, const std::function<bool(const uint8_t*, size_t)>& sink,
              const std::function<void(uint32_t, int32_t)>& progress, String& error)
{
    // GitHub answers a download with a redirect to its file servers (another host, another root certificate),
    // so redirects are followed here, one connection each
    String url = urlIn;
    for (int hop = 0; hop < 4; ++hop) {
        if (!ready(error)) return false;
        Session    session(url);
        HTTPClient http;
        prepare(http);
        const char* headers[] = {"Location"};
        http.collectHeaders(headers, 1);
        if (!http.begin(session.tls->client, url)) {
            error = "bad URL";
            return false;
        }
        const int status = http.GET();
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            url = http.header("Location");
            http.end();
            if (!url.startsWith("https://")) {
                error = "bad redirect";
                return false;
            }
            continue;
        }
        if (status != 200) {
            error = failure(*session.tls, status, http);
            http.end();
            return false;
        }
        const int32_t total  = http.getSize();
        WiFiClient*   stream = http.getStreamPtr();
        uint8_t       buf[512];
        uint32_t      received = 0, lastData = millis();
        while (http.connected() && (total < 0 || received < uint32_t(total))) {
            const size_t avail = stream->available();
            if (avail == 0) {
                if (elapsed(lastData) > 20000) {
                    error = "download stalled";
                    http.end();
                    return false;
                }
                delay(1);
                continue;
            }
            const int n = stream->readBytes(buf, std::min(avail, sizeof(buf)));
            if (n <= 0) continue;
            lastData = millis();
            if (!sink(buf, size_t(n))) {
                error = "rejected";
                http.end();
                return false;
            }
            received += n;
            progress(received, total);
        }
        http.end();
        if (total >= 0 && received != uint32_t(total)) {
            error = "download incomplete";
            return false;
        }
        logf("cloud: downloaded %lu bytes", (unsigned long)received);
        return true;
    }
    error = "too many redirects";
    return false;
}

bool postJson(const String& url, const String& body, JsonDocument& answer, int& status, String& error)
{
    status = 0;
    if (!ready(error)) return false;
    Session    session(url);
    HTTPClient http;
    prepare(http);
    if (!http.begin(session.tls->client, url)) {
        error = "bad URL";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Escalop-Device", "v1");
    status = http.POST(body);
    if (status < 0) {
        error = failure(*session.tls, status, http);
        http.end();
        return false;
    }
    const DeserializationError err = deserializeJson(answer, http.getStream());
    http.end();
    if (err) {
        error = String("bad answer: ") + err.c_str();
        return status != 200; // an error page without JSON is still an answer
    }
    return true;
}

bool checkin(const String& printerFirmware, Checkin& result, String& error)
{
    if (!ready(error)) return false;
    String payload;
    {
        JsonDocument body;
        String       mac = WiFi.macAddress();
        mac.toLowerCase();
        char chip[8];
        snprintf(chip, sizeof(chip), "%06x", ESP.getChipId());
        body["mac"]      = mac;
        body["chip"]     = chip;
        body["model"]    = PRINTER_MODEL;
        body["serial"]   = settings.serial;
        body["wifi"]     = WIFI_VERSION;
        body["firmware"] = printerFirmware;
        serializeJson(body, payload);
    }

    Session    session(CHECKIN_URL);
    HTTPClient http;
    prepare(http); // the API must answer directly, without redirects
    if (!http.begin(session.tls->client, CHECKIN_URL)) {
        error = "bad URL";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Escalop-Device", "v1");
    const int status = http.POST(payload);
    if (status != 200) {
        error = failure(*session.tls, status, http);
        http.end();
        return false;
    }
    JsonDocument               answer;
    const DeserializationError err = deserializeJson(answer, http.getStream());
    http.end();
    if (err) {
        error = "bad answer";
        return false;
    }
    const String s = answer["serial"] | "";
    // Only what the website promises: letters, digits and dashes, 1-12 characters
    if (s.length() == 0 || s.length() > 12) {
        error = "no serial in the answer";
        return false;
    }
    for (size_t i = 0; i < s.length(); ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '-')) {
            error = "bad serial in the answer";
            return false;
        }
    result.serial = s;

    // The firmware variant staff chose for this printer (optional: older website versions don't send it)
    if (answer["firmware_variant"].is<const char*>()) {
        const String v = answer["firmware_variant"].as<const char*>();
        if (validVariant(v)) {
            result.variantSet = true;
            result.variant    = v;
        } else {
            logf("checkin: ignored firmware variant \"%s\"", v.c_str());
        }
    }
    return true;
}

} // namespace Cloud
