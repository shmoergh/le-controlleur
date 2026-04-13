#ifndef SETTINGS_STORAGE_TEST_HELPERS_H
#define SETTINGS_STORAGE_TEST_HELPERS_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr uint8_t kSettingsSchemaVersion = 1u;

struct TestPersistedSettingsV1 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t reserved[2];
	uint32_t checksum;
};

struct TestPersistedSettingsV2 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t reserved[1];
	uint32_t checksum;
};

struct TestPersistedSettingsCurrent {
	uint8_t schema_version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint8_t midi_cv_channel;
	uint8_t midi_mode;
};

template <typename TSettings>
TSettings decode_settings_blob(const std::vector<uint8_t>& blob) {
	assert(blob.size() == sizeof(TSettings));
	TSettings settings{};
	std::memcpy(&settings, blob.data(), sizeof(TSettings));
	return settings;
}

#endif
