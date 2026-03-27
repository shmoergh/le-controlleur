#include "app-controller.h"

#include <cstring>
#include <stdio.h>

#include "pico/time.h"

#include "debug-log.h"
#include "settings-storage.h"

AppController* AppController::instance_ = nullptr;

AppController::AppController() :
	button_a_(GPIO_BRAIN_BUTTON_1),
	button_b_(GPIO_BRAIN_BUTTON_2),
	mode_(AppMode::kMidiToCv),
	midi_to_cv_engine_(brain::io::AudioCvOutChannel::kChannelB, 1),
	sequencer_midi_parser_(1, true),
	sequencer_midi_parser_initialized_(false),
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
	instance_ = this;
	button_a_.init();
	button_b_.init();

	button_a_.set_on_press([this]() { on_button_a_press(); });
	button_a_.set_on_release([this]() { on_button_a_release(); });
	button_b_.set_on_press([this]() { on_button_b_press(); });
	button_b_.set_on_release([this]() { on_button_b_release(); });
	sequencer_midi_parser_.set_realtime_callback(&AppController::on_sequencer_midi_realtime);

	uint8_t persisted_mode = static_cast<uint8_t>(AppMode::kMidiToCv);
	if (load_persisted_app_mode(persisted_mode) && persisted_mode == static_cast<uint8_t>(AppMode::kSequencer)) {
		mode_ = AppMode::kSequencer;
		ensure_sequencer_midi_parser_initialized();
		sequencer_midi_parser_.reset();
		sequencer_engine_.on_mode_enter();
	}
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
			update_sequencer_midi_realtime();
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
		if (mode_ == AppMode::kSequencer) {
			sequencer_engine_.on_button_a_short_press();
			button_a_single_press_dispatched_ = true;
			button_a_pending_single_press_ = false;
			first_button_pressed_at_ = now;
			return;
		}

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

	if (mode_ == AppMode::kSequencer && button_a_single_press_dispatched_) {
		// Button A now toggles play/pause on press; if this evolves into a dual press,
		// revert the optimistic single-press action so dual-button behavior stays clean.
		sequencer_engine_.on_button_a_short_press();
		button_a_single_press_dispatched_ = false;
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

void AppController::ensure_sequencer_midi_parser_initialized() {
	if (sequencer_midi_parser_initialized_) {
		return;
	}

	sequencer_midi_parser_initialized_ = sequencer_midi_parser_.init_uart();
	if (!sequencer_midi_parser_initialized_) {
		LOG_ERROR("APP", "failed to init sequencer realtime midi parser");
	}
}

void AppController::update_sequencer_midi_realtime() {
	ensure_sequencer_midi_parser_initialized();
	if (!sequencer_midi_parser_initialized_) {
		return;
	}
	sequencer_midi_parser_.process_uart();
}

void AppController::on_sequencer_midi_realtime(uint8_t status) {
	if (instance_ == nullptr) {
		return;
	}
	instance_->handle_sequencer_midi_realtime(status);
}

void AppController::handle_sequencer_midi_realtime(uint8_t status) {
	if (mode_ != AppMode::kSequencer) {
		return;
	}

	const uint64_t now_us = time_us_64();
	switch (status) {
		case 0xF8:  // Clock
			sequencer_engine_.on_midi_clock_tick(now_us);
			break;
		case 0xFA:  // Start
			sequencer_engine_.on_midi_transport_start();
			break;
		case 0xFB:  // Continue
			sequencer_engine_.on_midi_transport_continue();
			break;
		case 0xFC:  // Stop
			sequencer_engine_.on_midi_transport_stop();
			break;
		default:
			break;
	}
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
		char tempo_text[32];
		char transport_text[24];
		transport_text[0] = '\0';
		if (sequencer_engine_.external_sync_enabled()) {
			switch (sequencer_engine_.external_clock_source()) {
				case ExternalClockSource::kExternalPulse:
					snprintf(tempo_text, sizeof(tempo_text), "%s", "EXT (Pulse In)");
					break;
				case ExternalClockSource::kExternalMidi:
					snprintf(tempo_text, sizeof(tempo_text), "%s", "EXT (MIDI Clock)");
					snprintf(
						transport_text,
						sizeof(transport_text),
						"\nTransport: %s",
						sequencer_engine_.midi_transport_running() ? "RUN" : "STOP"
					);
					break;
				case ExternalClockSource::kInternal:
				default:
					snprintf(tempo_text, sizeof(tempo_text), "%s", "EXT (Waiting)");
					break;
			}
		} else {
			snprintf(tempo_text, sizeof(tempo_text), "%u", static_cast<unsigned>(sequencer_engine_.tempo_bpm()));
		}

		snprintf(
			current_status,
			sizeof(current_status),
			"MODE: SEQUENCER\n"
			"Tempo: %s%s\n"
			"Swing: %.1f%%\n"
			"Randomness: %.2f\n"
			"Sequence Length: %u\n"
			"Octave Range: %u\n"
			"Quantization: %s\n"
			"Voltage: %.2f > %.2f\n"
			"Timing: base=%luus current=%luus\n"
			"Gate History: %s",
			tempo_text,
			transport_text,
			sequencer_engine_.swing() * 100.0f,
			sequencer_engine_.randomness(),
			static_cast<unsigned>(sequencer_engine_.sequence_length()),
			static_cast<unsigned>(sequencer_engine_.range_octaves()),
			sequencer_engine_.quantization_mode_name(),
			sequencer_engine_.last_raw_voltage(),
			sequencer_engine_.last_quantized_voltage(),
			static_cast<unsigned long>(sequencer_engine_.base_interval_us()),
			static_cast<unsigned long>(sequencer_engine_.current_interval_us()),
			sequencer_engine_.gate_history()
		);
		line_count = static_cast<uint8_t>(10 + (transport_text[0] != '\0' ? 1 : 0));
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

	const bool entering_sequencer = (mode == AppMode::kSequencer);
	if (entering_sequencer) {
		// Play the shared 6-LED animation while LEDs are still in MIDI/simple mode.
		midi_to_cv_engine_.play_startup_animation();
	}

	if (mode_ == AppMode::kSequencer) {
		sequencer_engine_.on_mode_exit();
	}

	cancel_pending_single_button_presses();
	mode_ = mode;

	if (mode_ == AppMode::kSequencer) {
		ensure_sequencer_midi_parser_initialized();
		sequencer_midi_parser_.reset();
		sequencer_engine_.on_mode_enter();
	}

	save_persisted_app_mode(static_cast<uint8_t>(mode_));

	if (!entering_sequencer) {
		midi_to_cv_engine_.play_startup_animation();
	}
}
