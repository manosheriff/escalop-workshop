#include "transfer.h"

#include "config.h"
#include "marlin.h"
#include "settings.h"
#include "stage.h"
#include "util.h"

namespace {

// The next line from Marlin that starts with '#' ("#A", "#D 1234", ...), or "" after the timeout.
// Everything else Marlin prints meanwhile (temperatures, echo:) is skipped.
String waitHashLine(uint32_t timeoutMs)
{
    static char  line[64];
    size_t       len  = 0;
    bool         skip = false;
    const uint32_t start = millis();
    while (elapsed(start) < timeoutMs) {
        if (!Serial.available()) {
            yield();
            continue;
        }
        const char c = char(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (!skip && len > 0 && line[0] == '#') {
                line[len] = 0;
                return String(line);
            }
            len  = 0;
            skip = false;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        } else {
            skip = true;
        }
    }
    return String();
}

} // namespace

// --------------------------------------------------------------------------------------------------------------

bool SdUpload::begin(uint32_t sizeHint, const String& name, String& error)
{
    m_fill   = 0;
    m_total  = 0;
    m_name   = name;
    m_packet = 512;
    if (!m_buf) m_buf = static_cast<uint8_t*>(malloc(512));
    if (!m_buf) {
        error = "Out of memory";
        return false;
    }
    Marlin::drain(3000);
    Marlin::pause();
    while (Serial.available()) Serial.read();
    Serial.printf("M9110 %lu %s\n", (unsigned long)sizeHint, name.c_str());
    const String reply = waitHashLine(8000); // Marlin deletes/creates the file first
    if (reply == "#A") {
        m_active = true;
        logf("upload: %s started", name.c_str());
        return true;
    }
    error = reply.startsWith("#X NOSD") ? "Insert the SD card in the printer"
          : reply.startsWith("#X BUSY") ? "The printer is busy printing"
          : reply.length()             ? "The printer couldn't create the file (" + reply + ")"
                                       : "The printer didn't answer";
    logf("upload: refused: %s", reply.c_str());
    free(m_buf);
    m_buf = nullptr;
    Marlin::resume();
    return false;
}

// One packet: 1 = written, 0 = Marlin asks to send it again, -1 = the transfer is over (error set)
int SdUpload::sendPacketOnce(const uint8_t* data, uint16_t len, String& error)
{
    const uint8_t header[2] = {uint8_t(len & 0xFF), uint8_t(len >> 8)};
    uint16_t      crc       = crc16Update(0xFFFF, header, 2);
    if (len != 0xFFFF) crc = crc16Update(crc, data, len);
    const uint8_t tail[2] = {uint8_t(crc & 0xFF), uint8_t(crc >> 8)};

    Serial.write(header, 2);
    if (len != 0xFFFF && len > 0) Serial.write(data, len);
    Serial.write(tail, 2);
    const String reply = waitHashLine(len == 0 ? 15000 : 5000);
    if (reply == "#A") return 1;
    if (reply.startsWith("#D")) {
        const uint32_t total = strtoul(reply.c_str() + 2, nullptr, 10);
        if (total != m_total) {
            error = "The file on the SD card is incomplete";
            logf("upload: Marlin wrote %lu of %lu bytes", (unsigned long)total, (unsigned long)m_total);
            return -1;
        }
        return 1;
    }
    if (reply == "#R") return 0; // damaged on the way
    error = reply.startsWith("#X SDERR") ? "The SD card couldn't be written"
          : reply.length()              ? "The printer stopped the transfer (" + reply + ")"
                                        : "The printer stopped answering";
    logf("upload: failed: %s", reply.c_str());
    return -1;
}

bool SdUpload::sendPacket(const uint8_t* data, uint16_t len, String& error)
{
    for (int attempt = 0; attempt <= 5; ++attempt) {
        const int r = sendPacketOnce(data, len, error);
        if (r != 0) return r > 0;
    }
    error = "Too many transfer errors";
    return false;
}

// File data in packets of m_packet bytes. When Marlin asks for a packet again, packets get smaller: long bursts at
// 1 Mbaud can lose bytes on some boards; the size that works is kept for the rest of the file.
bool SdUpload::sendData(const uint8_t* data, size_t len, String& error)
{
    int failures = 0;
    while (len > 0) {
        const uint16_t n = uint16_t(std::min<size_t>(len, m_packet));
        const int      r = sendPacketOnce(data, n, error);
        if (r < 0) return false;
        if (r == 0) {
            if (m_packet > 16) {
                m_packet /= 2;
                logf("upload: resend asked, packets now %u bytes", (unsigned) m_packet);
            } else if (++failures > 5) {
                error = "Too many transfer errors";
                return false;
            }
            continue;
        }
        data += n;
        len -= n;
    }
    return true;
}

bool SdUpload::write(const uint8_t* data, size_t len, String& error)
{
    if (!m_active) {
        error = "No transfer in progress";
        return false;
    }
    while (len > 0) {
        const size_t n = std::min<size_t>(len, 512 - m_fill);
        memcpy(m_buf + m_fill, data, n);
        m_fill += n;
        data += n;
        len -= n;
        if (m_fill == 512) {
            m_total += m_fill;
            if (!sendData(m_buf, m_fill, error)) {
                end();
                return false;
            }
            m_fill = 0;
        }
    }
    return true;
}

bool SdUpload::finish(String& error)
{
    if (!m_active) {
        error = "No transfer in progress";
        return false;
    }
    if (m_fill > 0) {
        m_total += m_fill;
        if (!sendData(m_buf, m_fill, error)) {
            end();
            return false;
        }
        m_fill = 0;
    }
    const bool ok = sendPacket(nullptr, 0, error); // end of file; Marlin answers #D <total>
    end();
    if (!ok) Marlin::send("M30 " + m_name);
    else logf("upload: %s done, %lu bytes", m_name.c_str(), (unsigned long)m_total);
    return ok;
}

void SdUpload::abort()
{
    if (!m_active) return;
    String ignored;
    const uint8_t header[2] = {0xFF, 0xFF};
    const uint16_t crc      = crc16Update(0xFFFF, header, 2);
    const uint8_t  tail[2]  = {uint8_t(crc & 0xFF), uint8_t(crc >> 8)};
    Serial.write(header, 2);
    Serial.write(tail, 2);
    waitHashLine(3000); // #X ABORT
    logf("upload: %s aborted", m_name.c_str());
    end();
}

void SdUpload::end()
{
    m_active = false;
    free(m_buf);
    m_buf = nullptr;
    Marlin::resume();
}

// --------------------------------------------------------------------------------------------------------------

bool receiveSdFirmware(uint32_t bytes, String& error)
{
    Marlin::pause();
    struct Resume { ~Resume() { Marlin::resume(); } } resumeLater;

    if (bytes < 1000 || bytes > 1024u * 1024u) {
        Serial.print("#NO SIZE\n");
        error = "file size not plausible";
        return false;
    }
    if (!Stage::available()) {
        Serial.print("#NO SPACE\n");
        error = "no staging space";
        return false;
    }
    settings.stagedValid = 0;
    settingsSave();
    if (!Stage::erase()) {
        Serial.print("#NO FLASH\n");
        error = "staging erase failed";
        return false;
    }
    while (Serial.available()) Serial.read();
    Serial.print("#READY\n");

    Stage::HexWriter& hex = Stage::hexWriter();
    hex.begin();
    uint8_t  buf[256];
    uint32_t received = 0, lastByte = millis();
    while (received < bytes) {
        const size_t avail = Serial.available();
        if (avail == 0) {
            if (elapsed(lastByte) > 6000) {
                error = "the printer stopped sending";
                logf("sdfw: %s at %lu of %lu", error.c_str(), (unsigned long)received, (unsigned long)bytes);
                return false;
            }
            yield();
            continue;
        }
        // Never read past the end of the current 1024-byte block: Marlin waits for #OK after each one
        const uint32_t blockEnd = std::min<uint32_t>((received / 1024 + 1) * 1024, bytes);
        const size_t   n = Serial.readBytes(buf, std::min<size_t>({avail, sizeof(buf), size_t(blockEnd - received)}));
        lastByte = millis();
        if (!hex.feed(buf, n)) {
            Serial.print("#BAD\n");
            error = hex.error();
            return false;
        }
        received += n;
        if (received % 1024 == 0 || received == bytes) Serial.print("#OK\n"); // after every block, the last one too
    }
    if (!hex.finish()) {
        Serial.print("#INVALID HEX\n");
        error = hex.error();
        return false;
    }
    settings.stagedValid = 1;
    settings.stagedVersion[0] = 0; // unknown until it runs
    settings.stagedVariant[0] = 0;
    settings.stagedSize  = hex.size();
    settings.stagedCrc   = hex.crc();
    settingsSave();
    Serial.printf("#VALID %lu\n", (unsigned long)hex.size());
    logf("sdfw: received %lu bytes, image %lu bytes", (unsigned long)bytes, (unsigned long)hex.size());
    return true;
}

String sdNameFor(const String& uploadName)
{
    String base = uploadName;
    const int slash = std::max(base.lastIndexOf('/'), base.lastIndexOf('\\'));
    if (slash >= 0) base = base.substring(slash + 1);
    const int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    String clean;
    for (size_t i = 0; i < base.length() && clean.length() < 6; ++i) {
        const char c = base[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) clean += c;
        else if (c >= 'a' && c <= 'z') clean += char(c - 'a' + 'A');
    }
    if (clean.length() == 0) clean = "PRINT";
    uint8_t hash = 0;
    for (size_t i = 0; i < uploadName.length(); ++i) hash = uint8_t(hash * 31 + uint8_t(uploadName[i]));
    char suffix[3];
    snprintf(suffix, sizeof(suffix), "%02X", hash);
    return clean + suffix + ".GCO";
}
