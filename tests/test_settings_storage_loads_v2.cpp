#include <cassert>
#include <cstdint>

#include "settings-storage-test-helpers.h"
#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();

	TestPersistedSettingsV2 v2{};
	v2.magic = kSettingsMagic;
	v2.version = 2u;
	v2.midi_channel = 14u;
	v2.app_mode = 0u;
	v2.reserved[0] = 0u;
	v2.checksum = test_checksum32(v2);

	storage_mock::set_read_blob(&v2, sizeof(v2));

	uint8_t value = 0;

	assert(load_persisted_midi_channel(value));
	assert(value == 14u);

	assert(load_persisted_app_mode(value));
	assert(value == 0u);

	assert(load_persisted_root_note(value));
	assert(value == 0u);

	assert(load_persisted_midi_cv_channel(value));
	assert(value == 0xFFu);

	assert(load_persisted_midi_mode(value));
	assert(value == 0xFFu);

	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 0u);

	return 0;
}
