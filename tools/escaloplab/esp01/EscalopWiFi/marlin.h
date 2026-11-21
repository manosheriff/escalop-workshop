#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

// The link to the printer's Marlin firmware on Serial (PROTOCOL.md): sends G-code with simple "ok" flow control,
// reads Marlin's output and keeps what the web page and the update logic need to know.

namespace Marlin {

struct Info
{
    bool     valid     = false; // an //ESC:INFO arrived since Marlin last started
    String   fw;                // e.g. "1.7.0"; also filled from M115 for older firmware without the protocol
    bool     sd        = false;
    bool     printing  = false;
    bool     busy      = false;
    uint32_t at        = 0;     // millis() of the last INFO
};

struct Temps
{
    float    hotend = 0, hotendTarget = 0, bed = 0, bedTarget = 0;
    uint32_t at     = 0;
};

struct SdProgress
{
    bool     printing = false;
    uint32_t pos = 0, size = 0;
    uint32_t at = 0;
};

struct FileEntry
{
    String   name;     // 8.3 name, e.g. BENCHY3F.GCO
    String   longName; // when Marlin reports one
    uint32_t size = 0;
};

const Info&       info();
const Temps&      temps();
const SdProgress& sdProgress();
bool              protocol();       // the firmware speaks PROTOCOL.md (sent an INFO at least once)
bool              alive();          // any output from Marlin in the last 30 s
uint32_t          lastBoot();       // millis() when Marlin last said it started (0 = not seen)

void begin();
// Reads and handles everything Marlin sent; sends queued commands. Call often.
void poll();

// Queues one G-code line (no newline). Dropped when the queue is full or the link is paused.
void send(const String& line);
// Sends M9100 and waits for the INFO line
bool requestInfo(uint32_t timeoutMs = 2000);
// Waits until every queued command was answered (or timed out)
bool drain(uint32_t timeoutMs = 4000);
// Lists the SD card's root (M20 L)
bool listFiles(std::vector<FileEntry>& files, uint32_t timeoutMs = 6000);

// True once, about a second after Marlin stopped because someone pressed the KILL button (not for other kills,
// such as heater faults, whose message must stay on the screen)
bool takeKillButton();

// While paused, poll() leaves Serial alone (raw transfers and flashing read it themselves)
void pause();
void resume();

// After installing firmware (Serial is at Marlin's 1000000 baud again): find the link speed anew
void relink();

// True while a computer updates the board over USB (the module then sends nothing)
bool quiet();

// Called for every //ESC: line: (command, arguments)
void onEsc(std::function<void(const String&, const String&)> handler);
// Called when Marlin (re)starts
void onBoot(std::function<void(bool withProtocol)> handler);

} // namespace Marlin
