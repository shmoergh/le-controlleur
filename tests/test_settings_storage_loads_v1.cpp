#include <cassert>
#include <cstdint>

#include "settings-storage-test-helpers.h"
#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();
	Storage storage;

	// Legacy payload format should now be treated as unsupported.
	TestPersistedSettingsV1 v1{};
	v1.version = 1u;
	v1.midi_channel = 9u;
	v1.reserved[0] = 0u;
	v1.reserved[1] = 0u;
	v1.checksum = 0u;

	storage_mock::set_read_blob(&v1, sizeof(v1));

	uint8_t value = 0;

	assert(load_persisted_midi_channel(storage, value));
	assert(value == 1u);

	assert(load_persisted_app_mode(storage, value));
	assert(value == 1u);

	assert(load_persisted_root_note(storage, value));
	assert(value == 0u);

	assert(load_persisted_midi_cv_channel(storage, value));
	assert(value == 1u);

	assert(load_persisted_midi_mode(storage, value));
	assert(value == 0u);

	assert(service_persisted_settings(storage, true));
	assert(storage_mock::write_count() == 0u);

	return 0;
}
