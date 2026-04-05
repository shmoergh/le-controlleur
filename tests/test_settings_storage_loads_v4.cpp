#include <cassert>
#include <cstdint>

#include "settings-storage-test-helpers.h"
#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();
	Storage storage;

	TestPersistedSettingsCurrent settings{};
	settings.schema_version = kSettingsSchemaVersion;
	settings.midi_channel = 12u;
	settings.app_mode = 0u;
	settings.root_note = 11u;
	settings.midi_cv_channel = 0u;
	settings.midi_mode = 2u;

	storage_mock::set_read_blob(&settings, sizeof(settings));

	uint8_t value = 0;

	assert(load_persisted_midi_channel(storage, value));
	assert(value == 12u);

	assert(load_persisted_app_mode(storage, value));
	assert(value == 0u);

	assert(load_persisted_root_note(storage, value));
	assert(value == 11u);

	assert(load_persisted_midi_cv_channel(storage, value));
	assert(value == 0u);

	assert(load_persisted_midi_mode(storage, value));
	assert(value == 2u);

	return 0;
}
