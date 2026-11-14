#include "AvrFlasher.hpp"
#include "Serial.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <regex>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include <boost/algorithm/string/trim.hpp>
#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Exception.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"

namespace Slic3r {
namespace Utils {

namespace {

namespace asio = boost::asio;
using Clock    = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr size_t   FLASH_PAGE      = 256;                    // ATmega2560 flash page
constexpr size_t   APP_FLASH_SIZE  = 256 * 1024 - 8 * 1024;  // the top 8 KB hold the bootloader
constexpr unsigned BOOTLOADER_BAUD = 115200;
constexpr uint8_t  SIGNATURE[3]    = {0x1E, 0x98, 0x01};     // ATmega2560

// Serial::set_DTR is commented out upstream, so DTR is switched here
void set_dtr(asio::serial_port& port, bool on)
{
#ifdef _WIN32
    if (!EscapeCommFunction(port.native_handle(), on ? SETDTR : CLRDTR))
        throw RuntimeError("Could not set serial port DTR");
#else
    int status = 0;
    if (::ioctl(port.native_handle(), TIOCMGET, &status) != 0)
        throw RuntimeError("Could not read serial port lines");
    if (on)
        status |= TIOCM_DTR;
    else
        status &= ~TIOCM_DTR;
    if (::ioctl(port.native_handle(), TIOCMSET, &status) != 0)
        throw RuntimeError("Could not set serial port DTR");
#endif
}

// STK500v2 protocol (Atmel AVR068), as spoken by the Arduino Mega bootloader
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

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string connection_lost()
{
    return _u8L("The printer stopped answering during the update. Unplug the USB cable, plug it in again and "
                "run the update again. The printer can always be updated again.");
}

// A serial port with reads that give up at a deadline
class SerialLink
{
public:
    SerialLink(const std::string& port, unsigned baud) : m_port(m_io)
    {
        try {
            m_port.open(port);
            m_port.set_baud_rate(baud);
            m_port.set_option(asio::serial_port_base::character_size(8));
            m_port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
            m_port.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
            m_port.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Firmware: can't open " << port << ": " << e.what();
            throw RuntimeError(GUI::format(_u8L("%1% can't be opened. Close other programs that use the printer "
                                                "(for example Pronterface or Cura) and try again."), port));
        }
    }

    // The board restarts when DTR switches on (the USB chip pulses its reset line)
    void reset_board()
    {
        set_dtr(m_port, false);
        std::this_thread::sleep_for(milliseconds(250));
        set_dtr(m_port, true);
        std::this_thread::sleep_for(milliseconds(50));
        m_buf.clear();
        m_pos = 0;
        m_line.clear();
    }

    void write(const std::vector<uint8_t>& bytes) { asio::write(m_port, asio::buffer(bytes)); }
    void write(const std::string& text) { asio::write(m_port, asio::buffer(text)); }

    bool read_byte(uint8_t& out, Clock::time_point deadline)
    {
        if (m_pos == m_buf.size()) {
            const auto now = Clock::now();
            if (now >= deadline)
                return false;
            std::array<uint8_t, 512>  chunk;
            size_t                    got  = 0;
            bool                      done = false;
            boost::system::error_code error;
            m_port.async_read_some(asio::buffer(chunk), [&](const boost::system::error_code& ec, size_t n) {
                error = ec;
                got   = n;
                done  = true;
            });
            m_io.restart();
            m_io.run_for(deadline - now);
            if (!done) {
                m_port.cancel();
                m_io.restart();
                m_io.run(); // lets the cancelled read finish
            }
            if (got == 0) {
                if (error && error != asio::error::operation_aborted)
                    throw RuntimeError(_u8L("The connection to the printer was lost. Check the USB cable."));
                return false;
            }
            m_buf.assign(chunk.begin(), chunk.begin() + got);
            m_pos = 0;
        }
        out = m_buf[m_pos++];
        return true;
    }

    // A complete line; a partial one is kept for the next call
    bool read_line(std::string& line, Clock::time_point deadline)
    {
        uint8_t c;
        while (read_byte(c, deadline)) {
            if (c == '\n') {
                line = std::move(m_line);
                m_line.clear();
                return true;
            }
            if (c != '\r')
                m_line.push_back(char(c));
        }
        return false;
    }

private:
    asio::io_service     m_io;
    Serial               m_port;
    std::vector<uint8_t> m_buf;
    size_t               m_pos = 0;
    std::string          m_line;
};

class Stk500v2
{
public:
    explicit Stk500v2(SerialLink& link) : m_link(link) {}

    // The answer's body, or empty when the bootloader doesn't answer in time
    std::vector<uint8_t> command(const std::vector<uint8_t>& body, int timeout_ms = 1000)
    {
        const uint8_t        seq = m_seq++;
        std::vector<uint8_t> msg{MESSAGE_START, seq, uint8_t(body.size() >> 8), uint8_t(body.size() & 0xFF), TOKEN};
        msg.insert(msg.end(), body.begin(), body.end());
        uint8_t sum = 0;
        for (uint8_t b : msg)
            sum ^= b;
        msg.push_back(sum);
        m_link.write(msg);

        const auto deadline = Clock::now() + milliseconds(timeout_ms);
        for (;;) {
            uint8_t b;
            do {
                if (!m_link.read_byte(b, deadline))
                    return {};
            } while (b != MESSAGE_START);
            uint8_t head[4]; // sequence, size (2), token
            for (uint8_t& h : head)
                if (!m_link.read_byte(h, deadline))
                    return {};
            const size_t size = size_t(head[1]) << 8 | head[2];
            if (head[3] != TOKEN || size == 0 || size > 512)
                continue;
            std::vector<uint8_t> answer(size);
            for (uint8_t& a : answer)
                if (!m_link.read_byte(a, deadline))
                    return {};
            uint8_t check;
            if (!m_link.read_byte(check, deadline))
                return {};
            uint8_t expected = MESSAGE_START ^ head[0] ^ head[1] ^ head[2] ^ head[3];
            for (uint8_t a : answer)
                expected ^= a;
            if (expected == check && head[0] == seq && answer[0] == body[0])
                return answer;
        }
    }

    bool sign_on()
    {
        const auto answer = command({CMD_SIGN_ON}, 300);
        return answer.size() >= 2 && answer[1] == STATUS_CMD_OK;
    }

    std::vector<uint8_t> expect_ok(const std::vector<uint8_t>& body, int timeout_ms = 1000)
    {
        auto answer = command(body, timeout_ms);
        if (answer.size() < 2 || answer[1] != STATUS_CMD_OK)
            throw RuntimeError(connection_lost());
        return answer;
    }

    void load_address(size_t byte_address)
    {
        // Word address; bit 31 selects the extended (above 64K words) addressing the ATmega2560 needs
        const uint32_t word = uint32_t(byte_address / 2) | 0x80000000u;
        expect_ok({CMD_LOAD_ADDRESS, uint8_t(word >> 24), uint8_t(word >> 16), uint8_t(word >> 8), uint8_t(word)});
    }

    void leave() { command({CMD_LEAVE_PROGMODE_ISP, 0x01, 0x01}); }

private:
    SerialLink& m_link;
    uint8_t     m_seq = 1;
};

// "FIRMWARE_NAME:Marlin 2.1.2 (...) SOURCE_CODE_URL:..." -> "Marlin 2.1.2 (...)"
std::string m115_field(const std::string& line, const std::string& key)
{
    const size_t at = line.find(key);
    if (at == std::string::npos)
        return {};
    std::string            rest = line.substr(at + key.size());
    static const std::regex next_key(" [A-Z_]{4,}:");
    std::smatch            match;
    if (std::regex_search(rest, match, next_key))
        rest = rest.substr(0, match.position(0));
    return boost::trim_copy(rest);
}

} // namespace

FirmwareImage FirmwareImage::from_intel_hex(const std::string& text)
{
    const std::string not_firmware = _u8L("This file isn't printer firmware (an Intel HEX file).");
    FirmwareImage     image;
    uint32_t          base = 0;
    bool              end  = false;
    std::istringstream in(text);
    std::string       line;
    while (!end && std::getline(in, line)) {
        boost::trim(line);
        if (line.empty())
            continue;
        if (line[0] != ':' || line.size() < 11 || line.size() % 2 == 0)
            throw RuntimeError(not_firmware);
        std::vector<uint8_t> rec;
        for (size_t i = 1; i + 1 < line.size(); i += 2) {
            const int hi = hex_digit(line[i]), lo = hex_digit(line[i + 1]);
            if (hi < 0 || lo < 0)
                throw RuntimeError(not_firmware);
            rec.push_back(uint8_t(hi << 4 | lo));
        }
        const size_t len = rec[0];
        if (rec.size() != len + 5)
            throw RuntimeError(not_firmware);
        uint8_t sum = 0;
        for (uint8_t b : rec)
            sum += b;
        if (sum != 0)
            throw RuntimeError(_u8L("The firmware file is damaged. Download it again."));
        const uint32_t offset = uint32_t(rec[1]) << 8 | rec[2];
        switch (rec[3]) {
        case 0x00: { // data
            const size_t at = size_t(base) + offset;
            if (at + len > APP_FLASH_SIZE)
                throw RuntimeError(_u8L("This firmware is too big for the printer's board (ATmega2560)."));
            if (image.data.size() < at + len)
                image.data.resize(at + len, 0xFF);
            std::copy(rec.begin() + 4, rec.begin() + 4 + len, image.data.begin() + at);
            break;
        }
        case 0x01: end = true; break; // end of file
        case 0x02:                    // extended segment address
        case 0x04:                    // extended linear address
            if (len != 2)
                throw RuntimeError(not_firmware);
            base = (uint32_t(rec[4]) << 8 | rec[5]) << (rec[3] == 0x02 ? 4 : 16);
            break;
        case 0x03:
        case 0x05: break; // start address, unused on AVR
        default: throw RuntimeError(not_firmware);
        }
    }
    if (!end || image.data.empty())
        throw RuntimeError(not_firmware);
    return image;
}

void AvrFlasher::flash(const std::string& port, const FirmwareImage& image, const Progress& progress)
{
    auto report = [&progress](const std::string& stage, int percent) {
        if (progress)
            progress(stage, percent);
    };

    // The bootloader erases page by page in the order they come, so every page from address 0 is written
    std::vector<uint8_t> data = image.data;
    data.resize((data.size() + FLASH_PAGE - 1) / FLASH_PAGE * FLASH_PAGE, 0xFF);
    const size_t pages = data.size() / FLASH_PAGE;
    if (pages == 0)
        throw RuntimeError(_u8L("The firmware file is empty."));

    report(_u8L("Connecting to the printer"), 0);
    SerialLink link(port, BOOTLOADER_BAUD);
    Stk500v2   stk(link);

    // The bootloader listens for about a second after the board restarts
    bool in_bootloader = false;
    for (int attempt = 0; attempt < 3 && !in_bootloader; ++attempt) {
        link.reset_board();
        for (int i = 0; i < 5 && !in_bootloader; ++i)
            in_bootloader = stk.sign_on();
    }
    if (!in_bootloader)
        throw RuntimeError(_u8L("The printer didn't answer. Check that it is switched on, that the USB cable is plugged in "
                                "and that the right port is chosen."));

    stk.expect_ok({CMD_ENTER_PROGMODE_ISP, 0xC8, 0x64, 0x19, 0x20, 0x00, 0x53, 0x03, 0xAC, 0x53, 0x00, 0x00});
    for (uint8_t i = 0; i < 3; ++i) {
        const auto answer = stk.expect_ok({CMD_READ_SIGNATURE_ISP, 0x04, 0x30, 0x00, i, 0x00});
        if (answer.size() < 3 || answer[2] != SIGNATURE[i]) {
            stk.leave();
            throw RuntimeError(_u8L("This board isn't the Escaloplab v80's (ATmega2560). Nothing was changed."));
        }
    }
    BOOST_LOG_TRIVIAL(info) << "Firmware: writing " << data.size() << " bytes to " << port;

    for (size_t p = 0; p < pages; ++p) {
        const size_t address = p * FLASH_PAGE;
        stk.load_address(address);
        std::vector<uint8_t> cmd{CMD_PROGRAM_FLASH_ISP, uint8_t(FLASH_PAGE >> 8), uint8_t(FLASH_PAGE & 0xFF),
                                 0xC1, 0x0A, 0x40, 0x4C, 0x20, 0x00, 0x00};
        cmd.insert(cmd.end(), data.begin() + address, data.begin() + address + FLASH_PAGE);
        stk.expect_ok(cmd, 2000);
        report(_u8L("Installing"), 3 + int(72 * (p + 1) / pages));
    }

    for (size_t p = 0; p < pages; ++p) {
        const size_t address = p * FLASH_PAGE;
        stk.load_address(address);
        const auto answer = stk.expect_ok({CMD_READ_FLASH_ISP, uint8_t(FLASH_PAGE >> 8), uint8_t(FLASH_PAGE & 0xFF), 0x20}, 2000);
        if (answer.size() < 2 + FLASH_PAGE ||
            !std::equal(data.begin() + address, data.begin() + address + FLASH_PAGE, answer.begin() + 2)) {
            BOOST_LOG_TRIVIAL(error) << "Firmware: verify failed at " << address;
            throw RuntimeError(_u8L("The check after installing found a difference. Run the update again."));
        }
        report(_u8L("Checking"), 75 + int(24 * (p + 1) / pages));
    }

    stk.leave(); // the printer starts the new firmware
    BOOST_LOG_TRIVIAL(info) << "Firmware: update done on " << port;
    report(_u8L("Done"), 100);
}

boost::optional<PrinterFirmwareInfo> AvrFlasher::query(const std::string& port)
{
    // Marlin's usual speeds on 8-bit boards
    for (unsigned baud : {250000u, 115200u}) {
        SerialLink link(port, baud);
        link.reset_board(); // Marlin starts after the bootloader's wait
        const auto  until    = Clock::now() + std::chrono::seconds(7);
        auto        next_ask = Clock::now() + milliseconds(2500);
        std::string line;
        while (Clock::now() < until) {
            if (Clock::now() >= next_ask) {
                link.write(std::string("\nM115\n"));
                next_ask = Clock::now() + std::chrono::seconds(1);
            }
            if (!link.read_line(line, std::min(until, next_ask)) || line.find("FIRMWARE_NAME:") == std::string::npos)
                continue;
            PrinterFirmwareInfo info;
            info.firmware_name = m115_field(line, "FIRMWARE_NAME:");
            info.machine_type  = m115_field(line, "MACHINE_TYPE:");
            BOOST_LOG_TRIVIAL(info) << "Firmware: " << port << " at " << baud << " reports " << line;
            return info;
        }
    }
    return boost::none;
}

} // namespace Utils
} // namespace Slic3r
