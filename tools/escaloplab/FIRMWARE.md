# Publishing v80 firmware for the Printer tab

The app's **Printer** tab (*Update firmware*) installs the newest firmware release from this GitHub repository over USB
(ATmega2560 board, Arduino Mega bootloader, same as the Arduino IDE / avrdude "wiring" upload).

## Make a firmware release

1. Build Marlin for each model and export the compiled `.hex` (Arduino IDE: *Sketch → Export compiled binary*;
   PlatformIO: `.pio/build/mega2560/firmware.hex`).
2. Name the files so the app can tell them apart: the Direct model's file must contain `direct`, e.g.
   - `EscalopV80_Firmware_1.0.0.hex`
   - `EscalopV80_Direct_Firmware_1.0.0.hex`
   (With only one `.hex` in the release, both models get it.)
3. Create the release with a tag `firmware-vX.Y.Z` and **don't mark it as latest**, so the website's
   "latest release" link keeps pointing at the app:

   ```
   gh release create firmware-v1.0.0 --latest=false --title "v80 firmware 1.0.0" \
       --notes "What changed" EscalopV80_Firmware_1.0.0.hex EscalopV80_Direct_Firmware_1.0.0.hex
   ```

The app ignores drafts and pre-releases (use a pre-release to stage a firmware before customers see it), and
the app's own updater ignores `firmware-` tags.

## Tips

- Put the version in Marlin's `SHORT_BUILD_VERSION` (e.g. `"Escaloplab 1.0.0"`) and set `MACHINE_NAME`;
  the tab's *Check printer* shows both (M115).
- A new Marlin version can reset the EEPROM: tell customers to check the Z offset and re-run bed leveling
  (the app says so too).
- Test a `.hex` before publishing with *Install it from this computer* at the bottom of the tab.
