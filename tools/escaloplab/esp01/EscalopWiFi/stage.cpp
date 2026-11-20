#include "stage.h"

#include "config.h"
#include "util.h"

extern "C" uint32_t _FS_start; // end of the program/update space (no file system: the EEPROM sector follows)

namespace Stage {

namespace {
constexpr uint32_t SECTOR = 4096;
constexpr uint32_t AREA   = (AVR_APP_SIZE + SECTOR - 1) / SECTOR * SECTOR;

constexpr uint32_t PAGE = 256;
uint32_t g_page[PAGE / 4]; // 4-byte aligned, as the flash functions need

uint32_t areaEnd() { return (uint32_t(&_FS_start) - 0x40200000u) & ~(SECTOR - 1); }
uint32_t areaStart() { return areaEnd() - AREA; }

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
} // namespace

bool available()
{
    const uint32_t programEnd = (ESP.getSketchSize() + SECTOR - 1) / SECTOR * SECTOR;
    return areaEnd() > AREA && areaStart() >= programEnd;
}

bool erase()
{
    if (!available()) return false;
    for (uint32_t s = areaStart() / SECTOR; s < areaEnd() / SECTOR; ++s) {
        if (!ESP.flashEraseSector(s)) {
            logf("stage: erase failed at sector %lu", (unsigned long)s);
            return false;
        }
        yield();
    }
    return true;
}

bool read(uint32_t offset, uint8_t* out, size_t len)
{
    static uint32_t tmp[64]; // 256 bytes
    while (len > 0) {
        const uint32_t addr    = areaStart() + offset;
        const uint32_t aligned = addr & ~3u;
        const uint32_t skip    = addr - aligned;
        const size_t   n       = std::min<size_t>(len, sizeof(tmp) - skip);
        if (!ESP.flashRead(aligned, tmp, (skip + n + 3) & ~3u)) return false;
        memcpy(out, reinterpret_cast<uint8_t*>(tmp) + skip, n);
        out += n;
        offset += n;
        len -= n;
    }
    return true;
}

uint32_t crc(uint32_t size)
{
    uint8_t  buf[256];
    uint32_t value = 0;
    for (uint32_t off = 0; off < size; off += sizeof(buf)) {
        const size_t n = std::min<uint32_t>(sizeof(buf), size - off);
        if (!read(off, buf, n)) return 0;
        value = crc32Update(value, buf, n);
        yield();
    }
    return value;
}

// ---------------------------------------------------------------------------------------------------------------

void HexWriter::begin()
{
    m_len = 0;
    m_overflow = false;
    m_base = 0;
    m_eof = false;
    m_sector = -1;
    m_lastFlushed = -1;
    m_dirty = false;
    m_highest = 0;
    m_size = 0;
    m_crc = 0;
    m_error = "";
    m_failed = false;
}

bool HexWriter::fail(const char* why)
{
    if (!m_failed) {
        m_failed = true;
        m_error = why;
        logf("hex: %s", why);
    }
    return false;
}

bool HexWriter::feed(const uint8_t* data, size_t len)
{
    if (m_failed) return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = char(data[i]);
        if (c == '\n' || c == '\r') {
            if (m_overflow) return fail("line too long");
            if (m_len > 0 && !line()) return false;
            m_len = 0;
        } else if (m_len < sizeof(m_line) - 1) {
            m_line[m_len++] = c;
        } else {
            m_overflow = true;
        }
    }
    return true;
}

bool HexWriter::line()
{
    if (m_eof) return true; // ignore anything after the end record
    m_line[m_len] = 0;
    if (m_line[0] != ':' || m_len < 11 || (m_len - 1) % 2 != 0) return fail("not an Intel HEX file");

    uint8_t bytes[300];
    const size_t count = (m_len - 1) / 2;
    if (count > sizeof(bytes)) return fail("line too long");
    uint8_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const int hi = hexValue(m_line[1 + 2 * i]), lo = hexValue(m_line[2 + 2 * i]);
        if (hi < 0 || lo < 0) return fail("not an Intel HEX file");
        bytes[i] = uint8_t(hi << 4 | lo);
        sum += bytes[i];
    }
    const uint8_t n = bytes[0];
    if (count != size_t(n) + 5) return fail("damaged line (length)");
    if (sum != 0) return fail("damaged line (checksum)");

    const uint16_t offset = uint16_t(bytes[1]) << 8 | bytes[2];
    const uint8_t  type   = bytes[3];
    switch (type) {
    case 0x00: // data
        for (uint8_t i = 0; i < n; ++i)
            if (!put(m_base + offset + i, bytes[4 + i])) return false;
        return true;
    case 0x01: m_eof = true; return true;
    case 0x02: // extended segment address
        if (n != 2) return fail("damaged line");
        m_base = (uint32_t(bytes[4]) << 8 | bytes[5]) << 4;
        return true;
    case 0x04: // extended linear address
        if (n != 2) return fail("damaged line");
        m_base = (uint32_t(bytes[4]) << 8 | bytes[5]) << 16;
        return true;
    case 0x03:
    case 0x05: return true; // start address: not used on AVR
    default: return fail("unknown record type");
    }
}

bool HexWriter::put(uint32_t address, uint8_t value)
{
    if (address >= AVR_APP_SIZE) return fail("too big for the printer's board");
    const int32_t page = int32_t(address / PAGE);
    if (page != m_sector) {
        if (!flushSector()) return false;
        if (page <= m_lastFlushed) return fail("addresses out of order");
        memset(g_page, 0xFF, sizeof(g_page));
        m_sector = page;
    }
    reinterpret_cast<uint8_t*>(g_page)[address % PAGE] = value;
    m_dirty = true;
    if (address + 1 > m_highest) m_highest = address + 1;
    return true;
}

bool HexWriter::flushSector()
{
    if (m_sector < 0 || !m_dirty) return true;
    // The area was erased first, so each page can be written once, in any order of pages
    if (!ESP.flashWrite(areaStart() + uint32_t(m_sector) * PAGE, g_page, PAGE)) return fail("flash write failed");
    m_lastFlushed = m_sector;
    m_dirty = false;
    return true;
}

bool HexWriter::finish()
{
    if (m_failed) return false;
    if (m_len > 0 && !line()) return false; // a last line without newline
    m_len = 0;
    if (!m_eof) return fail("file incomplete");
    if (!flushSector()) return false;
    if (m_highest < 1024) return fail("file too small to be firmware");
    m_size = (m_highest + 255) / 256 * 256;
    // ATmega2560 programs start with the interrupt table: a JMP (0x940C, stored 0C 94) to the start-up code
    uint8_t head[4];
    if (!read(0, head, sizeof(head)) || head[0] != 0x0C || head[1] != 0x94) return fail("not ATmega2560 firmware");
    m_crc = Stage::crc(m_size);
    logf("hex: image %lu bytes, crc %08lx", (unsigned long)m_size, (unsigned long)m_crc);
    return true;
}

HexWriter& hexWriter()
{
    static HexWriter writer;
    return writer;
}

} // namespace Stage
