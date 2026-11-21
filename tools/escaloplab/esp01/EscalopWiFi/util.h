#pragma once

#include <Arduino.h>

// Small helpers shared by the whole sketch.

// A line in the in-memory log, shown at http://<printer>/log (Serial is the printer's link, so there's no console)
void logf_P(PGM_P fmt, ...);
#define logf(fmt, ...) logf_P(PSTR(fmt), ##__VA_ARGS__) // the format text stays in flash
String logText();
// At start-up: logs what happened before a crash restart. True when the crash was during a secure connection.
bool crashMemoryBegin();
// Marks a secure connection as open (remembered across a crash restart)
void setInTls(bool on);
// Call right before ESP.restart(): the next start doesn't report a problem
void markPlannedRestart();

// Work to do right after the next restart (kept in RTC memory across a software restart)
void     setBootTasks(uint32_t tasks);
uint32_t takeBootTasks(); // and clears them

// Text <-> hex of its bytes ("Hi" <-> "4869"). Empty text is "-" (PROTOCOL.md section 2).
String toHex(const String& text);
bool fromHex(const String& hex, String& text);

// Compares "1.7.0" with "1.10" numerically; missing parts count as 0. <0, 0, >0 like strcmp.
int compareVersions(const String& a, const String& b);
// True for "1", "1.7", "1.7.0" (digits and dots, starting and ending with a digit)
bool isVersion(const String& v);

// CRC-32 (IEEE, as zlib) and CRC-16/CCITT-FALSE (PROTOCOL.md section 9)
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);
uint16_t crc16Update(uint16_t crc, const uint8_t* data, size_t len);

// Time since `since`, safe across millis() wrap-around
inline uint32_t elapsed(uint32_t since) { return millis() - since; }

// Splits "a b c" on single spaces into at most `max` parts; returns the count
int splitWords(const String& line, String* parts, int max);
