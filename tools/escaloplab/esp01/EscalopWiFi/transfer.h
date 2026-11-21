#pragma once

#include <Arduino.h>

// Raw transfers over the printer link (PROTOCOL.md sections 9 and 10). Both pause the normal line handling.

// Streams a print file onto the printer's SD card: begin(), write() any number of times, then finish() or abort().
class SdUpload
{
public:
    // name: 8.3 name like BENCHY3F.GCO. sizeHint: expected size for Marlin's progress display (0 = unknown).
    bool begin(uint32_t sizeHint, const String& name, String& error);
    bool write(const uint8_t* data, size_t len, String& error);
    bool finish(String& error);
    void abort();

    bool     active() const { return m_active; }
    uint32_t sent() const { return m_total; }

private:
    int  sendPacketOnce(const uint8_t* data, uint16_t len, String& error);
    bool sendPacket(const uint8_t* data, uint16_t len, String& error);
    bool sendData(const uint8_t* data, size_t len, String& error);
    void end();

    uint8_t* m_buf    = nullptr; // 512 bytes while a transfer runs
    size_t   m_fill   = 0;
    uint32_t m_total  = 0;
    uint16_t m_packet = 512; // shrinks when Marlin asks for resends
    bool     m_active = false;
    String   m_name;
};

// Receives a .hex file the printer reads from its SD card (after //ESC:SDFW <bytes>) into the staging area.
// True when it is valid ATmega2560 firmware; settings.staged* then describe it.
bool receiveSdFirmware(uint32_t bytes, String& error);

// A valid 8.3 G-code name for an uploaded file: up to 6 letters/digits from the name + 2 hex digits of its hash
String sdNameFor(const String& uploadName);
