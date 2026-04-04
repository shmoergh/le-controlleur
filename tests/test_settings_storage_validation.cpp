#include <cassert>
#include <cstdint>

#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();

	assert(!save_persisted_midi_channel(0u));
	assert(!save_persisted_midi_channel(17u));
	assert(!save_persisted_app_mode(2u));
	assert(!save_persisted_root_note(12u));
	assert(!save_persisted_midi_cv_channel(2u));
	assert(!save_persisted_midi_mode(4u));

	assert(storage_mock::write_count() == 0u);
	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 0u);

	assert(save_persisted_midi_channel(3u));
	assert(save_persisted_app_mode(1u));
	assert(save_persisted_root_note(5u));
	assert(save_persisted_midi_cv_channel(1u));
	assert(save_persisted_midi_mode(1u));
	assert(service_persisted_settings(true));
	assert(storage_mock::write_count() == 1u);

	return 0;
}
