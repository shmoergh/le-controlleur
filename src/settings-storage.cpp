#include "settings-storage.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace {

constexpr uint32_t SETTINGS_MAGIC = 0x4C435452;  // "LCTR"
constexpr uint8_t SETTINGS_VERSION = 1;

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

constexpr uint32_t SETTINGS_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct PersistedSettings {
	uint32_t magic;
	uint8_t version;
	uint8_t midi_channel;
	uint8_t reserved[2];
	uint32_t checksum;
};

uint32_t checksum32(const PersistedSettings& settings_without_checksum) {
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&settings_without_checksum);
	const size_t length = sizeof(PersistedSettings) - sizeof(settings_without_checksum.checksum);

	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < length; ++i) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

bool is_valid_midi_channel(uint8_t channel) {
	return channel >= 1 && channel <= 16;
}

}  // namespace

bool load_persisted_midi_channel(uint8_t& out_channel) {
	const auto* flash_settings =
		reinterpret_cast<const PersistedSettings*>(XIP_BASE + SETTINGS_FLASH_OFFSET);

	if (flash_settings->magic != SETTINGS_MAGIC || flash_settings->version != SETTINGS_VERSION) {
		return false;
	}

	if (!is_valid_midi_channel(flash_settings->midi_channel)) {
		return false;
	}

	PersistedSettings candidate = *flash_settings;
	const uint32_t expected = checksum32(candidate);
	if (expected != flash_settings->checksum) {
		return false;
	}

	out_channel = flash_settings->midi_channel;
	return true;
}

bool save_persisted_midi_channel(uint8_t channel) {
	if (!is_valid_midi_channel(channel)) {
		return false;
	}

	PersistedSettings settings{};
	settings.magic = SETTINGS_MAGIC;
	settings.version = SETTINGS_VERSION;
	settings.midi_channel = channel;
	settings.reserved[0] = 0;
	settings.reserved[1] = 0;
	settings.checksum = checksum32(settings);

	uint8_t page_buffer[FLASH_PAGE_SIZE];
	memset(page_buffer, 0xFF, sizeof(page_buffer));
	memcpy(page_buffer, &settings, sizeof(settings));

	uint32_t interrupts = save_and_disable_interrupts();
	flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(SETTINGS_FLASH_OFFSET, page_buffer, FLASH_PAGE_SIZE);
	restore_interrupts(interrupts);

	uint8_t verify_channel = 0;
	return load_persisted_midi_channel(verify_channel) && verify_channel == channel;
}
