#include "settings-storage.h"

#include <cstring>

#include "brain/include/storage.h"

namespace {

constexpr uint32_t SETTINGS_MAGIC = 0x4C435452;  // "LCTR"
constexpr uint8_t SETTINGS_VERSION_V1 = 1;
constexpr uint8_t SETTINGS_VERSION_V2 = 2;
constexpr uint8_t SETTINGS_VERSION_V3 = 3;
constexpr uint8_t SETTINGS_VERSION_V4 = 4;
constexpr uint8_t APP_MODE_MIDI_TO_CV = 0;
constexpr uint8_t APP_MODE_SEQUENCER = 1;
constexpr uint8_t ROOT_NOTE_MIN = 0;
constexpr uint8_t ROOT_NOTE_MAX = 11;
constexpr uint8_t MIDI_CV_CHANNEL_A = 0;
constexpr uint8_t MIDI_CV_CHANNEL_B = 1;
constexpr uint8_t MIDI_CV_CHANNEL_UNSET = 0xFF;
constexpr uint8_t MIDI_MODE_DEFAULT = 0;
constexpr uint8_t MIDI_MODE_MODWHEEL = 1;
constexpr uint8_t MIDI_MODE_UNISON = 2;
constexpr uint8_t MIDI_MODE_DUO = 3;
constexpr uint8_t MIDI_MODE_UNSET = 0xFF;

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

struct PersistedSettingsV4 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint8_t midi_cv_channel;
	uint8_t midi_mode;
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

bool is_valid_midi_cv_channel(uint8_t cv_channel) {
	return cv_channel == MIDI_CV_CHANNEL_A || cv_channel == MIDI_CV_CHANNEL_B;
}

bool is_valid_midi_mode(uint8_t mode) {
	return mode >= MIDI_MODE_DEFAULT && mode <= MIDI_MODE_DUO;
}

PersistedSettingsV4 default_settings() {
	PersistedSettingsV4 settings{};
	settings.magic = SETTINGS_MAGIC;
	settings.version = SETTINGS_VERSION_V4;
	settings.midi_channel = 1;
	settings.app_mode = APP_MODE_SEQUENCER;
	settings.root_note = 0;
	settings.midi_cv_channel = MIDI_CV_CHANNEL_B;
	settings.midi_mode = MIDI_MODE_DEFAULT;
	settings.checksum = checksum32(settings);
	return settings;
}

struct DeferredWriteState {
	PersistedSettingsV4 shadow_settings;
	bool shadow_loaded;
	bool dirty;
};

DeferredWriteState deferred_write_state = {default_settings(), false, false};

bool load_v4(const PersistedSettingsV4& flash_settings, PersistedSettingsV4& out_settings) {
	if (flash_settings.magic != SETTINGS_MAGIC || flash_settings.version != SETTINGS_VERSION_V4) {
		return false;
	}
	if (!is_valid_midi_channel(flash_settings.midi_channel)
		|| !is_valid_app_mode(flash_settings.app_mode)
		|| !is_valid_root_note(flash_settings.root_note)
		|| !is_valid_midi_cv_channel(flash_settings.midi_cv_channel)
		|| !is_valid_midi_mode(flash_settings.midi_mode)) {
		return false;
	}

	PersistedSettingsV4 candidate = flash_settings;
	if (checksum32(candidate) != flash_settings.checksum) {
		return false;
	}

	out_settings = flash_settings;
	return true;
}

bool load_v3(const PersistedSettingsV3& flash_settings, PersistedSettingsV4& out_settings) {
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

	out_settings = default_settings();
	out_settings.midi_channel = flash_settings.midi_channel;
	out_settings.app_mode = flash_settings.app_mode;
	out_settings.root_note = flash_settings.root_note;
	out_settings.midi_cv_channel = MIDI_CV_CHANNEL_UNSET;
	out_settings.midi_mode = MIDI_MODE_UNSET;
	out_settings.checksum = checksum32(out_settings);
	return true;
}

bool load_v2(const PersistedSettingsV2& flash_settings, PersistedSettingsV4& out_settings) {
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
	out_settings.midi_cv_channel = MIDI_CV_CHANNEL_UNSET;
	out_settings.midi_mode = MIDI_MODE_UNSET;
	out_settings.checksum = checksum32(out_settings);
	return true;
}

bool load_v1(const PersistedSettingsV1& flash_settings, PersistedSettingsV4& out_settings) {
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
	out_settings.midi_cv_channel = MIDI_CV_CHANNEL_UNSET;
	out_settings.midi_mode = MIDI_MODE_UNSET;
	out_settings.checksum = checksum32(out_settings);
	return true;
}

bool load_settings(PersistedSettingsV4& out_settings) {
	uint8_t blob[StorageLayout::kAppDataRegionSizeBytes];
	size_t actual_size = 0;
	const StorageStatus status = read_app_blob(blob, sizeof(blob), &actual_size);

	if (status != StorageStatus::kOk || actual_size == 0) {
		return false;
	}

	if (actual_size < (sizeof(uint32_t) + sizeof(uint8_t))) {
		return false;
	}

	const uint8_t version = blob[sizeof(uint32_t)];
	switch (version) {
		case SETTINGS_VERSION_V4: {
			if (actual_size != sizeof(PersistedSettingsV4)) {
				return false;
			}
			PersistedSettingsV4 stored{};
			memcpy(&stored, blob, sizeof(stored));
			return load_v4(stored, out_settings);
		}
		case SETTINGS_VERSION_V3: {
			if (actual_size != sizeof(PersistedSettingsV3)) {
				return false;
			}
			PersistedSettingsV3 stored{};
			memcpy(&stored, blob, sizeof(stored));
			return load_v3(stored, out_settings);
		}
		case SETTINGS_VERSION_V2: {
			if (actual_size != sizeof(PersistedSettingsV2)) {
				return false;
			}
			PersistedSettingsV2 stored{};
			memcpy(&stored, blob, sizeof(stored));
			return load_v2(stored, out_settings);
		}
		case SETTINGS_VERSION_V1: {
			if (actual_size != sizeof(PersistedSettingsV1)) {
				return false;
			}
			PersistedSettingsV1 stored{};
			memcpy(&stored, blob, sizeof(stored));
			return load_v1(stored, out_settings);
		}
		default:
			return false;
	}
}

void ensure_shadow_loaded() {
	if (deferred_write_state.shadow_loaded) {
		return;
	}

	PersistedSettingsV4 loaded = default_settings();
	load_settings(loaded);
	deferred_write_state.shadow_settings = loaded;
	deferred_write_state.shadow_loaded = true;
}

bool write_settings(const PersistedSettingsV4& settings) {
	PersistedSettingsV4 to_write = settings;
	to_write.magic = SETTINGS_MAGIC;
	to_write.version = SETTINGS_VERSION_V4;
	to_write.checksum = checksum32(to_write);

	const StorageStatus status = write_app_blob(&to_write, sizeof(to_write));
	if (status != StorageStatus::kOk) {
		return false;
	}

	PersistedSettingsV4 verify{};
	if (!load_settings(verify)) {
		return false;
	}

	return verify.midi_channel == to_write.midi_channel
		&& verify.app_mode == to_write.app_mode
		&& verify.root_note == to_write.root_note
		&& verify.midi_cv_channel == to_write.midi_cv_channel
		&& verify.midi_mode == to_write.midi_mode;
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

bool load_persisted_midi_cv_channel(uint8_t& out_cv_channel) {
	ensure_shadow_loaded();
	out_cv_channel = deferred_write_state.shadow_settings.midi_cv_channel;
	return true;
}

bool save_persisted_midi_cv_channel(uint8_t cv_channel) {
	if (!is_valid_midi_cv_channel(cv_channel)) {
		return false;
	}
	ensure_shadow_loaded();
	if (deferred_write_state.shadow_settings.midi_cv_channel == cv_channel) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_cv_channel = cv_channel;
	deferred_write_state.shadow_settings.checksum = checksum32(deferred_write_state.shadow_settings);
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_midi_mode(uint8_t& out_mode) {
	ensure_shadow_loaded();
	out_mode = deferred_write_state.shadow_settings.midi_mode;
	return true;
}

bool save_persisted_midi_mode(uint8_t mode) {
	if (!is_valid_midi_mode(mode)) {
		return false;
	}
	ensure_shadow_loaded();
	if (deferred_write_state.shadow_settings.midi_mode == mode) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_mode = mode;
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
