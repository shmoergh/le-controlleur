#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include <cstdint>

bool load_persisted_midi_channel(uint8_t& out_channel);
bool save_persisted_midi_channel(uint8_t channel);
bool load_persisted_app_mode(uint8_t& out_mode);
bool save_persisted_app_mode(uint8_t mode);
bool load_persisted_root_note(uint8_t& out_root_note);
bool save_persisted_root_note(uint8_t root_note);
bool service_persisted_settings(bool allow_commit);

#endif
