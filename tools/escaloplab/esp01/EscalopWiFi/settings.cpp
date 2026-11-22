#include "settings.h"

#include <EEPROM.h>

Settings settings;

namespace {
constexpr uint32_t MAGIC  = 0x45534331; // "ESC1"; change when Settings changes shape
constexpr int      OFFSET = 4;          // the magic comes first
constexpr uint32_t EXT_MAGIC = 0x45585431; // "EXT1": the per-printer fields after pendingInstall are valid
} // namespace

// The EEPROM buffer (512 bytes) is only held while reading or writing, to keep RAM free for HTTPS
void settingsLoad()
{
    EEPROM.begin(512);
    static_assert(sizeof(Settings) + OFFSET <= 512, "settings don't fit");
    uint32_t magic = 0;
    EEPROM.get(0, magic);
    if (magic == MAGIC) {
        EEPROM.get(OFFSET, settings);
        // Never trust stored strings to be terminated
        settings.ssid[sizeof(settings.ssid) - 1]                   = 0;
        settings.pass[sizeof(settings.pass) - 1]                   = 0;
        settings.serial[sizeof(settings.serial) - 1]               = 0;
        settings.stagedVersion[sizeof(settings.stagedVersion) - 1] = 0;
        if (settings.extMagic != EXT_MAGIC) { // saved by a version before 1.0.14
            settings.extMagic = EXT_MAGIC;
            memset(settings.steps, 0, sizeof(settings.steps));
            memset(settings.fwChannel, 0, sizeof(settings.fwChannel));
        }
        settings.fwChannel[sizeof(settings.fwChannel) - 1] = 0;
    } else {
        memset(&settings, 0, sizeof(settings));
        settings.extMagic = EXT_MAGIC;
        EEPROM.put(0, MAGIC);
        EEPROM.put(OFFSET, settings);
    }
    EEPROM.end(); // writes if anything changed
}

void settingsSave()
{
    EEPROM.begin(512);
    EEPROM.put(0, MAGIC);
    EEPROM.put(OFFSET, settings);
    EEPROM.end();
}
