#include "settings-storage.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace {

constexpr uint32_t SETTINGS_MAGIC = 0x4C435452;  // "LCTR"
constexpr uint8_t SETTINGS_VERSION_V1 = 1;
constexpr uint8_t SETTINGS_VERSION_V2 = 2;
constexpr uint8_t SETTINGS_VERSION_V3 = 3;
constexpr uint8_t APP_MODE_MIDI_TO_CV = 0;
constexpr uint8_t APP_MODE_SEQUENCER = 1;
constexpr uint8_t ROOT_NOTE_MIN = 0;
constexpr uint8_t ROOT_NOTE_MAX = 11;

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

constexpr uint32_t SETTINGS_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct PersistedSettingsV1 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t reserved[2];
	uint32_t checksum;
};

struct PersistedSettingsV2 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t reserved[1];
	uint32_t checksum;
};

struct PersistedSettingsV3 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint32_t checksum;
};

template <typename TSettings>
uint32_t checksum32(const TSettings& settings_without_checksum) {
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&settings_without_checksum);
	const size_t length = sizeof(TSettings) - sizeof(settings_without_checksum.checksum);

	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < length; ++i) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

bool is_valid_midi_channel(uint8_t channel) {
	return channel >= 1 && channel <= 16;
}

bool is_valid_app_mode(uint8_t mode) {
	return mode == APP_MODE_MIDI_TO_CV || mode == APP_MODE_SEQUENCER;
}

bool is_valid_root_note(uint8_t root_note) {
	return root_note >= ROOT_NOTE_MIN && root_note <= ROOT_NOTE_MAX;
}

PersistedSettingsV3 default_settings() {
	PersistedSettingsV3 settings{};
	settings.magic = SETTINGS_MAGIC;
	settings.version = SETTINGS_VERSION_V3;
	settings.midi_channel = 1;
	settings.app_mode = APP_MODE_MIDI_TO_CV;
	settings.root_note = 0;
	settings.checksum = checksum32(settings);
	return settings;
}

struct DeferredWriteState {
	PersistedSettingsV3 shadow_settings;
	bool shadow_loaded;
	bool dirty;
};

DeferredWriteState deferred_write_state = {default_settings(), false, false};

bool load_v3(const PersistedSettingsV3& flash_settings, PersistedSettingsV3& out_settings) {
	if (flash_settings.magic != SETTINGS_MAGIC || flash_settings.version != SETTINGS_VERSION_V3) {
		return false;
	}
	if (!is_valid_midi_channel(flash_settings.midi_channel)
		|| !is_valid_app_mode(flash_settings.app_mode)
		|| !is_valid_root_note(flash_settings.root_note)) {
		return false;
	}

	PersistedSettingsV3 candidate = flash_settings;
	if (checksum32(candidate) != flash_settings.checksum) {
		return false;
	}

	out_settings = flash_settings;
	return true;
}

bool load_v2(const PersistedSettingsV2& flash_settings, PersistedSettingsV3& out_settings) {
	if (flash_settings.magic != SETTINGS_MAGIC || flash_settings.version != SETTINGS_VERSION_V2) {
		return false;
	}
	if (!is_valid_midi_channel(flash_settings.midi_channel) || !is_valid_app_mode(flash_settings.app_mode)) {
		return false;
	}

	PersistedSettingsV2 candidate = flash_settings;
	if (checksum32(candidate) != flash_settings.checksum) {
		return false;
	}

	out_settings = default_settings();
	out_settings.midi_channel = flash_settings.midi_channel;
	out_settings.app_mode = flash_settings.app_mode;
	out_settings.checksum = checksum32(out_settings);
	return true;
}

bool load_v1(const PersistedSettingsV1& flash_settings, PersistedSettingsV3& out_settings) {
	if (flash_settings.magic != SETTINGS_MAGIC || flash_settings.version != SETTINGS_VERSION_V1) {
		return false;
	}
	if (!is_valid_midi_channel(flash_settings.midi_channel)) {
		return false;
	}

	PersistedSettingsV1 candidate = flash_settings;
	if (checksum32(candidate) != flash_settings.checksum) {
		return false;
	}

	out_settings = default_settings();
	out_settings.midi_channel = flash_settings.midi_channel;
	out_settings.checksum = checksum32(out_settings);
	return true;
}

bool load_settings(PersistedSettingsV3& out_settings) {
	const uint8_t* flash_base = reinterpret_cast<const uint8_t*>(XIP_BASE + SETTINGS_FLASH_OFFSET);
	const PersistedSettingsV3 flash_v3 = *reinterpret_cast<const PersistedSettingsV3*>(flash_base);

	if (load_v3(flash_v3, out_settings)) {
		return true;
	}

	const PersistedSettingsV2 flash_v2 = *reinterpret_cast<const PersistedSettingsV2*>(flash_base);
	if (load_v2(flash_v2, out_settings)) {
		return true;
	}

	const PersistedSettingsV1 flash_v1 = *reinterpret_cast<const PersistedSettingsV1*>(flash_base);
	return load_v1(flash_v1, out_settings);
}

void ensure_shadow_loaded() {
	if (deferred_write_state.shadow_loaded) {
		return;
	}

	PersistedSettingsV3 loaded = default_settings();
	load_settings(loaded);
	deferred_write_state.shadow_settings = loaded;
	deferred_write_state.shadow_loaded = true;
}

bool write_settings(const PersistedSettingsV3& settings) {
	PersistedSettingsV3 to_write = settings;
	to_write.magic = SETTINGS_MAGIC;
	to_write.version = SETTINGS_VERSION_V3;
	to_write.checksum = checksum32(to_write);

	uint8_t page_buffer[FLASH_PAGE_SIZE];
	memset(page_buffer, 0xFF, sizeof(page_buffer));
	memcpy(page_buffer, &to_write, sizeof(to_write));

	uint32_t interrupts = save_and_disable_interrupts();
	flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(SETTINGS_FLASH_OFFSET, page_buffer, FLASH_PAGE_SIZE);
	restore_interrupts(interrupts);

	PersistedSettingsV3 verify{};
	if (!load_settings(verify)) {
		return false;
	}

	return verify.midi_channel == to_write.midi_channel
		&& verify.app_mode == to_write.app_mode
		&& verify.root_note == to_write.root_note;
}

}  // namespace

bool load_persisted_midi_channel(uint8_t& out_channel) {
	ensure_shadow_loaded();
	out_channel = deferred_write_state.shadow_settings.midi_channel;
	return true;
}

bool save_persisted_midi_channel(uint8_t channel) {
	if (!is_valid_midi_channel(channel)) {
		return false;
	}
	ensure_shadow_loaded();
	if (deferred_write_state.shadow_settings.midi_channel == channel) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_channel = channel;
	deferred_write_state.shadow_settings.checksum = checksum32(deferred_write_state.shadow_settings);
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_app_mode(uint8_t& out_mode) {
	ensure_shadow_loaded();
	out_mode = deferred_write_state.shadow_settings.app_mode;
	return true;
}

bool save_persisted_app_mode(uint8_t mode) {
	if (!is_valid_app_mode(mode)) {
		return false;
	}
	ensure_shadow_loaded();
	if (deferred_write_state.shadow_settings.app_mode == mode) {
		return true;
	}
	deferred_write_state.shadow_settings.app_mode = mode;
	deferred_write_state.shadow_settings.checksum = checksum32(deferred_write_state.shadow_settings);
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_root_note(uint8_t& out_root_note) {
	ensure_shadow_loaded();
	out_root_note = deferred_write_state.shadow_settings.root_note;
	return true;
}

bool save_persisted_root_note(uint8_t root_note) {
	if (!is_valid_root_note(root_note)) {
		return false;
	}
	ensure_shadow_loaded();
	if (deferred_write_state.shadow_settings.root_note == root_note) {
		return true;
	}
	deferred_write_state.shadow_settings.root_note = root_note;
	deferred_write_state.shadow_settings.checksum = checksum32(deferred_write_state.shadow_settings);
	deferred_write_state.dirty = true;
	return true;
}

bool service_persisted_settings(bool allow_commit) {
	if (!allow_commit) {
		return true;
	}

	ensure_shadow_loaded();
	if (!deferred_write_state.dirty) {
		return true;
	}

	if (!write_settings(deferred_write_state.shadow_settings)) {
		return false;
	}

	deferred_write_state.dirty = false;
	return true;
}
