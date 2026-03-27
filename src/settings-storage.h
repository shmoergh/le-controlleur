#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include <cstdint>

bool load_persisted_midi_channel(uint8_t& out_channel);
bool save_persisted_midi_channel(uint8_t channel);

#endif
