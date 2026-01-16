#include "phoenix_settings.hpp"

#include "ad524x.hpp"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

// ===================== File Storage Constants ===================================
// Path to the settings file in internal flash filesystem.
static const char* k_settings_file_path = "/phoenix_settings.dat";

// ===================== Internal File Format =====================================
// The file stores a version byte followed by the PhoenixSettings struct. This allows
// detection of incompatible formats and migration when the struct changes.
struct SettingsFileHeader {
  uint8_t version;
};

namespace {

// ===================== Module State =============================================
static bool            g_initialized     = false;
static PhoenixSettings g_cached_settings = {};
static PhoenixSettings g_defaults        = {};  // Caller-provided defaults.

// Populates the settings struct with caller-provided default values.
static void apply_defaults(PhoenixSettings* settings) {
  if (settings == nullptr) {
    return;
  }
  memset(settings, 0, sizeof(PhoenixSettings));
  settings->blue_wiper_code  = g_defaults.blue_wiper_code;
  settings->green_wiper_code = g_defaults.green_wiper_code;
}

// Attempts to read settings from flash storage. Returns true if successful.
static bool load_from_flash(PhoenixSettings* settings) {
  using namespace Adafruit_LittleFS_Namespace;

  if (settings == nullptr) {
    return false;
  }

  // Step 1: Check if the settings file exists.
  if (!InternalFS.exists(k_settings_file_path)) {
    return false;
  }

  // Step 2: Open the file for reading.
  File file(InternalFS);
  if (!file.open(k_settings_file_path, FILE_O_READ)) {
    return false;
  }

  // Step 3: Read and validate the version header.
  SettingsFileHeader header = {};
  if (file.read(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }

  if (header.version != PHOENIX_SETTINGS_FORMAT_VERSION) {
    // Version mismatch; stored settings are incompatible.
    file.close();
    return false;
  }

  // Step 4: Read the settings payload.
  if (file.read(settings, sizeof(PhoenixSettings)) != sizeof(PhoenixSettings)) {
    file.close();
    return false;
  }

  file.close();
  return true;
}

// Writes settings to flash storage. Returns true if successful.
static bool save_to_flash(const PhoenixSettings* settings) {
  using namespace Adafruit_LittleFS_Namespace;

  if (settings == nullptr) {
    return false;
  }

  // Step 1: Remove existing file if present (ensures clean write).
  if (InternalFS.exists(k_settings_file_path)) {
    InternalFS.remove(k_settings_file_path);
  }

  // Step 2: Open file for writing (creates new file).
  File file(InternalFS);
  if (!file.open(k_settings_file_path, FILE_O_WRITE)) {
    return false;
  }

  // Step 3: Write version header.
  SettingsFileHeader header = {PHOENIX_SETTINGS_FORMAT_VERSION};
  if (file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }

  // Step 4: Write settings payload.
  if (file.write(reinterpret_cast<const uint8_t*>(settings), sizeof(PhoenixSettings)) != sizeof(PhoenixSettings)) {
    file.close();
    return false;
  }

  file.close();
  return true;
}

}  // namespace

int phoenix_settings_initialize(const PhoenixSettings* defaults) {
  // Step 1: Validate defaults pointer.
  GUARD_NONNULL(defaults);

  // Step 2: Store defaults for later use in apply_defaults and reset_to_defaults.
  memcpy(&g_defaults, defaults, sizeof(PhoenixSettings));

  // Step 3: Initialize the internal filesystem if not already mounted.
  if (!InternalFS.begin()) {
    return PHOENIX_SETTINGS_ERR_STORAGE;
  }

  // Step 4: Attempt to load settings from flash.
  if (load_from_flash(&g_cached_settings)) {
    g_initialized = true;
    return PHOENIX_SETTINGS_OK;
  }

  // Step 5: No valid settings found; create defaults and persist.
  apply_defaults(&g_cached_settings);
  if (!save_to_flash(&g_cached_settings)) {
    return PHOENIX_SETTINGS_ERR_STORAGE;
  }

  g_initialized = true;
  return PHOENIX_SETTINGS_OK;
}

bool phoenix_settings_is_initialized(void) {
  return g_initialized;
}

const PhoenixSettings* phoenix_settings_get(void) {
  if (!g_initialized) {
    return nullptr;
  }
  return &g_cached_settings;
}

int phoenix_settings_save(const PhoenixSettings* settings) {
  // Step 1: Validate inputs and module state.
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Update cached settings.
  memcpy(&g_cached_settings, settings, sizeof(PhoenixSettings));

  // Step 3: Persist to flash.
  if (!save_to_flash(&g_cached_settings)) {
    return PHOENIX_SETTINGS_ERR_STORAGE;
  }

  return PHOENIX_SETTINGS_OK;
}

int phoenix_settings_apply_wiper_codes(void) {
  // Step 1: Ensure module is initialized.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Apply blue wiper code to channel 1.
  GUARD(ad524x_set_wiper(1u, g_cached_settings.blue_wiper_code));

  // Step 3: Apply green wiper code to channel 0.
  GUARD(ad524x_set_wiper(0u, g_cached_settings.green_wiper_code));

  return PHOENIX_SETTINGS_OK;
}

int phoenix_settings_reset_to_defaults(void) {
  // Step 1: Ensure module is initialized.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Apply compile-time defaults.
  apply_defaults(&g_cached_settings);

  // Step 3: Persist defaults to flash.
  if (!save_to_flash(&g_cached_settings)) {
    return PHOENIX_SETTINGS_ERR_STORAGE;
  }

  return PHOENIX_SETTINGS_OK;
}

void phoenix_settings_deinitialize(void) {
  // Step 1: Clear cached settings for security.
  memset(&g_cached_settings, 0, sizeof(PhoenixSettings));

  // Step 2: Mark module as uninitialized.
  g_initialized = false;
}
