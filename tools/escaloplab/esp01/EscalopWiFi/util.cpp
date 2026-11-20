#include "util.h"

#include <stdarg.h>

namespace {
constexpr size_t LOG_SIZE = 1536;
char   g_log[LOG_SIZE];
size_t g_log_head = 0;   // next write position
bool   g_log_full = false;

void logPut(char c)
{
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
