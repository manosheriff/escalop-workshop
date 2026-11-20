#pragma once

#include <Arduino.h>
#include <functional>

// Installs the staged image (stage.h) on the printer's ATmega2560: resets the board, talks STK500v2 to the Arduino
// Mega bootloader at 115200 baud on Serial, writes every page, reads everything back to check it, then starts the
// new firmware. Serial is back at Marlin's speed afterwards, whatever happened.

namespace Flasher {

// percent: 0..100. Returns false with `error` set; the bootloader itself is never touched, so it can be retried.
bool install(uint32_t size, const std::function<void(int percent)>& progress, String& error);

// Pulses the board's RESET line (Marlin restarts)
void resetBoard();

} // namespace Flasher
