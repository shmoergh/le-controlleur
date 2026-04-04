#ifndef SETTINGS_STORAGE_TEST_HELPERS_H
#define SETTINGS_STORAGE_TEST_HELPERS_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr uint32_t kSettingsMagic = 0x4C435452;  // "LCTR"

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

struct TestPersistedSettingsV3 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint32_t checksum;
};

struct TestPersistedSettingsV4 {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t app_mode;
	uint8_t root_note;
	uint8_t midi_cv_channel;
	uint8_t midi_mode;
	uint32_t checksum;
};

template <typename TSettings>
uint32_t test_checksum32(const TSettings& settings_without_checksum) {
	const auto* bytes = reinterpret_cast<const uint8_t*>(&settings_without_checksum);
	const size_t length = sizeof(TSettings) - sizeof(settings_without_checksum.checksum);

	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < length; ++i) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

template <typename TSettings>
TSettings decode_settings_blob(const std::vector<uint8_t>& blob) {
	assert(blob.size() == sizeof(TSettings));
	TSettings settings{};
	std::memcpy(&settings, blob.data(), sizeof(TSettings));
	return settings;
}

#endif
