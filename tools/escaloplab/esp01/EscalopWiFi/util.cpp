#include "util.h"

#include <stdarg.h>
#include <user_interface.h> // rst_info, REASON_*

namespace {
constexpr size_t LOG_SIZE = 1024;
char   g_log[LOG_SIZE];
size_t g_log_head = 0;   // next write position
bool   g_log_full = false;

// The newest log text also goes to RTC memory, which survives a crash restart (not a power cut), so the log
// after a crash can show what happened just before it
struct Crumbs
{
    uint32_t magic;
    uint32_t inTls;      // a secure connection was open
    char     tail[248];  // the newest log text, oldest first
};
constexpr uint32_t CRUMBS_MAGIC = 0x45534352; // "ESCR"
constexpr uint32_t PLANNED      = 2;          // inTls value: the module restarted itself on purpose
Crumbs             g_crumbs;

// RTC user memory blocks 0-31 (128 bytes) hold the bootloader's "install the update" command (eboot): never
// write there, or updates silently don't install
constexpr uint32_t CRUMBS_BLOCK = 32;
static_assert(CRUMBS_BLOCK * 4 + sizeof(Crumbs) <= 512, "RTC user memory is 512 bytes");

void crumbsSave() { ESP.rtcUserMemoryWrite(CRUMBS_BLOCK, reinterpret_cast<uint32_t*>(&g_crumbs), sizeof(g_crumbs)); }

void logPut(char c)
{
    memmove(g_crumbs.tail, g_crumbs.tail + 1, sizeof(g_crumbs.tail) - 1);
    g_crumbs.tail[sizeof(g_crumbs.tail) - 1] = c;
    g_log[g_log_head++] = c;
    if (g_log_head == LOG_SIZE) {
        g_log_head = 0;
        g_log_full = true;
    }
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
} // namespace

void logf_P(PGM_P fmt, ...)
{
    char    line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf_P(line, sizeof(line), fmt, args);
    va_end(args);
    char stamp[16];
    snprintf(stamp, sizeof(stamp), "%lu ", (unsigned long)(millis() / 1000));
    for (const char* p = stamp; *p; ++p) logPut(*p);
    for (const char* p = line; *p; ++p) logPut(*p);
    logPut('\n');
    crumbsSave();
}

bool crashMemoryBegin()
{
    Crumbs before;
    const bool valid =
        ESP.rtcUserMemoryRead(CRUMBS_BLOCK, reinterpret_cast<uint32_t*>(&before), sizeof(before)) && before.magic == CRUMBS_MAGIC;
    const uint32_t reason  = ESP.getResetInfoPtr()->reason;
    // Any restart except power-on and the reset pin: also software restarts, which is what a failed allocation
    // (abort) or a panic looks like
    const bool     crashed = reason == REASON_EXCEPTION_RST || reason == REASON_SOFT_WDT_RST || reason == REASON_WDT_RST ||
                         (reason == REASON_SOFT_RESTART && !(valid && before.inTls == PLANNED));
    memset(&g_crumbs, 0, sizeof(g_crumbs));
    g_crumbs.magic = CRUMBS_MAGIC;
    if (valid && crashed) {
        logf("restarted after a problem: %s", ESP.getResetInfo().c_str());
        before.tail[sizeof(before.tail) - 1] = 0;
        const char* text = before.tail;
        while (*text == 0 && text < before.tail + sizeof(before.tail) - 1) ++text; // the buffer fills from the end
        logf("last log lines before it:");
        String line;
        for (const char* p = text; *p; ++p) {
            if (*p == '\n') {
                if (line.length()) logf("  | %s", line.c_str());
                line = "";
            } else {
                line += *p;
            }
        }
    }
    const bool inTls = valid && crashed && before.inTls;
    crumbsSave();
    return inTls;
}

namespace {
constexpr uint32_t TASKS_BLOCK = 100; // after the crash memory (blocks 32-95)
constexpr uint32_t TASKS_MAGIC = 0x45535441; // "ESTA"
} // namespace

void setBootTasks(uint32_t tasks)
{
    uint32_t data[2] = {TASKS_MAGIC, tasks};
    ESP.rtcUserMemoryWrite(TASKS_BLOCK, data, sizeof(data));
}

uint32_t takeBootTasks()
{
    uint32_t data[2] = {0, 0};
    ESP.rtcUserMemoryRead(TASKS_BLOCK, data, sizeof(data));
    setBootTasks(0);
    return data[0] == TASKS_MAGIC ? data[1] : 0;
}

void setInTls(bool on)
{
    g_crumbs.inTls = on;
    crumbsSave();
}

void markPlannedRestart()
{
    g_crumbs.inTls = PLANNED;
    crumbsSave();
}

String logText()
{
    String out;
    out.reserve(LOG_SIZE);
    if (g_log_full)
        for (size_t i = g_log_head; i < LOG_SIZE; ++i) out += g_log[i];
    for (size_t i = 0; i < g_log_head; ++i) out += g_log[i];
    // Drop the first, probably cut, line
    if (g_log_full) {
        int nl = out.indexOf('\n');
        if (nl >= 0) out.remove(0, nl + 1);
    }
    return out;
}

String toHex(const String& text)
{
    if (text.length() == 0) return "-";
    static const char digits[] = "0123456789ABCDEF";
    String out;
    out.reserve(text.length() * 2);
    for (size_t i = 0; i < text.length(); ++i) {
        uint8_t b = uint8_t(text[i]);
        out += digits[b >> 4];
        out += digits[b & 0x0F];
    }
    return out;
}

bool fromHex(const String& hex, String& text)
{
    text = "";
    if (hex == "-") return true;
    if (hex.length() % 2 != 0) return false;
    text.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        int hi = hexValue(hex[i]), lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        text += char(hi << 4 | lo);
    }
    return true;
}

bool isVersion(const String& v)
{
    if (v.length() == 0 || v.length() > 15) return false;
    if (!isDigit(v[0]) || !isDigit(v[v.length() - 1])) return false;
    for (size_t i = 0; i < v.length(); ++i) {
        if (isDigit(v[i])) continue;
        if (v[i] == '.' && v[i - 1] != '.') continue;
        return false;
    }
    return true;
}

int compareVersions(const String& a, const String& b)
{
    size_t ia = 0, ib = 0;
    while (ia < a.length() || ib < b.length()) {
        long na = 0, nb = 0;
        while (ia < a.length() && a[ia] != '.') na = na * 10 + (a[ia++] - '0');
        while (ib < b.length() && b[ib] != '.') nb = nb * 10 + (b[ib++] - '0');
        if (na != nb) return na < nb ? -1 : 1;
        ++ia;
        ++ib;
    }
    return 0;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *data++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint16_t crc16Update(uint16_t crc, const uint8_t* data, size_t len)
{
    while (len--) {
        crc ^= uint16_t(*data++) << 8;
        for (int k = 0; k < 8; ++k) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

int splitWords(const String& line, String* parts, int max)
{
    int    count = 0;
    size_t start = 0;
    while (start <= line.length() && count < max) {
        int sp = line.indexOf(' ', start);
        if (sp < 0) sp = line.length();
        if (size_t(sp) > start) parts[count++] = line.substring(start, sp);
        start = sp + 1;
    }
    return count;
}
