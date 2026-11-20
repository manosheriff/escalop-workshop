#include "cloud.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <algorithm>
#include <memory>
#include <time.h>
#include <vector>

#include "certs.h"
#include "config.h"
#include "settings.h"
#include "util.h"

namespace Cloud {

namespace {

constexpr time_t VALID_TIME = 1735689600; // 2025-01-01: anything earlier means the clock isn't set

// The date this was compiled, as a fallback clock when no time server answers (certificates then still work for
// a while). __DATE__ is "Nov 20 2026".
time_t buildTime()
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char*       date     = __DATE__;
    struct tm         t        = {};
    t.tm_mon  = int((strstr(months, String(date).substring(0, 3).c_str()) - months) / 3);
    t.tm_mday = atoi(date + 4);
    t.tm_year = atoi(date + 7) - 1900;
    t.tm_hour = 12;
    return mktime(&t);
}

// A TLS client that trusts the roots in certs.h. Made per request so its memory (about 25 KB) is freed afterwards.
struct Secure
{
    BearSSL::X509List         anchors{TRUST_ANCHORS};
    BearSSL::WiFiClientSecure client;

    Secure()
    {
        client.setTrustAnchors(&anchors);
        const time_t now = time(nullptr);
        client.setX509Time(now >= VALID_TIME ? now : buildTime());
        client.setTimeout(15000);
    }
};

void prepare(HTTPClient& http)
{
    http.setUserAgent(USER_AGENT);
    http.useHTTP10(true); // no chunked encoding, so the body can be streamed straight into the JSON parser
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
}

bool getJson(const String& url, JsonDocument& doc, const JsonDocument& filter, int& status, String& error)
{
    if (WiFi.status() != WL_CONNECTED) {
        error = "no internet";
        return false;
    }
    std::unique_ptr<Secure> tls(new Secure);
    HTTPClient http;
    prepare(http);
    if (!http.begin(tls->client, url)) {
        error = "bad URL";
        return false;
    }
    http.addHeader("Accept", "application/vnd.github+json");
    status = http.GET();
    if (status != 200) {
        error = status < 0 ? http.errorToString(status) : "HTTP " + String(status);
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

bool ensureTime()
{
    if (time(nullptr) >= VALID_TIME) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    const uint32_t start = millis();
    while (elapsed(start) < 8000) {
        if (time(nullptr) >= VALID_TIME) return true;
        delay(100);
    }
    logf("cloud: no time from the internet, using the build date");
    return false;
}

bool latestRelease(const char* tagPrefix, const char* extension, String& version, String& url, String& error)
{
    version = "";
    url     = "";
    ensureTime();

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
        for (JsonObject asset : doc["assets"].as<JsonArray>()) {
            String name = asset["name"] | "";
            String lower = name;
            lower.toLowerCase();
            if (!endsWithIgnoreCase(name, extension) || lower.indexOf("direct") >= 0) continue;
            version = versions[i];
            url     = asset["browser_download_url"] | "";
            logf("cloud: newest %s%s: %s", tagPrefix, version.c_str(), name.c_str());
            return url.startsWith("https://");
        }
    }
    return true;
}

bool download(const String& url, const std::function<bool(const uint8_t*, size_t)>& sink,
              const std::function<void(uint32_t, int32_t)>& progress, String& error)
{
    if (WiFi.status() != WL_CONNECTED) {
        error = "no internet";
        return false;
    }
    ensureTime();
    std::unique_ptr<Secure> tls(new Secure);
    HTTPClient http;
    prepare(http);
    if (!http.begin(tls->client, url)) {
        error = "bad URL";
        return false;
    }
    const int status = http.GET();
    if (status != 200) {
        error = status < 0 ? http.errorToString(status) : "HTTP " + String(status);
        http.end();
        return false;
    }
    const int32_t total = http.getSize();
    WiFiClient*   stream = http.getStreamPtr();
    uint8_t       buf[1024];
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

bool checkin(const String& printerFirmware, String& serial, String& error)
{
    if (WiFi.status() != WL_CONNECTED) {
        error = "no internet";
        return false;
    }
    ensureTime();
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
    String payload;
    serializeJson(body, payload);

    std::unique_ptr<Secure> tls(new Secure);
    HTTPClient http;
    prepare(http);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // the API must answer directly
    if (!http.begin(tls->client, CHECKIN_URL)) {
        error = "bad URL";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Escalop-Device", "v1");
    const int status = http.POST(payload);
    if (status != 200) {
        error = status < 0 ? http.errorToString(status) : "HTTP " + String(status);
        http.end();
        return false;
    }
    JsonDocument answer;
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
    serial = s;
    return true;
}

} // namespace Cloud
