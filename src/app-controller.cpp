#include "app-controller.h"

#include <cstring>
#include <stdio.h>

AppController::AppController() :
	button_a_(GPIO_BRAIN_BUTTON_1),
	button_b_(GPIO_BRAIN_BUTTON_2),
	mode_(AppMode::kMidiToCv),
	midi_to_cv_engine_(brain::io::AudioCvOutChannel::kChannelB, 1),
	button_a_pressed_(false),
	button_b_pressed_(false),
	button_a_pending_single_press_(false),
	button_b_pending_single_press_(false),
	button_a_single_press_dispatched_(false),
	button_b_single_press_dispatched_(false),
	dual_button_active_(false),
	dual_button_action_handled_(false),
	first_button_pressed_at_(0),
	dual_button_started_at_(0),
	status_has_text_(false),
	status_line_count_(0),
	status_text_{0} {
	button_a_.init();
	button_b_.init();

	button_a_.set_on_press([this]() { on_button_a_press(); });
	button_a_.set_on_release([this]() { on_button_a_release(); });
	button_b_.set_on_press([this]() { on_button_b_press(); });
	button_b_.set_on_release([this]() { on_button_b_release(); });
}

void AppController::update() {
	button_a_.update();
	button_b_.update();

	check_toggle_mode();

	switch (mode_) {
		case AppMode::kMidiToCv: {
			check_dispatch_pending_single_button_presses(get_absolute_time());
			midi_to_cv_engine_.update();
			break;
		}
		case AppMode::kSequencer:
			sequencer_engine_.update();
			break;
	}

	render_status_block();
}

AppMode AppController::mode() const {
	return mode_;
}

void AppController::on_button_a_press() {
	button_a_pressed_ = true;
	absolute_time_t now = get_absolute_time();

	if (!button_b_pressed_) {
		first_button_pressed_at_ = now;
		button_a_pending_single_press_ = true;
		return;
	}

	if (first_button_pressed_at_ == 0) {
		first_button_pressed_at_ = now;
	}

	int64_t delta_us = absolute_time_diff_us(first_button_pressed_at_, now);
	if (delta_us <= BUTTON_DUAL_PRESS_WINDOW_US) {
		start_dual_button_press(now);
		cancel_pending_single_button_presses();
		return;
	}

	button_a_pending_single_press_ = true;
}

void AppController::on_button_a_release() {
	button_a_pressed_ = false;
	check_dispatch_single_button_release(true);

	if (!button_a_pressed_ && !button_b_pressed_) {
		check_handle_short_dual_button_on_release(get_absolute_time());
		clear_dual_button_tracking();
	}
}

void AppController::on_button_b_press() {
	button_b_pressed_ = true;
	absolute_time_t now = get_absolute_time();

	if (!button_a_pressed_) {
		if (mode_ == AppMode::kSequencer) {
			sequencer_engine_.on_button_b_press();
		}
		first_button_pressed_at_ = now;
		button_b_pending_single_press_ = true;
		return;
	}

	if (first_button_pressed_at_ == 0) {
		first_button_pressed_at_ = now;
	}

	int64_t delta_us = absolute_time_diff_us(first_button_pressed_at_, now);
	if (delta_us <= BUTTON_DUAL_PRESS_WINDOW_US) {
		start_dual_button_press(now);
		cancel_pending_single_button_presses();
		return;
	}

	button_b_pending_single_press_ = true;
}

void AppController::on_button_b_release() {
	button_b_pressed_ = false;
	check_dispatch_single_button_release(false);

	if (!button_a_pressed_ && !button_b_pressed_) {
		check_handle_short_dual_button_on_release(get_absolute_time());
		clear_dual_button_tracking();
	}
}

void AppController::start_dual_button_press(absolute_time_t started_at) {
	if (dual_button_active_) {
		return;
	}

	if (mode_ == AppMode::kSequencer && button_b_pressed_) {
		sequencer_engine_.on_button_b_release();
	}

	cancel_pending_single_button_presses();
	dual_button_active_ = true;
	dual_button_action_handled_ = false;
	dual_button_started_at_ = started_at;
}

void AppController::check_dispatch_pending_single_button_presses(absolute_time_t now) {
	if (dual_button_active_ || first_button_pressed_at_ == 0) {
		return;
	}

	if (absolute_time_diff_us(first_button_pressed_at_, now) < BUTTON_DUAL_PRESS_WINDOW_US) {
		return;
	}

	if (button_a_pending_single_press_ && !button_a_single_press_dispatched_) {
		midi_to_cv_engine_.on_button_a_press();
		button_a_single_press_dispatched_ = true;
		button_a_pending_single_press_ = false;
	}

	if (button_b_pending_single_press_ && !button_b_single_press_dispatched_) {
		midi_to_cv_engine_.on_button_b_press();
		button_b_single_press_dispatched_ = true;
		button_b_pending_single_press_ = false;
	}
}

void AppController::check_dispatch_single_button_release(bool is_button_a) {
	if (dual_button_active_) {
		return;
	}

	if (mode_ == AppMode::kSequencer) {
		if (!is_button_a) {
			sequencer_engine_.on_button_b_release();
			button_b_pending_single_press_ = false;
			button_b_single_press_dispatched_ = false;
			return;
		}

		const absolute_time_t now = get_absolute_time();
		const int64_t held_us = (first_button_pressed_at_ == 0) ? 0 : absolute_time_diff_us(first_button_pressed_at_, now);
		if (held_us <= BUTTON_SHORT_PRESS_MAX_US) {
			sequencer_engine_.on_button_a_short_press();
		}
		button_a_pending_single_press_ = false;
		button_a_single_press_dispatched_ = false;
		return;
	}

	if (mode_ != AppMode::kMidiToCv) {
		return;
	}

	if (is_button_a) {
		if (button_a_pending_single_press_ && !button_a_single_press_dispatched_) {
			midi_to_cv_engine_.on_button_a_press();
			button_a_single_press_dispatched_ = true;
			button_a_pending_single_press_ = false;
		}

		if (button_a_single_press_dispatched_) {
			midi_to_cv_engine_.on_button_a_release();
			button_a_single_press_dispatched_ = false;
		}
		return;
	}

	if (button_b_pending_single_press_ && !button_b_single_press_dispatched_) {
		midi_to_cv_engine_.on_button_b_press();
		button_b_single_press_dispatched_ = true;
		button_b_pending_single_press_ = false;
	}

	if (button_b_single_press_dispatched_) {
		midi_to_cv_engine_.on_button_b_release();
		button_b_single_press_dispatched_ = false;
	}
}

void AppController::cancel_pending_single_button_presses() {
	button_a_pending_single_press_ = false;
	button_b_pending_single_press_ = false;
	button_a_single_press_dispatched_ = false;
	button_b_single_press_dispatched_ = false;
}

void AppController::clear_dual_button_tracking() {
	dual_button_active_ = false;
	dual_button_action_handled_ = false;
	first_button_pressed_at_ = 0;
	dual_button_started_at_ = 0;
	cancel_pending_single_button_presses();
}

void AppController::check_toggle_mode() {
	if (!dual_button_active_ || dual_button_action_handled_ || dual_button_started_at_ == 0) {
		return;
	}

	absolute_time_t now = get_absolute_time();
	int64_t held_us = absolute_time_diff_us(dual_button_started_at_, now);
	if (held_us < BUTTON_LONG_PRESS_MIN_US) {
		return;
	}
	int64_t held_ms = held_us / 1000;

	if (mode_ == AppMode::kMidiToCv) {
		set_mode(AppMode::kSequencer);
	} else {
		set_mode(AppMode::kMidiToCv);
	}
	(void) held_ms;

	dual_button_action_handled_ = true;
}

void AppController::check_handle_short_dual_button_on_release(absolute_time_t released_at) {
	if (!dual_button_active_ || dual_button_action_handled_ || dual_button_started_at_ == 0) {
		return;
	}

	int64_t held_us = absolute_time_diff_us(dual_button_started_at_, released_at);
	if (held_us > BUTTON_SHORT_PRESS_MAX_US) {
		return;
	}
	int64_t held_ms = held_us / 1000;

	if (mode_ == AppMode::kMidiToCv) {
		midi_to_cv_engine_.panic();
	}
	(void) held_ms;

	dual_button_action_handled_ = true;
}

void AppController::render_status_block() {
	char current_status[512];
	uint8_t line_count = 0;

	if (mode_ == AppMode::kMidiToCv) {
		snprintf(
			current_status,
			sizeof(current_status),
			"MODE: MIDI 2 CV\n"
			"Midi Channel: %u",
			static_cast<unsigned>(midi_to_cv_engine_.get_midi_channel())
		);
		line_count = 2;
	} else {
		snprintf(
			current_status,
			sizeof(current_status),
			"MODE: SEQUENCER\n"
			"Tempo: %u\n"
			"Swing: %.1f%%\n"
			"Randomness: %.2f\n"
			"Sequence Length: %u\n"
			"Octave Range: %u\n"
			"Quantization: %s\n"
			"Voltage: %.2f > %.2f\n"
			"Timing: base=%luus current=%luus",
			static_cast<unsigned>(sequencer_engine_.tempo_bpm()),
			sequencer_engine_.swing() * 100.0f,
			sequencer_engine_.randomness(),
			static_cast<unsigned>(sequencer_engine_.sequence_length()),
			static_cast<unsigned>(sequencer_engine_.range_octaves()),
			sequencer_engine_.quantization_mode_name(),
			sequencer_engine_.last_raw_voltage(),
			sequencer_engine_.last_quantized_voltage(),
			static_cast<unsigned long>(sequencer_engine_.base_interval_us()),
			static_cast<unsigned long>(sequencer_engine_.current_interval_us())
		);
		line_count = 9;
	}

	if (status_has_text_ && strcmp(current_status, status_text_) == 0) {
		return;
	}

	snprintf(status_text_, sizeof(status_text_), "%s", current_status);
	status_has_text_ = true;
	if (status_line_count_ == 0) {
		status_line_count_ = line_count;
	}

	printf("\r");
	if (status_line_count_ > 1) {
		printf("\033[%uA", static_cast<unsigned>(status_line_count_ - 1));
	}
	printf("\033[J%s", status_text_);
	fflush(stdout);
	status_line_count_ = line_count;
}

void AppController::set_mode(AppMode mode) {
	if (mode_ == mode) {
		return;
	}

	if (mode_ == AppMode::kSequencer) {
		sequencer_engine_.on_mode_exit();
	}

	cancel_pending_single_button_presses();
	mode_ = mode;

	if (mode_ == AppMode::kSequencer) {
		sequencer_engine_.on_mode_enter();
	}

	midi_to_cv_engine_.play_startup_animation();
}
