#include <cassert>
#include <cstdint>

#include "settings-storage-test-helpers.h"
#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();

	assert(save_persisted_midi_channel(7u));
	assert(save_persisted_app_mode(0u));
	assert(save_persisted_root_note(9u));
	assert(save_persisted_midi_cv_channel(0u));
	assert(save_persisted_midi_mode(3u));

	assert(storage_mock::write_count() == 0u);
	assert(service_persisted_settings(false));
	assert(storage_mock::write_count() == 0u);

	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 1u);

	const TestPersistedSettingsV4 first_write =
		decode_settings_blob<TestPersistedSettingsV4>(storage_mock::persisted_blob());
	assert(first_write.magic == kSettingsMagic);
	assert(first_write.version == 4u);
	assert(first_write.midi_channel == 7u);
	assert(first_write.app_mode == 0u);
	assert(first_write.root_note == 9u);
	assert(first_write.midi_cv_channel == 0u);
	assert(first_write.midi_mode == 3u);
	assert(first_write.checksum == test_checksum32(first_write));

	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 1u);

	assert(save_persisted_midi_channel(7u));
	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 1u);

	assert(save_persisted_midi_channel(8u));
	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 2u);

	const TestPersistedSettingsV4 second_write =
		decode_settings_blob<TestPersistedSettingsV4>(storage_mock::persisted_blob());
	assert(second_write.midi_channel == 8u);
	assert(second_write.checksum == test_checksum32(second_write));

	return 0;
}
