#pragma once

#include "core/FlightData.h"

namespace variometer {
namespace SettingsStore {

// Reads persisted settings from NVS into `settings`, leaving its
// constructor defaults in place for any key that has never been saved
// (e.g. first boot). Uses the "nvs" partition already reserved in
// partitions.csv, so this needs no filesystem of its own.
void load(DeviceSettings& settings);

// Persists the fields that are actually wired to device behaviour
// (audio/haptic vario, replay speed). minSatellites/backgroundWhite are
// deliberately not persisted here -- see DisplayManager's edit-mode
// fields for why: they aren't read by anything yet, so saving them would
// just be NVS writes for values nothing acts on.
void save(const DeviceSettings& settings);

}  // namespace SettingsStore
}  // namespace variometer
