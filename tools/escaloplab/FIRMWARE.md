# Publishing v80 firmware for the Printer tab

The app's **Printer** tab (*Update firmware*) installs the newest firmware release from this GitHub repository over USB
(ATmega2560 board, Arduino Mega bootloader, same as the Arduino IDE / avrdude "wiring" upload).

## Make a firmware release

1. Build Marlin for each model and export the compiled `.hex` (Arduino IDE: *Sketch → Export compiled binary*;
   PlatformIO: `.pio/build/mega2560/firmware.hex`).
2. Name the file `EscalopV80_Firmware_X.Y.Z.hex` (X.Y.Z = Marlin's `ESCALOPLAB_FW_VERSION`). Only the v80 exists for
   now; if a model with a different firmware comes back, its file name must contain its own word (the app's
   Printer tab and the Wi-Fi module both skip files containing `direct`).
3. Create the release with a tag `firmware-vX.Y.Z` and **don't mark it as latest**, so the website's
   "latest release" link keeps pointing at the app:

   ```
   gh release create firmware-v1.7.0 --latest=false --title "v80 firmware 1.7.0" \
       --notes "What changed" EscalopV80_Firmware_1.7.0.hex
   ```

Printers with the Wi-Fi module see the new firmware within a day ("New firmware available" on the screen) and
install it over Wi-Fi; see `esp01/README.md`. The app ignores drafts and pre-releases (use a pre-release to stage a firmware before customers see it), and
the app's own updater ignores `firmware-` tags.

## Tips

- The version is Marlin's `ESCALOPLAB_FW_VERSION` (Configuration.h), always three numbers (`1.7.0`): the Wi-Fi
  module compares it with the release tag. *Check printer* shows it (M115).
- A new Marlin version can reset the EEPROM: tell customers to check the Z offset and re-run bed leveling
  (the app says so too).
- Test a `.hex` before publishing with *Install it from this computer* at the bottom of the tab.
