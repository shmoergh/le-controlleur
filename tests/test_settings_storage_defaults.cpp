#include <cassert>
#include <cstdint>

#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();
	Storage storage;

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

	assert(service_persisted_settings(storage, false));
	assert(service_persisted_settings(storage, true));
	assert(storage_mock::write_count() == 0u);

	return 0;
}
