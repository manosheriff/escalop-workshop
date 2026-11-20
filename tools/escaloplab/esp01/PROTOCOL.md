# Escalop v80 Wi-Fi: ESP-01 ⇄ Marlin protocol (version 1)

This file is the single source of truth for how the ESP-01 Wi-Fi module and the printer's Marlin firmware
talk to each other. The ESP-01 code (`EscalopWiFi.ino`, next to this file) and Marlin must both follow it exactly.

## 1. The link

- ESP-01 TX/RX are wired to the Mega 2560's **Serial0** (RAMPS AUX-1 pins D0 = RX0, D1 = TX0), the same port as USB.
  Serial0 is required: the Mega's bootloader only listens there, and the ESP-01 installs new Marlin firmware through it.
- Speed **1 000 000 baud**, 8N1, for Marlin and the ESP-01. (The bootloader itself runs at 115 200; only the ESP-01 uses that, while flashing.)
- The ESP-01 can pull the Mega's RESET low (GPIO2, through a level shifter). Marlin never needs to do anything for that.
- Because USB shares the port, a computer connected over USB sees everything Marlin sends, including the lines below.
  That is harmless. Customers are told not to print over USB and Wi-Fi at the same time.
- When the ESP-01 receives control bytes (below 0x20, other than `
`, ``, `	`) it assumes the bootloader is talking
  to a computer over USB (a USB firmware update) and sends nothing for 20 s. So Marlin must never print such bytes on
  Serial0 (it normally doesn't).

## 2. Text encoding

- Lines end with `\n` (Marlin may send `\r\n`; both sides ignore `\r`).
- **Free text (SSIDs, passwords, hotspot names) is always sent as hex of its UTF-8 bytes**, two hex digits per byte,
  uppercase when sending, either case accepted when reading. An empty string is sent as `-`.
  Example: `MyWiFi 5G` → `4D7957694669203547`. This avoids every G-code problem with `;`, `*`, spaces, quotes and case.
- Numbers are plain decimal. Versions are plain text like `1.7.0` (digits and dots only).

## 3. ESP-01 → Marlin: G-code commands

The ESP-01 sends these as ordinary G-code lines (no line numbers, no checksums). Marlin must treat everything after
the code and one space as **one raw string** (like M117), then split it on single spaces itself.
Each command is at most 95 characters. Marlin answers `ok` as usual; the ESP-01 ignores `ok`.

| Command | Meaning | Marlin does |
|---|---|---|
| `M9100` | Hello: tell me about yourself | Reply with the `//ESC:INFO` line (section 4) |
| `M9101 <state> <a> <b>` | Wi-Fi state changed (or re-sent on request) | Store it for the Wi-Fi menu; react as in section 6 |
| `M9102 <count>` | Wi-Fi scan finished, `<count>` networks follow (0–10) | Prepare an empty list; if 0 show "No Wi-Fi networks found" |
| `M9103 <index> <rssi> <lock> <ssidhex>` | One network: index 0..count-1, signal in dBm (e.g. `-61`), lock `1` = needs password, `0` = open | Add to the list (display name may be truncated; keep only what fits). After the last index show the list |
| `M9104 <version>` | New firmware `<version>` is downloaded, checked and ready to install | Offer it (section 7). Only sent when Marlin last reported `printing=0 busy=0` |
| `M9105 <stage> <percent>` | Update progress. stage = `DOWNLOAD`, `CHECK` or `INSTALL` | Show progress. On `INSTALL`: show "Installing firmware - don't turn off the printer" and stop all motion/heaters; the ESP-01 resets the board within 2 s |
| `M9106 <result> <version>` | Sent after an update, once the new Marlin has said hello. result = `OK` or `FAIL` | `OK`: "Firmware updated to <version>". `FAIL`: "Firmware update failed - the old firmware is still installed" |
| `M9107 <serial>` | The printer's serial number (from escaloplab.com), e.g. `00123`, or `-` if not registered yet | Store it; show it in the Wi-Fi menu and the Info screen |
| `M9108 <code>` | A notice for the user | Show the text from the table in section 8 |
| `M9110 <size> <name>` | Start a fast binary upload of a print file to the SD card (section 9) | Enter binary receive mode |

`<state>` values for `M9101`:

| state | a | b | Meaning |
|---|---|---|---|
| `AP` | hotspot name hex | `-` | Setup hotspot is on (no Wi-Fi saved, or the saved one can't be reached). The phone setup page is at http://192.168.4.1 |
| `JOIN` | ssid hex | `-` | Connecting to a network |
| `ON` | ssid hex | IP address, plain, e.g. `192.168.1.57` | Connected |
| `OFF` | ssid hex or `-` | `-` | Not connected and no hotspot (e.g. network out of range, retrying) |
| `BADPW` | ssid hex | `-` | The password was wrong |

## 4. Marlin → ESP-01: lines starting with `//ESC:`

Marlin prints these lines on Serial0 (with `SERIAL_ECHOLN`/`SERIAL_ECHOPGM` style output; never with an `echo:` prefix).

| Line | When |
|---|---|
| `//ESC:INFO proto=1 model=v80 fw=<version> sd=<0/1> printing=<0/1> busy=<0/1> boot=<0/1>` | In reply to `M9100` (`boot=0`), and once by itself at the end of start-up (`boot=1`) |
| `//ESC:SCAN` | The user chose "Choose network" |
| `//ESC:JOIN <index> <passhex>` | The user picked network `<index>` from the last scan and typed a password (`-` for open networks) |
| `//ESC:HOTSPOT` | The user chose "Set up with phone": start the setup hotspot now |
| `//ESC:RESETWIFI` | The user chose "Forget Wi-Fi" (after confirming) |
| `//ESC:STATE` | The user opened the Wi-Fi menu: please re-send `M9101` and `M9107` |
| `//ESC:CHECK` | The user chose "Check for updates" |
| `//ESC:UPDATE` | The user accepted the firmware offer |
| `//ESC:LATER` | The user declined the offer (the ESP-01 offers again after 24 h or the next start) |
| `//ESC:SDFW <bytes>` | The user chose a `.hex` file in "Update firmware from SD card" (section 10) |

INFO fields:
- `fw` = Marlin's `ESCALOPLAB_FW_VERSION` (e.g. `1.7.0`).
- `sd` = 1 when an SD card is inserted **and** mounted.
- `printing` = 1 while an SD print is running or paused (or a host print is in progress, if Marlin can tell).
- `busy` = 1 when any heater target is above 0, the hotend is above 50 °C or the bed above 45 °C, or motion is
  queued/running. Firmware updates are only offered when `printing=0 busy=0`.
- `boot` = 1 only on the unsolicited line at start-up.

## 5. Timing rules

- At start-up Marlin sends `//ESC:INFO ... boot=1` once its serial port, SD card and LCD are ready.
- The ESP-01 sends `M9100` when it starts and then every 5 s until it gets an INFO line, and later whenever it needs
  fresh status (before uploads and updates). It also sends `M155 S2` and `M27 S2` after every `boot=1`, so Marlin
  reports temperatures and SD progress by itself (requires AUTO_REPORT_TEMPERATURES and AUTO_REPORT_SD_STATUS).
- The ESP-01 never sends anything while it is in the middle of a raw transfer (sections 9 and 10) except the bytes
  those sections define.

## 6. Wi-Fi screens (Marlin)

- `M9101 AP ...` received within 60 s after start-up → show a full screen until the knob is pressed:
  ```
  Wi-Fi setup
  On your phone join:
  Escalop-v80-00123
  Or: Menu > Wi-Fi
  ```
  (Line 3 is the hotspot name from `a`.) After start-up, `AP` only updates the Wi-Fi menu (and the status line).
- `JOIN` → "Connecting to <ssid>...". `ON` → "Wi-Fi connected" (and the IP in the Wi-Fi menu).
- `BADPW` → "Wrong Wi-Fi password" with **Try again** (re-opens the password editor with the text kept) and **Cancel**.
- `OFF` after a JOIN the user started → "Can't connect to <ssid>".

## 7. Firmware update over Wi-Fi

1. The ESP-01 checks GitHub once a day (and on `//ESC:CHECK`). If there is a newer `firmware-vX.Y.Z` release, it
   downloads and verifies it first, then sends `M9104 X.Y.Z` (only when `printing=0 busy=0`).
   On `//ESC:CHECK` with nothing new it sends `M9108 UPTODATE`; without internet `M9108 NOINTERNET`.
2. Marlin shows: `New firmware X.Y.Z` / `available. It installs` / `automatically over` / `Wi-Fi.` with **Update** / **Later**.
3. **Update** → Marlin sends `//ESC:UPDATE` and shows "Updating over Wi-Fi - don't turn off the printer".
   The ESP-01 sends `M9105 CHECK 100`, then `M9105 INSTALL 0`, then resets the board and installs.
4. The new Marlin starts and sends `//ESC:INFO ... boot=1`; the ESP-01 compares the version and sends `M9106 OK X.Y.Z`
   (or `M9106 FAIL <old version>`). If Marlin found its EEPROM reset at start-up, it adds:
   "Settings were reset: check the Z offset and level the bed".
5. If installing fails, the ESP-01 retries; the bootloader can't be damaged, so the printer can always be recovered
   (by the ESP-01 on the next power-on, or over USB with Escalop Workshop).

## 8. Notices (`M9108 <code>`)

| code | Text to show |
|---|---|
| `NOSD` | "Insert the SD card to print over Wi-Fi" (a print was sent but no card is in) |
| `PRINTING` | "Printer is busy - can't receive a file while printing" |
| `UPTODATE` | "Firmware is up to date" |
| `NOINTERNET` | "No internet - can't check for updates" |
| `DLFAIL` | "Firmware download failed. Try again later" |
| `SDFWBAD` | "This .hex file is not valid firmware" |
| `RECEIVED` | "Print file received" |

## 9. Fast print upload to the SD card (ESP-01 → Marlin)

The ESP-01 receives a G-code file over Wi-Fi and streams it straight onto the printer's SD card.

1. The ESP-01 asks `M9100` first and only continues if `sd=1 printing=0`. Otherwise it sends `M9108 NOSD` or
   `M9108 PRINTING` and refuses the upload.
2. ESP-01 sends `M9110 <size> <name>`. `<name>` is an 8.3 name in uppercase, `[A-Z0-9_]{1,8}.GCO`, made by the ESP-01.
   `<size>` is the expected file size **for the progress display only**; it may be an estimate or `0` (unknown).
   Marlin must not reject or delete a file because of `<size>`.
3. Marlin, in the M9110 handler:
   - No card / not mounted → print `#X NOSD`, printing → `#X BUSY`, anything else wrong → `#X ERR`, and return.
   - Otherwise delete any existing file with that name, create it, disable the emergency parser, print `#A` and enter
     a **blocking receive loop** that reads Serial0 bytes directly (not through the G-code queue).
4. Packets (ESP-01 → Marlin), one at a time, stop-and-wait:
   ```
   len_lo len_hi   data[len]   crc_lo crc_hi
   ```
   - `len` = 1..512 data bytes. `len = 0` means **end of file**. `len = 0xFFFF` means **abort** (no data follows).
   - CRC = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR) over the two length bytes **and**
     the data. Check value: CRC of ASCII `123456789` = `0x29B1`.
5. Marlin's replies (each a line):
   - `#A` packet written to the SD card, send the next one.
   - `#R` CRC wrong, length impossible, or a byte didn't arrive within 200 ms: Marlin first waits until the line has been
     quiet for 20 ms, then sends `#R`. The ESP-01 resends the same packet (at most 5 times, then aborts).
   - `#D <total>` after the end packet: file closed, `<total>` bytes written. The ESP-01 compares `<total>` with what it
     sent and deletes the file (`M30 <name>`) if they differ.
   - `#X <reason>` the transfer is over and the file is deleted: `ABORT`, `TIMEOUT` (no byte for 10 s), `SDERR`.
6. Rules for Marlin's loop:
   - Read each packet in a tight loop straight into a 512-byte buffer. Do **not** update the LCD, run `idle()`, or do
     anything slow while a packet is arriving (the RX buffer would overflow at 1 Mbaud).
   - After writing a packet to the SD card and **before** sending `#A`: refresh the watchdog, run heater management
     (`thermalManager.task()`), and at most every 500 ms update the LCD ("Receiving over Wi-Fi 42%").
   - Lines Marlin prints during the loop besides `#A/#R/#D/#X` are allowed (the ESP-01 ignores every line not starting with `#`).
   - Re-enable the emergency parser and flush leftover input when the loop ends, whatever the outcome.
7. After `#D`, when the user asked to print, the ESP-01 sends `M23 <name>` and `M24`. Otherwise it sends `M9108 RECEIVED`.

## 10. Firmware update from the SD card (Marlin → ESP-01)

The user copies the firmware `.hex` from escaloplab.com to the SD card. Marlin can't flash itself, so it hands the
file to the ESP-01, which installs it exactly like a Wi-Fi update.

1. Menu "Update firmware from SD card" lists the `.hex` files in the card's root (long names shown). The user picks
   one and confirms. Marlin refuses while printing or busy (heaters on): "Turn heaters off first".
2. Marlin turns all heaters off, then prints `//ESC:SDFW <bytes>` (`<bytes>` = file size) and waits up to 10 s for a
   line from the ESP-01:
   - `#READY` → go on.  `#NO <reason>` → show "Wi-Fi module not ready" and stop. Nothing in 10 s → show
     "No Wi-Fi module found" and stop.
3. Marlin sends the file's raw bytes in blocks of **1024 bytes** (the last block is shorter). After each block it waits
   up to 5 s for `#OK` (next block) or `#BAD` (stop; show "Transfer error, try again"). Marlin shows the progress.
4. After the last block Marlin waits up to 15 s for:
   - `#VALID <bytes>` → show "Installing firmware - don't turn off the printer" and wait. The ESP-01 resets the board
     within 2 s and installs; afterwards it sends `M9106` like a Wi-Fi update.
   - `#INVALID <reason>` → show "This .hex file is not valid firmware".
5. During the transfer Marlin reads Serial0 directly (not the G-code queue), keeps the watchdog refreshed, and ignores
   any input line that isn't `#READY`, `#NO`, `#OK`, `#BAD`, `#VALID` or `#INVALID`.
   From `//ESC:SDFW` until the final answer, Marlin must **print nothing else** on Serial0 (no auto-reports, no echo,
   no `idle()` that could print): the ESP-01 counts the received bytes, so any extra output would corrupt the file.

## 11. Releases the ESP-01 looks for (GitHub: manosheriff/escalop-workshop)

- Marlin firmware: tag `firmware-vX.Y.Z`, asset `EscalopV80_Firmware_X.Y.Z.hex` (one `.hex`, for the v80).
- ESP-01 software: tag `wifi-vX.Y.Z`, asset `EscalopV80_WiFi_X.Y.Z.bin.gz` (gzip-compressed; the ESP8266 installs it compressed).
- Drafts and pre-releases are ignored. Neither tag is ever marked "latest" (that's the Escalop Workshop app).
