#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include <cstdint>

class Storage;

bool load_persisted_midi_channel(Storage& storage, uint8_t& out_channel);
bool save_persisted_midi_channel(Storage& storage, uint8_t channel);
bool load_persisted_midi_cv_channel(Storage& storage, uint8_t& out_cv_channel);
bool save_persisted_midi_cv_channel(Storage& storage, uint8_t cv_channel);
bool load_persisted_midi_mode(Storage& storage, uint8_t& out_mode);
bool save_persisted_midi_mode(Storage& storage, uint8_t mode);
bool load_persisted_app_mode(Storage& storage, uint8_t& out_mode);
bool save_persisted_app_mode(Storage& storage, uint8_t mode);
bool load_persisted_root_note(Storage& storage, uint8_t& out_root_note);
bool save_persisted_root_note(Storage& storage, uint8_t root_note);
bool service_persisted_settings(Storage& storage, bool allow_commit);

#endif
