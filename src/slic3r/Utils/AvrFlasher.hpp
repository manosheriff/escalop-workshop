#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <boost/optional.hpp>

namespace Slic3r {
namespace Utils {

// Escaloplab: updates the Escaloplab v80's firmware over USB. The v80 has an ATmega2560 board with the
// Arduino Mega (STK500v2) bootloader, so the firmware goes in the same way the Arduino IDE / avrdude
// "wiring" programmer sends it. The bootloader itself is never written, so a failed update can always
// be repeated.

// The flash contents of an Intel HEX file, from address 0, gaps filled with 0xFF.
struct FirmwareImage
{
    std::vector<uint8_t> data;

    // Throws Slic3r::RuntimeError with a message for the user when the file is not a usable firmware.
    static FirmwareImage from_intel_hex(const std::string& text);
};

// What a running Marlin reports about itself (M115).
struct PrinterFirmwareInfo
{
    std::string firmware_name; // e.g. "Marlin 2.1.2.1 (Sep 18 2023)"
    std::string machine_type;  // e.g. "Escaloplab v80"
};

class AvrFlasher
{
public:
    // stage: short text for the user; percent: 0..100 for the whole update
    using Progress = std::function<void(const std::string& stage, int percent)>;

    // Writes the image and reads it back to verify. Blocks; run it on a worker thread.
    // Throws Slic3r::RuntimeError with a message for the user.
    static void flash(const std::string& port, const FirmwareImage& image, const Progress& progress);

    // Asks the printer on the port for its firmware (opening the port restarts the printer).
    // Empty when the printer doesn't answer. Blocks for up to ~15 s.
    static boost::optional<PrinterFirmwareInfo> query(const std::string& port);
};

} // namespace Utils
} // namespace Slic3r
