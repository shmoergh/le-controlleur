#include <cassert>
#include <cstdint>

#include "settings-storage.h"
#include "storage-mock.h"

int main() {
	storage_mock::reset();
	Storage storage;

	assert(!save_persisted_midi_channel(storage, 0u));
	assert(!save_persisted_midi_channel(storage, 17u));
	assert(!save_persisted_app_mode(storage, 2u));
	assert(!save_persisted_root_note(storage, 12u));
	assert(!save_persisted_midi_cv_channel(storage, 2u));
	assert(!save_persisted_midi_mode(storage, 4u));

	assert(storage_mock::write_count() == 0u);
	assert(service_persisted_settings(storage, true));
	assert(storage_mock::write_count() == 0u);

	assert(save_persisted_midi_channel(storage, 3u));
	assert(save_persisted_app_mode(storage, 1u));
	assert(save_persisted_root_note(storage, 5u));
	assert(save_persisted_midi_cv_channel(storage, 1u));
	assert(save_persisted_midi_mode(storage, 1u));
	assert(service_persisted_settings(storage, true));
	assert(storage_mock::write_count() == 1u);

	return 0;
}
