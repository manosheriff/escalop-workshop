#include "settings.h"

#include <EEPROM.h>

Settings settings;

namespace {
constexpr uint32_t MAGIC  = 0x45534331; // "ESC1"; change when Settings changes shape
constexpr int      OFFSET = 4;          // the magic comes first
constexpr uint32_t EXT_MAGIC = 0x45585432; // "EXT2": the variant fields after pendingInstall are valid
constexpr uint32_t REMOTE_MAGIC = 0x524D5431; // "RMT1": remoteToken is valid
constexpr uint32_t TASKS_MAGIC  = 0x54534B31; // "TSK1": tasksAt is valid
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
        if (settings.extMagic != EXT_MAGIC) { // saved by a version before 1.0.15
            settings.extMagic       = EXT_MAGIC;
            settings.siteVariantSet = 0;
            memset(settings.siteVariant, 0, sizeof(settings.siteVariant));
            memset(settings.stagedVariant, 0, sizeof(settings.stagedVariant));
        }
        if (settings.remoteMagic != REMOTE_MAGIC) { // saved by a version before 1.0.22
            settings.remoteMagic = REMOTE_MAGIC;
            memset(settings.remoteToken, 0, sizeof(settings.remoteToken));
            memset(settings.remoteJob, 0, sizeof(settings.remoteJob));
        }
        if (settings.tasksMagic != TASKS_MAGIC) { // saved by a version before 1.0.24
            settings.tasksMagic = TASKS_MAGIC;
            settings.tasksAt    = 0;
        }
        settings.remoteToken[sizeof(settings.remoteToken) - 1] = 0;
        settings.remoteJob[sizeof(settings.remoteJob) - 1]     = 0;
        settings.siteVariant[sizeof(settings.siteVariant) - 1]     = 0;
        settings.stagedVariant[sizeof(settings.stagedVariant) - 1] = 0;
    } else {
        memset(&settings, 0, sizeof(settings));
        settings.extMagic    = EXT_MAGIC;
        settings.remoteMagic = REMOTE_MAGIC;
        settings.tasksMagic  = TASKS_MAGIC;
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
