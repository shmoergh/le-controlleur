#include <cassert>
#include <cstdint>

#include "settings-storage-test-helpers.h"
#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();

	TestPersistedSettingsV4 v4{};
	v4.magic = kSettingsMagic;
	v4.version = 4u;
	v4.midi_channel = 12u;
	v4.app_mode = 0u;
	v4.root_note = 11u;
	v4.midi_cv_channel = 0u;
	v4.midi_mode = 2u;
	v4.checksum = test_checksum32(v4);

	storage_mock::set_read_blob(&v4, sizeof(v4));

	uint8_t value = 0;

	assert(load_persisted_midi_channel(value));
	assert(value == 12u);

	assert(load_persisted_app_mode(value));
	assert(value == 0u);

	assert(load_persisted_root_note(value));
	assert(value == 11u);

	assert(load_persisted_midi_cv_channel(value));
	assert(value == 0u);

	assert(load_persisted_midi_mode(value));
	assert(value == 2u);

	return 0;
}
