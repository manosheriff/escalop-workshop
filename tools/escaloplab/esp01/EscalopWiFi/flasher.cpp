#include "flasher.h"

#include "config.h"
#include "stage.h"
#include "util.h"

#include <memory>

namespace Flasher {

namespace {
// STK500v2 (Atmel AVR068), as spoken by the Arduino Mega 2560 bootloader. Same sequence as Escalop Workshop's
// USB updater (src/slic3r/Utils/AvrFlasher.cpp).
constexpr uint8_t MESSAGE_START          = 0x1B;
constexpr uint8_t TOKEN                  = 0x0E;
constexpr uint8_t CMD_SIGN_ON            = 0x01;
constexpr uint8_t CMD_LOAD_ADDRESS       = 0x06;
constexpr uint8_t CMD_ENTER_PROGMODE_ISP = 0x10;
constexpr uint8_t CMD_LEAVE_PROGMODE_ISP = 0x11;
constexpr uint8_t CMD_PROGRAM_FLASH_ISP  = 0x13;
constexpr uint8_t CMD_READ_FLASH_ISP     = 0x14;
constexpr uint8_t CMD_READ_SIGNATURE_ISP = 0x1B;
constexpr uint8_t STATUS_CMD_OK          = 0x00;
constexpr size_t  PAGE                   = 256;
constexpr uint8_t SIGNATURE[3]           = {0x1E, 0x98, 0x01}; // ATmega2560

uint8_t  g_seq = 0;
uint8_t* g_answer    = nullptr; // PAGE + 8 bytes while installing
size_t   g_answerLen = 0;
constexpr size_t ANSWER_SIZE = PAGE + 8;

bool readByte(uint8_t& b, uint32_t deadline)
{
    while (int32_t(deadline - millis()) > 0) {
        if (Serial.available()) {
            b = uint8_t(Serial.read());
            return true;
        }
        yield();
    }
    return false;
}

// Sends one message; true when the matching answer arrived (in g_answer) in time
bool command(const uint8_t* body, size_t len, uint32_t timeoutMs)
{
    const uint8_t seq   = g_seq++;
    uint8_t       head[5] = {MESSAGE_START, seq, uint8_t(len >> 8), uint8_t(len & 0xFF), TOKEN};
    uint8_t       sum   = 0;
    for (uint8_t h : head) sum ^= h;
    for (size_t i = 0; i < len; ++i) sum ^= body[i];
    while (Serial.available()) Serial.read();
    Serial.write(head, sizeof(head));
    Serial.write(body, len);
    Serial.write(&sum, 1);

    const uint32_t deadline = millis() + timeoutMs;
    for (;;) {
        uint8_t b;
        do {
            if (!readByte(b, deadline)) return false;
        } while (b != MESSAGE_START);
        uint8_t h[4]; // sequence, size (2), token
        for (uint8_t& x : h)
            if (!readByte(x, deadline)) return false;
        const size_t size = size_t(h[1]) << 8 | h[2];
        if (h[3] != TOKEN || size == 0 || size > ANSWER_SIZE) continue;
        uint8_t check = MESSAGE_START ^ h[0] ^ h[1] ^ h[2] ^ h[3];
        for (size_t i = 0; i < size; ++i) {
            if (!readByte(g_answer[i], deadline)) return false;
            check ^= g_answer[i];
        }
        uint8_t sumByte;
        if (!readByte(sumByte, deadline)) return false;
        if (sumByte == check && h[0] == seq && g_answer[0] == body[0]) {
            g_answerLen = size;
            return true;
        }
    }
}

bool commandOk(const uint8_t* body, size_t len, uint32_t timeoutMs, int attempts = 3)
{
    for (int i = 0; i < attempts; ++i)
        if (command(body, len, timeoutMs) && g_answerLen >= 2 && g_answer[1] == STATUS_CMD_OK) return true;
    return false;
}

bool loadAddress(uint32_t byteAddress)
{
    // Word address; bit 31 selects the extended addressing the ATmega2560 (more than 64K words) needs
    const uint32_t w = (byteAddress / 2) | 0x80000000u;
    const uint8_t  body[] = {CMD_LOAD_ADDRESS, uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w)};
    return commandOk(body, sizeof(body), 500);
}

void leave()
{
    const uint8_t body[] = {CMD_LEAVE_PROGMODE_ISP, 0x01, 0x01};
    command(body, sizeof(body), 500);
}

bool signOn()
{
    const uint8_t body[] = {CMD_SIGN_ON};
    return command(body, sizeof(body), 120) && g_answerLen >= 2 && g_answer[1] == STATUS_CMD_OK;
}

void setSpeed(uint32_t baud)
{
    Serial.flush();
    Serial.updateBaudRate(baud);
    delay(2);
    while (Serial.available()) Serial.read();
}

bool run(uint32_t size, const std::function<void(int)>& progress, String& error)
{
    const size_t pages = (size + PAGE - 1) / PAGE;

    // The bootloader listens for about a second after a reset
    bool inBootloader = false;
    for (int attempt = 0; attempt < 4 && !inBootloader; ++attempt) {
        resetBoard();
        for (int i = 0; i < 10 && !inBootloader; ++i) inBootloader = signOn();
    }
    if (!inBootloader) {
        error = "the printer's board didn't answer";
        return false;
    }

    const uint8_t enter[] = {CMD_ENTER_PROGMODE_ISP, 0xC8, 0x64, 0x19, 0x20, 0x00, 0x53, 0x03, 0xAC, 0x53, 0x00, 0x00};
    if (!commandOk(enter, sizeof(enter), 1000)) {
        error = "the bootloader refused programming mode";
        return false;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t sig[] = {CMD_READ_SIGNATURE_ISP, 0x04, 0x30, 0x00, i, 0x00};
        if (!commandOk(sig, sizeof(sig), 500) || g_answerLen < 3 || g_answer[2] != SIGNATURE[i]) {
            error = "this board isn't an ATmega2560";
            leave();
            return false;
        }
    }

    std::unique_ptr<uint8_t[]> cmdBuf(new uint8_t[10 + PAGE]);
    uint8_t*                    cmd  = cmdBuf.get();
    const size_t                cmdLen = 10 + PAGE;
    uint8_t*                    data = cmd + 10;
    for (size_t p = 0; p < pages; ++p) {
        if (!Stage::read(p * PAGE, data, PAGE)) {
            error = "couldn't read the staged firmware";
            return false;
        }
        const uint8_t header[10] = {CMD_PROGRAM_FLASH_ISP, uint8_t(PAGE >> 8), uint8_t(PAGE & 0xFF),
                                    0xC1, 0x0A, 0x40, 0x4C, 0x20, 0x00, 0x00};
        memcpy(cmd, header, sizeof(header));
        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; ++attempt)
            ok = loadAddress(p * PAGE) && commandOk(cmd, cmdLen, 1500, 1);
        if (!ok) {
            error = "writing stopped at page " + String(p);
            return false;
        }
        progress(int(70 * (p + 1) / pages));
    }

    uint8_t* expected = data; // the write buffer is free now
    for (size_t p = 0; p < pages; ++p) {
        if (!Stage::read(p * PAGE, expected, PAGE)) {
            error = "couldn't read the staged firmware";
            return false;
        }
        const uint8_t rd[] = {CMD_READ_FLASH_ISP, uint8_t(PAGE >> 8), uint8_t(PAGE & 0xFF), 0x20};
        if (!loadAddress(p * PAGE) || !commandOk(rd, sizeof(rd), 1500) || g_answerLen < 2 + PAGE) {
            error = "reading back stopped at page " + String(p);
            return false;
        }
        if (memcmp(g_answer + 2, expected, PAGE) != 0) {
            error = "check failed at page " + String(p);
            return false;
        }
        progress(70 + int(29 * (p + 1) / pages));
    }

    leave(); // the board starts the new firmware
    return true;
}
} // namespace

void resetBoard()
{
    digitalWrite(RESET_PIN, LOW);
    delay(50);
    digitalWrite(RESET_PIN, HIGH);
    delay(10);
}

bool wakePrinter()
{
    // At power-on the ESP's start-up noise reaches the Mega's bootloader, which then waits for a programmer
    // forever (blank screen until someone presses reset). Restart the board (reset wire), then tell the bootloader
    // to start the firmware (works even without the reset wire).
    resetBoard();
    std::unique_ptr<uint8_t[]> answer(new uint8_t[ANSWER_SIZE]);
    g_answer = answer.get();
    setSpeed(BOOTLOADER_BAUD);
    bool           talked = false;
    const uint32_t start  = millis();
    while (!talked && elapsed(start) < 1500) talked = signOn();
    if (talked) leave();
    g_answer = nullptr;
    setSpeed(MARLIN_BAUD);
    return talked;
}

bool install(uint32_t size, const std::function<void(int)>& progress, String& error)
{
    if (size == 0 || size > AVR_APP_SIZE) {
        error = "no firmware staged";
        return false;
    }
    logf("flash: installing %lu bytes", (unsigned long)size);
    std::unique_ptr<uint8_t[]> answer(new uint8_t[ANSWER_SIZE]);
    g_answer = answer.get();
    setSpeed(BOOTLOADER_BAUD);
    const bool ok = run(size, progress, error);
    g_answer = nullptr;
    if (!ok) resetBoard(); // leave the bootloader; the old (or partial) firmware starts
    setSpeed(MARLIN_BAUD);
    if (ok) {
        progress(100);
        logf("flash: done");
    } else {
        logf("flash: failed: %s", error.c_str());
    }
    return ok;
}

} // namespace Flasher
