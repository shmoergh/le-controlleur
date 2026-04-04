#include <cassert>
#include <cstdint>

#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();

	uint8_t value = 0;

	assert(load_persisted_midi_channel(value));
	assert(value == 1u);

	assert(load_persisted_app_mode(value));
	assert(value == 1u);

	assert(load_persisted_root_note(value));
	assert(value == 0u);

	assert(load_persisted_midi_cv_channel(value));
	assert(value == 1u);

	assert(load_persisted_midi_mode(value));
	assert(value == 0u);

	assert(service_persisted_settings(false));
	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 0u);

	return 0;
}
