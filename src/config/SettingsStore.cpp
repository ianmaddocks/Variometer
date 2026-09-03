#include "config/SettingsStore.h"

#include <Preferences.h>

#include "config/Config.h"

namespace variometer {
namespace SettingsStore {
namespace {
constexpr char kNamespace[] = "vario-settings";
}

void load(DeviceSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        // Namespace doesn't exist yet (first boot); constructor defaults stand.
        return;
    }
    settings.audioVarioEnabled = prefs.getBool("audio", settings.audioVarioEnabled);
    settings.hapticVarioEnabled = prefs.getBool("haptic", settings.hapticVarioEnabled);
    settings.replaySpeed = static_cast<uint8_t>(prefs.getUChar("replay", settings.replaySpeed));
    prefs.end();

    DBGF("SettingsStore: loaded audio=%d haptic=%d replay=%u\n", settings.audioVarioEnabled,
         settings.hapticVarioEnabled, static_cast<unsigned>(settings.replaySpeed));
}

void save(const DeviceSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        DBGLN("SettingsStore: failed to open NVS namespace for writing");
        return;
    }
    prefs.putBool("audio", settings.audioVarioEnabled);
    prefs.putBool("haptic", settings.hapticVarioEnabled);
    prefs.putUChar("replay", settings.replaySpeed);
    prefs.end();

    DBGLN("SettingsStore: saved");
}

}  // namespace SettingsStore
}  // namespace variometer
