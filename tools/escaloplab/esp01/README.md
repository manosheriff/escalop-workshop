# Escaloplab v80 Wi-Fi module (ESP-01)

The ESP-01 inside the v80 gives the printer:

- **Wi-Fi setup two ways.** At start-up without Wi-Fi, the printer shows *"Wi-Fi setup / On your phone join:
  Escalop-v80-00123"*. Join that open network on a phone and the setup page opens by itself (else http://192.168.4.1).
  Or set it up on the printer: *Menu > Wi-Fi > Choose network*, then type the password with the knob.
- **Automatic firmware updates.** Once a day it checks GitHub for a `firmware-vX.Y.Z` release, downloads and checks it,
  and the printer asks *"New firmware X.Y.Z available. It installs automatically over Wi-Fi"*: **Update** / **Later**.
  It can also install a `.hex` that the printer reads from its SD card (*Menu > Firmware > Update from SD card*).
- **Fast printing over Wi-Fi.** Escalop Workshop sends prints as if to OctoPrint; the file goes straight onto the SD
  card at 1 Mbaud, then printing starts.
- **The printer's web page** at `http://escalop-v80-<serial>.local`: temperatures, progress, pause/stop, files,
  firmware.
- **Its own updates** from `wifi-vX.Y.Z` releases, and its **serial number** from escaloplab.com.

How it talks to Marlin: [PROTOCOL.md](PROTOCOL.md) (Marlin 1.7.0 or newer). The code is the Arduino sketch in
`EscalopWiFi/`.

## Parts (as built and tested on the first v80)

| Part | Notes |
|---|---|
| ESP-01 or ESP-01S | 1 MB flash (all current ones) |
| 3.3 V regulator: KA78R33 (or an AMS1117-3.3 module) | 5 V → 3.3 V. The Mega's 3.3 V pin is far too weak (the ESP takes up to 300 mA). KA78R33 pins: 1 in, 2 out, 3 GND, 4 (on/off) to GND |
| 100–330 µF capacitor | across the ESP's VCC and GND, right at the ESP |
| 2 × 1N4148 diodes | ESP TX → D0 (keeps USB working) and ESP GPIO0 → RESET |
| 1 kΩ and 2 kΩ resistors | Mega TX → ESP RX divider (5 V → 3.3 V) |

## Wiring in the printer (RAMPS 1.4 on the Mega 2560)

The ESP-01 uses the Mega's **Serial0** (the USB port's serial line), because the Mega's bootloader only listens there,
and that's how the module installs firmware. RAMPS **AUX-1** has `5V`, `GND`, `D0` (RX0), `D1` (TX0).

```
AUX-1 5V   ── KA78R33 pin 1
AUX-1 GND  ── KA78R33 pins 3 + 4, ESP GND
KA78R33 pin 2 (3.3 V) ── ESP VCC + EN          (capacitor + to VCC, stripe to GND, at the ESP)
ESP TX     ──|◄── AUX-1 D0 (RX0)               1N4148, black band toward the ESP
ESP GPIO0  ──|◄── Mega RESET                   1N4148, black band toward the ESP
AUX-1 D1 (TX0) ── 1 kΩ ──┬── ESP RX
                         └── 2 kΩ ── GND
ESP GPIO2, ESP RST: not connected
```

- **Mega RESET:** the RAMPS reset button, on the leg that measures 5 V to GND (not the GND leg).
- **Use GPIO0, not GPIO2, for RESET:** the ESP's boot ROM pulses GPIO2 at every start, which restarted the printer
  whenever the module restarted. GPIO0 stays quiet (and the RESET pull-up keeps it high, as the ESP needs at start).
  (One side effect: if someone holds the printer's reset button exactly while the ESP starts, the ESP starts in
  programming mode; power-cycle to fix. Reflashing by hand later: GPIO0 to GND also holds the Mega in reset, harmlessly.)
- The diode on TX lets the USB port still talk to the board (USB printing, and USB firmware updates from Escalop
  Workshop keep working). The module goes quiet by itself while a USB update runs.
- At power-on the ESP's start-up noise used to leave the Mega's bootloader waiting (blank screen); the module now
  restarts the board and tells the bootloader to start Marlin.
- **Power:** the ESP takes ~80 mA on average. If the Mega's own 5 V regulator gets hot (12 V in), feed 5 V from a small
  12 V → 5 V buck converter instead.
- Keep the wires short (under ~15 cm); the link runs at 1 Mbaud.

## First installation, using an Arduino Uno as the USB adapter

Do this once per module, before it goes in the printer. Later updates come over Wi-Fi.

1. Wire the **Uno's RESET to GND** (that switches the Uno's own chip off, so its USB chip talks to the ESP directly).
2. Power the ESP from an AMS1117 fed by the Uno's 5 V (not the Uno's 3.3 V pin). EN, RST to 3.3 V through 10 kΩ.
3. **ESP GPIO0 to GND** (programming mode).
4. Uno **pin 0 (RX)** → 1 kΩ → ESP RX, and ESP RX → 2 kΩ → GND. ESP TX → Uno **pin 1 (TX)**.
   (Yes, RX to RX: with the Uno's chip off, pin 0 carries what the computer sends.)
5. Arduino IDE:
   - *File > Preferences > Additional boards manager URLs*: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - *Boards Manager*: install **esp8266 by ESP8266 Community** (3.1.2). *Library Manager*: install **ArduinoJson** (7.x).
   - Open `EscalopWiFi/EscalopWiFi.ino`. *Tools*: Board **Generic ESP8266 Module**, Flash Size
     **1MB (FS:none OTA:~502KB)**, Flash Mode **DOUT**, CPU Frequency **160 MHz**, Upload Speed **115200**,
     Reset Method **no dtr (aka ck)**, Port = the Uno's port. Click **Upload**.
   - Or flash the ready-made file from the GitHub Actions run ("Escalop v80 Wi-Fi module" → artifact):
     `python -m esptool --port COM5 --baud 115200 write_flash -fm dout 0x0 EscalopV80_WiFi_1.0.0.bin`
6. Remove GPIO0 from GND and power-cycle. A Wi-Fi network **Escalop-v80-XXXXXX** appears: the module works.

The printer must run **Marlin 1.7.0 or newer** (1 Mbaud and the protocol). A printer still on an older firmware
works too: the module notices the old speed, and its web page offers *Install update* to bring it to the newest
firmware.

## Serial numbers

The first time a module reaches the internet it checks in at `https://escaloplab.com/api/printer-units/checkin` and
gets its serial number (shown on the printer and in the admin portal's *Printer units*). Until then the hotspot is
named after the chip (`Escalop-v80-3F9A2C`). At the factory: connect each printer to Wi-Fi once, then
*Menu > Wi-Fi > Forget Wi-Fi* before shipping, so the customer sees the setup screen.

## Releases

- **Printer firmware:** see [../FIRMWARE.md](../FIRMWARE.md) (tag `firmware-vX.Y.Z`, asset `EscalopV80_Firmware_X.Y.Z.hex`).
- **This module:** raise `WIFI_VERSION` in `EscalopWiFi/config.h`, push. GitHub Actions ("Escalop v80 Wi-Fi module")
  builds it; download the artifact and publish the compressed file:
  ```
  gh release create wifi-v1.0.1 --latest=false --title "v80 Wi-Fi module 1.0.1" --notes "What changed" EscalopV80_WiFi_1.0.1.bin.gz
  ```
  Modules install it within a day (the printer keeps running; Wi-Fi drops for a few seconds).
- Neither is ever marked *latest*: that's the Escalop Workshop app.

## Good to know

- The setup hotspot is open, as decided, but only serves the setup page. The printer's page on the home network has
  no password (like OctoPrint without a login): anyone on that Wi-Fi can use it.
- Only 2.4 GHz Wi-Fi works (ESP8266).
- The log is at `http://escalop-v80-<serial>.local/log` (the serial line is taken by the printer, so there's no console).
- `make_certs.py` regenerates the trusted root certificates (`EscalopWiFi/certs.h`) from Mozilla's list.
- Build locally: `pio run` in this folder (PlatformIO), same settings as the Arduino IDE.

## Checklist for the first real test

1. Phone setup through the hotspot: wrong password (error shown on the phone and the printer), then the right one.
2. Setup on the printer's screen: scan, type the password, connect.
3. Web page at `http://escalop-v80-<serial>.local`: temperatures, preheat, file list.
4. Escalop Workshop: physical printer → OctoPrint → address → *Test* → send a print with *Print*. Without the SD card:
   the printer says to insert it. The speed is in `/log` ("upload: ... KB/s").
5. Firmware: publish a newer `firmware-v` release, *Menu > Firmware > Check for updates* → offer → Update.
6. *Update from SD card* with the `.hex` on the card.
7. A USB update from Escalop Workshop's Printer tab with the module connected.
