#include "app-controller.h"

#include "debug-log.h"

AppController::AppController() :
	button_a_(GPIO_BRAIN_BUTTON_1),
	button_b_(GPIO_BRAIN_BUTTON_2),
	mode_(AppMode::kMidiToCv),
	midi_to_cv_engine_(brain::io::AudioCvOutChannel::kChannelB, 1),
	button_a_pressed_(false),
	button_b_pressed_(false),
	chord_active_(false),
	chord_action_handled_(false),
	first_button_pressed_at_(0),
	chord_started_at_(0) {
	button_a_.init();
	button_b_.init();

	button_a_.set_on_press([this]() { on_button_a_press(); });
	button_a_.set_on_release([this]() { on_button_a_release(); });
	button_b_.set_on_press([this]() { on_button_b_press(); });
	button_b_.set_on_release([this]() { on_button_b_release(); });

	LOG_INFO("APP", "mode=MIDI2CV");
}

void AppController::update() {
	button_a_.update();
	button_b_.update();

	maybe_toggle_mode();

	if (chord_active_) {
		return;
	}

	// Keep a short window to detect potential two-button chords before
	// forwarding single-button presses to the MIDI2CV engine.
	if (mode_ == AppMode::kMidiToCv && (button_a_pressed_ ^ button_b_pressed_) && first_button_pressed_at_ != 0) {
		absolute_time_t now = get_absolute_time();
		int64_t elapsed_ms = absolute_time_diff_us(first_button_pressed_at_, now) / 1000;
		if (elapsed_ms < static_cast<int64_t>(BUTTON_CHORD_WINDOW_MS)) {
			return;
		}
	}

	switch (mode_) {
		case AppMode::kMidiToCv:
			midi_to_cv_engine_.update();
			break;
		case AppMode::kSequencer:
			// Sequencer routing will be implemented in the next phase.
			break;
	}
}

AppMode AppController::mode() const {
	return mode_;
}

void AppController::on_button_a_press() {
	button_a_pressed_ = true;
	absolute_time_t now = get_absolute_time();

	if (!button_b_pressed_) {
		first_button_pressed_at_ = now;
		return;
	}

	if (first_button_pressed_at_ == 0) {
		first_button_pressed_at_ = now;
	}

	int64_t delta_ms = absolute_time_diff_us(first_button_pressed_at_, now) / 1000;
	if (delta_ms <= static_cast<int64_t>(BUTTON_CHORD_WINDOW_MS)) {
		start_chord(now);
	}
}

void AppController::on_button_a_release() {
	button_a_pressed_ = false;

	if (!button_a_pressed_ && !button_b_pressed_) {
		maybe_handle_short_chord_on_release(get_absolute_time());
		clear_chord_tracking();
	}
}

void AppController::on_button_b_press() {
	button_b_pressed_ = true;
	absolute_time_t now = get_absolute_time();

	if (!button_a_pressed_) {
		first_button_pressed_at_ = now;
		return;
	}

	if (first_button_pressed_at_ == 0) {
		first_button_pressed_at_ = now;
	}

	int64_t delta_ms = absolute_time_diff_us(first_button_pressed_at_, now) / 1000;
	if (delta_ms <= static_cast<int64_t>(BUTTON_CHORD_WINDOW_MS)) {
		start_chord(now);
	}
}

void AppController::on_button_b_release() {
	button_b_pressed_ = false;

	if (!button_a_pressed_ && !button_b_pressed_) {
		maybe_handle_short_chord_on_release(get_absolute_time());
		clear_chord_tracking();
	}
}

void AppController::start_chord(absolute_time_t started_at) {
	if (chord_active_) {
		return;
	}

	chord_active_ = true;
	chord_action_handled_ = false;
	chord_started_at_ = started_at;
	LOG_TRACE("CTRL", "button-chord-started");
}

void AppController::clear_chord_tracking() {
	chord_active_ = false;
	chord_action_handled_ = false;
	first_button_pressed_at_ = 0;
	chord_started_at_ = 0;
}

void AppController::maybe_toggle_mode() {
	if (!chord_active_ || chord_action_handled_ || chord_started_at_ == 0) {
		return;
	}

	absolute_time_t now = get_absolute_time();
	int64_t held_ms = absolute_time_diff_us(chord_started_at_, now) / 1000;
	if (held_ms < static_cast<int64_t>(BUTTON_LONG_PRESS_MIN_MS)) {
		return;
	}

	if (mode_ == AppMode::kMidiToCv) {
		set_mode(AppMode::kSequencer);
		LOG_INFO("CTRL", "mode-toggle long-press new_mode=SEQUENCER held_ms=%lld", held_ms);
	} else {
		set_mode(AppMode::kMidiToCv);
		LOG_INFO("CTRL", "mode-toggle long-press new_mode=MIDI2CV held_ms=%lld", held_ms);
	}

	chord_action_handled_ = true;
}

void AppController::maybe_handle_short_chord_on_release(absolute_time_t released_at) {
	if (!chord_active_ || chord_action_handled_ || chord_started_at_ == 0) {
		return;
	}

	int64_t held_ms = absolute_time_diff_us(chord_started_at_, released_at) / 1000;
	if (held_ms > static_cast<int64_t>(BUTTON_SHORT_PRESS_MAX_MS)) {
		return;
	}

	if (mode_ == AppMode::kMidiToCv) {
		midi_to_cv_engine_.panic();
		LOG_INFO("PANIC", "short-chord panic triggered held_ms=%lld", held_ms);
	} else {
		LOG_TRACE("CTRL", "short-chord ignored in sequencer mode held_ms=%lld", held_ms);
	}

	chord_action_handled_ = true;
}

void AppController::set_mode(AppMode mode) {
	if (mode_ == mode) {
		return;
	}

	LOG_INFO("APP", "mode-exit=%s", mode_ == AppMode::kMidiToCv ? "MIDI2CV" : "SEQUENCER");
	mode_ = mode;
	LOG_INFO("APP", "mode-enter=%s", mode_ == AppMode::kMidiToCv ? "MIDI2CV" : "SEQUENCER");
}
