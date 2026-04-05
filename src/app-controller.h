#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <pico/stdlib.h>

#include "brain/brain.h"
#include "midi-to-cv-engine.h"
#include "sequencer-engine.h"

constexpr int64_t BUTTON_SHORT_PRESS_MAX_US = 400LL * 1000LL;
constexpr int64_t BUTTON_LONG_PRESS_MIN_US = 1500LL * 1000LL;
constexpr int64_t BUTTON_DUAL_PRESS_WINDOW_US = 120LL * 1000LL;
constexpr int64_t BUTTON_DOUBLE_TAP_WINDOW_US = 350LL * 1000LL;

enum class AppMode {
	kMidiToCv = 0,
	kSequencer = 1
};

class AppController {
public:
	AppController();
	void update();
	AppMode mode() const;

private:
	Brain brain_;
	AppMode mode_;
	MidiToCVEngine midi_to_cv_engine_;
	SequencerEngine sequencer_engine_;
	// Dedicated parser for sequencer realtime/root-edit so the Brain-owned
	// midi_to_cv parser callbacks remain untouched in MIDI2CV mode.
	MidiParser sequencer_midi_parser_;
	bool sequencer_midi_parser_initialized_;
	bool button_a_pressed_;
	bool button_b_pressed_;
	bool button_a_pending_single_press_;
	bool button_b_pending_single_press_;
	bool button_a_single_press_dispatched_;
	bool button_b_single_press_dispatched_;
	bool dual_button_active_;
	bool dual_button_action_handled_;
	bool sequencer_button_b_waiting_second_tap_;
	bool sequencer_root_edit_hold_active_;
	absolute_time_t first_button_pressed_at_;
	absolute_time_t dual_button_started_at_;
	absolute_time_t sequencer_button_b_last_release_at_;
	bool status_has_text_;
	uint8_t status_line_count_;
	char status_text_[512];
	static AppController* instance_;

	void on_button_a_press();
	void on_button_a_release();
	void on_button_b_press();
	void on_button_b_release();
	void start_dual_button_press(absolute_time_t started_at);
	void check_dispatch_pending_single_button_presses(absolute_time_t now);
	void check_dispatch_single_button_release(bool is_button_a);
	void cancel_pending_single_button_presses();
	void clear_dual_button_tracking();
	void check_toggle_mode();
	void check_handle_short_dual_button_on_release(absolute_time_t released_at);
	void ensure_sequencer_midi_parser_initialized();
	void update_sequencer_midi_realtime();
	static void on_sequencer_midi_realtime(uint8_t status);
	void handle_sequencer_midi_realtime(uint8_t status);
	static void on_sequencer_midi_note_on(uint8_t note, uint8_t velocity, uint8_t channel);
	void handle_sequencer_midi_note_on(uint8_t note, uint8_t velocity, uint8_t channel);
	void render_status_block();
	void set_mode(AppMode mode);
};

#endif
