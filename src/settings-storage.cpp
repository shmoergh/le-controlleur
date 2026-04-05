#include "settings-storage.h"

#include "brain/include/storage.h"

namespace {

constexpr uint8_t SETTINGS_SCHEMA_VERSION = 1;
constexpr uint8_t APP_MODE_MIDI_TO_CV = 0;
constexpr uint8_t APP_MODE_SEQUENCER = 1;
constexpr uint8_t ROOT_NOTE_MIN = 0;
constexpr uint8_t ROOT_NOTE_MAX = 11;
constexpr uint8_t MIDI_CV_CHANNEL_A = 0;
constexpr uint8_t MIDI_CV_CHANNEL_B = 1;
constexpr uint8_t MIDI_MODE_DEFAULT = 0;
constexpr uint8_t MIDI_MODE_MODWHEEL = 1;
constexpr uint8_t MIDI_MODE_UNISON = 2;
constexpr uint8_t MIDI_MODE_DUO = 3;

struct PersistedSettings {
	uint8_t schema_version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint8_t midi_cv_channel;
	uint8_t midi_mode;
};

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

bool is_valid_settings(const PersistedSettings& settings) {
	return settings.schema_version == SETTINGS_SCHEMA_VERSION
		&& is_valid_midi_channel(settings.midi_channel)
		&& is_valid_app_mode(settings.app_mode)
		&& is_valid_root_note(settings.root_note)
		&& is_valid_midi_cv_channel(settings.midi_cv_channel)
		&& is_valid_midi_mode(settings.midi_mode);
}

PersistedSettings default_settings() {
	PersistedSettings settings{};
	settings.schema_version = SETTINGS_SCHEMA_VERSION;
	settings.midi_channel = 1;
	settings.app_mode = APP_MODE_SEQUENCER;
	settings.root_note = 0;
	settings.midi_cv_channel = MIDI_CV_CHANNEL_B;
	settings.midi_mode = MIDI_MODE_DEFAULT;
	return settings;
}

struct DeferredWriteState {
	PersistedSettings shadow_settings;
	bool shadow_loaded;
	bool dirty;
};

DeferredWriteState deferred_write_state = {default_settings(), false, false};

bool ensure_storage_initialized(Storage& storage) {
	if (storage.is_initialized()) {
		return true;
	}
	return storage.init(true);
}

bool load_settings(Storage& storage, PersistedSettings& out_settings) {
	if (!ensure_storage_initialized(storage)) {
		return false;
	}

	PersistedSettings stored{};
	size_t actual_size = 0;
	const StorageStatus status = storage.read_app_blob(&stored, sizeof(stored), &actual_size);

	if (status != StorageStatus::kOk) {
		return false;
	}
	if (actual_size != sizeof(PersistedSettings)) {
		return false;
	}
	if (!is_valid_settings(stored)) {
		return false;
	}
	out_settings = stored;
	return true;
}

void ensure_shadow_loaded(Storage& storage) {
	if (deferred_write_state.shadow_loaded) {
		return;
	}

	PersistedSettings loaded = default_settings();
	load_settings(storage, loaded);
	deferred_write_state.shadow_settings = loaded;
	deferred_write_state.shadow_loaded = true;
}

bool write_settings(Storage& storage, const PersistedSettings& settings) {
	if (!ensure_storage_initialized(storage)) {
		return false;
	}

	PersistedSettings to_write = settings;
	to_write.schema_version = SETTINGS_SCHEMA_VERSION;
	return storage.write_app_blob(&to_write, sizeof(to_write)) == StorageStatus::kOk;
}

}  // namespace

bool load_persisted_midi_channel(Storage& storage, uint8_t& out_channel) {
	ensure_shadow_loaded(storage);
	out_channel = deferred_write_state.shadow_settings.midi_channel;
	return true;
}

bool save_persisted_midi_channel(Storage& storage, uint8_t channel) {
	if (!is_valid_midi_channel(channel)) {
		return false;
	}
	ensure_shadow_loaded(storage);
	if (deferred_write_state.shadow_settings.midi_channel == channel) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_channel = channel;
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_midi_cv_channel(Storage& storage, uint8_t& out_cv_channel) {
	ensure_shadow_loaded(storage);
	out_cv_channel = deferred_write_state.shadow_settings.midi_cv_channel;
	return true;
}

bool save_persisted_midi_cv_channel(Storage& storage, uint8_t cv_channel) {
	if (!is_valid_midi_cv_channel(cv_channel)) {
		return false;
	}
	ensure_shadow_loaded(storage);
	if (deferred_write_state.shadow_settings.midi_cv_channel == cv_channel) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_cv_channel = cv_channel;
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_midi_mode(Storage& storage, uint8_t& out_mode) {
	ensure_shadow_loaded(storage);
	out_mode = deferred_write_state.shadow_settings.midi_mode;
	return true;
}

bool save_persisted_midi_mode(Storage& storage, uint8_t mode) {
	if (!is_valid_midi_mode(mode)) {
		return false;
	}
	ensure_shadow_loaded(storage);
	if (deferred_write_state.shadow_settings.midi_mode == mode) {
		return true;
	}
	deferred_write_state.shadow_settings.midi_mode = mode;
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_app_mode(Storage& storage, uint8_t& out_mode) {
	ensure_shadow_loaded(storage);
	out_mode = deferred_write_state.shadow_settings.app_mode;
	return true;
}

bool save_persisted_app_mode(Storage& storage, uint8_t mode) {
	if (!is_valid_app_mode(mode)) {
		return false;
	}
	ensure_shadow_loaded(storage);
	if (deferred_write_state.shadow_settings.app_mode == mode) {
		return true;
	}
	deferred_write_state.shadow_settings.app_mode = mode;
	deferred_write_state.dirty = true;
	return true;
}

bool load_persisted_root_note(Storage& storage, uint8_t& out_root_note) {
	ensure_shadow_loaded(storage);
	out_root_note = deferred_write_state.shadow_settings.root_note;
	return true;
}

bool save_persisted_root_note(Storage& storage, uint8_t root_note) {
	if (!is_valid_root_note(root_note)) {
		return false;
	}
	ensure_shadow_loaded(storage);
	if (deferred_write_state.shadow_settings.root_note == root_note) {
		return true;
	}
	deferred_write_state.shadow_settings.root_note = root_note;
	deferred_write_state.dirty = true;
	return true;
}

bool service_persisted_settings(Storage& storage, bool allow_commit) {
	if (!allow_commit) {
		return true;
	}

	ensure_shadow_loaded(storage);
	if (!deferred_write_state.dirty) {
		return true;
	}

	if (!write_settings(storage, deferred_write_state.shadow_settings)) {
		return false;
	}

	deferred_write_state.dirty = false;
	return true;
}
