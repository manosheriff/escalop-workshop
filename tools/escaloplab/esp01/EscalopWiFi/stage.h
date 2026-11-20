#pragma once

#include <Arduino.h>

// The staging area: a fixed part of the ESP-01's own flash (after its program) that holds a printer firmware image
// while it is downloaded, checked and installed. The image is the binary the ATmega2560 runs, decoded from Intel HEX.
//
// The module's own updates (ESP8266 Updater) use the same space, so installing one of those discards a staged image.

namespace Stage {

// False when the program is too big to leave room for a printer firmware (shouldn't happen with 1 MB flash)
bool available();

// Erases the whole area (about 3 s). Required before a HexWriter starts.
bool erase();

// Reads `len` bytes of the staged image at `offset` (any alignment)
bool read(uint32_t offset, uint8_t* out, size_t len);

// CRC-32 of the first `size` bytes of the staged image
uint32_t crc(uint32_t size);

// Turns Intel HEX text, fed in pieces of any size, into the binary image in the staging area.
class HexWriter
{
public:
    void begin();
    // False on the first error (see error())
    bool feed(const uint8_t* data, size_t len);
    // Call after the last piece. True when the file was complete and looks like ATmega2560 firmware.
    bool finish();

    uint32_t      size() const { return m_size; } // image size, rounded up to whole 256-byte pages
    uint32_t      crc() const { return m_crc; }
    const String& error() const { return m_error; }

private:
    bool line();
    bool put(uint32_t address, uint8_t value);
    bool flushSector();
    bool fail(const char* why);

    char     m_line[600];
    size_t   m_len         = 0;
    bool     m_overflow    = false;
    uint32_t m_base        = 0;      // from record types 02/04
    bool     m_eof         = false;
    int32_t  m_sector      = -1;     // 256-byte page held in the buffer
    int32_t  m_lastFlushed = -1;
    bool     m_dirty       = false;
    uint32_t m_highest     = 0;      // highest address written + 1
    uint32_t m_size        = 0;
    uint32_t m_crc         = 0;
    String   m_error;
    bool     m_failed      = false;
};

// The one HexWriter (it needs 600 bytes; downloads and SD transfers never overlap)
HexWriter& hexWriter();

} // namespace Stage
