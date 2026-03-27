#include "midi-to-cv-engine.h"
#include "debug-log.h"

MidiToCVEngine::MidiToCVEngine(brain::io::AudioCvOutChannel cv_channel, uint8_t midi_channel) :
	button_a_(GPIO_BRAIN_BUTTON_1),
	button_b_(GPIO_BRAIN_BUTTON_2),
	pots_(),
	leds_(true)
{
	init(cv_channel, midi_channel);
	set_max_cc_voltage(5);

	midi_channel_ = midi_channel;
	cv_channel_ = cv_channel;
	state_ = State::kDefault;

	button_a_.init();
	button_b_.init();

	// Button A handlers
	button_a_.set_on_press([this]() {
		button_a_on_press();
	});
	button_a_.set_on_release([this]() {
		button_a_on_release();
	});

	// Button B handlers
	button_b_.set_on_press([this]() {
		button_b_on_press();
	});
	button_b_.set_on_release([this]() {
		button_b_on_release();
	});

	button_a_pressed_ = false;
	button_b_pressed_ = false;

	// Init leds
	leds_.init();
	leds_.startup_animation();
	reset_leds_ = false;
	playhead_led_ = 0;
	key_pressed_ = false;

	// Pots setup
	brain::ui::PotsConfig pots_config = brain::ui::create_default_config();
	pots_config.simple = true;
	pots_config.output_resolution = 8;
	pots_.init(pots_config);

	// Panic
	panic_timer_start_ = 0;
	telemetry_last_log_time_ = 0;

	// Load settings
	mode_ = MidiToCV::Mode::kDefault;
	load_settings();
	init_pot_functions();
}

void MidiToCVEngine::button_a_on_press() {
	if (state_ == State::kDefault && state_ != State::kSetMidiChannel) {
		state_ = State::kSetMidiChannel;
	}

	// If button B is pressed and button A is pressed then panic mode starts
	if (state_ == State::kSetCVChannel) {
		state_ = State::kPanicStarted;
	}
}

void MidiToCVEngine::button_a_on_release() {
	if (state_ == State::kSetMidiChannel) {
		set_midi_channel(midi_channel_);
		reset_pot_function_context();
	}
	if (state_ == State::kPanicStarted) {
		reset_panic();
	}
	state_ = State::kDefault;
}

void MidiToCVEngine::button_b_on_press() {
	if (state_ == State::kDefault && state_ != State::kSetCVChannel) {
		state_ = State::kSetCVChannel;
	}

	if (state_ == State::kSetMidiChannel) {
		state_ = State::kPanicStarted;
	}
}

void MidiToCVEngine::button_b_on_release() {
	if (state_ == State::kSetCVChannel) {
		set_pitch_channel(cv_channel_);
		MidiToCV::set_mode(mode_);
		reset_pot_function_context();
	}
	if (state_ == State::kPanicStarted) {
		reset_panic();
	}
	state_ = State::kDefault;
}

void MidiToCVEngine::update() {
	button_a_.update();
	button_b_.update();

	switch (state_) {
		// Read pot X and set MIDI channel accordingly
		case State::kSetMidiChannel: {
			if (is_note_playing()) break;
			set_active_pot_functions(POT_FUNCTION_ID_MIDI_CHANNEL, POT_FUNCTION_ID_NONE, POT_FUNCTION_ID_NONE);
			update_midi_channel_setting();
			pot_multi_function_.clear_changed_flags();
			leds_.set_from_mask(midi_channel_);
			reset_leds_ = true;
			break;
		}

		// Read CV settings
		case State::kSetCVChannel: {
			if (is_note_playing()) break;
			set_active_pot_functions(POT_FUNCTION_ID_NONE, POT_FUNCTION_ID_CV_CHANNEL, POT_FUNCTION_ID_MODE);

			update_cv_channel_setting();
			uint8_t cv_led_mask = 0b000000;
			if (cv_channel_ == brain::io::AudioCvOutChannel::kChannelA) {
				cv_led_mask = LED_MASK_CHANNEL_A;
			} else {
				cv_led_mask = LED_MASK_CHANNEL_B;
			}

			update_cc_setting();
			pot_multi_function_.clear_changed_flags();
			uint8_t cc_led_mask = 0b000000;
			switch (mode_) {
				case MidiToCV::Mode::kDefault:
					cc_led_mask = LED_MASK_MODE_VELOCITY;
					break;
				case MidiToCV::Mode::kModWheel:
					cc_led_mask = LED_MASK_MODE_MODWHEEL;
					break;
				case MidiToCV::Mode::kUnison:
					cc_led_mask = LED_MASK_MODE_UNISON;
					break;
				case MidiToCV::Mode::kDuo:
					cc_led_mask = LED_MASK_MODE_DUO;
					break;

				default:
					break;
			}

			uint8_t led_mask = cv_led_mask | cc_led_mask;
			leds_.set_from_mask(led_mask);
			reset_leds_ = true;
			break;
		}

		// Panic
		case State::kPanicStarted: {
			if (panic_timer_start_ == 0) {
				panic_timer_start_ = get_absolute_time();
				leds_.on_all();
			}

			absolute_time_t now = get_absolute_time();
			if (absolute_time_diff_us(panic_timer_start_, now) / 1000 >= PANIC_HOLD_THRESHOLD_MS) {
				MidiToCV::set_gate(false);
				MidiToCV::reset_note_stack();
				state_ = State::kDefault;
				reset_panic();
			}
			break;
		}

		default: {
			MidiToCV::update();
			if (is_note_playing()) {
				leds_.on(playhead_led_);
				key_pressed_ = true;
			} else {
				if (key_pressed_) {
					leds_.off(playhead_led_);
					playhead_led_++;
					if (playhead_led_ > 5) playhead_led_ = 0;
					key_pressed_ = false;
				}
			}

			if (reset_leds_) {
				leds_.off_all();
			}
			reset_leds_ = false;
			break;
		}
	}

	log_runtime_snapshot();
}

State MidiToCVEngine::get_state() const {
	return state_;
}

uint8_t MidiToCVEngine::get_midi_channel() const {
	return midi_channel_;
}

void MidiToCVEngine::reset_panic() {
	panic_timer_start_ = 0;
	reset_pot_function_context();
	leds_.off_all();
}

void MidiToCVEngine::init_pot_functions() {
	pot_multi_function_.init();

	brain::ui::PotFunctionConfig midi_channel_cfg;
	midi_channel_cfg.function_id = POT_FUNCTION_ID_MIDI_CHANNEL;
	midi_channel_cfg.pot_index = POT_MIDI_CHANNEL;
	midi_channel_cfg.min_value = 0;
	midi_channel_cfg.max_value = 255;
	midi_channel_cfg.initial_value = pots_.get(POT_MIDI_CHANNEL);
	midi_channel_cfg.mode = PotMode::kValueScale;
	midi_channel_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(midi_channel_cfg);

	brain::ui::PotFunctionConfig cv_channel_cfg;
	cv_channel_cfg.function_id = POT_FUNCTION_ID_CV_CHANNEL;
	cv_channel_cfg.pot_index = POT_CV_CHANNEL;
	cv_channel_cfg.min_value = 0;
	cv_channel_cfg.max_value = 255;
	cv_channel_cfg.initial_value = pots_.get(POT_CV_CHANNEL);
	cv_channel_cfg.mode = PotMode::kValueScale;
	cv_channel_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(cv_channel_cfg);

	brain::ui::PotFunctionConfig mode_cfg;
	mode_cfg.function_id = POT_FUNCTION_ID_MODE;
	mode_cfg.pot_index = POT_MODE;
	mode_cfg.min_value = 0;
	mode_cfg.max_value = 255;
	mode_cfg.initial_value = pots_.get(POT_MODE);
	mode_cfg.mode = PotMode::kValueScale;
	mode_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(mode_cfg);
}

void MidiToCVEngine::set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function) {
	const uint8_t functions[NUM_POTS] = {pot_0_function, pot_1_function, pot_2_function};
	pot_multi_function_.set_active_functions(functions, NUM_POTS);
	pot_multi_function_.update(pots_);
}

void MidiToCVEngine::reset_pot_function_context() {
	set_active_pot_functions(POT_FUNCTION_ID_NONE, POT_FUNCTION_ID_NONE, POT_FUNCTION_ID_NONE);
	pot_multi_function_.clear_changed_flags();
}

void MidiToCVEngine::update_midi_channel_setting() {
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_MIDI_CHANNEL)) return;
	uint8_t pot_a_value = pot_multi_function_.get_value(POT_FUNCTION_ID_MIDI_CHANNEL);

	// Divide into 16 bins (0-15) for stable MIDI channel selection
	uint8_t binned_value = pot_a_value / 16;
	midi_channel_ = binned_value + 1;
}

void MidiToCVEngine::update_cv_channel_setting() {
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_CV_CHANNEL)) return;
	uint8_t pot_b_value = pot_multi_function_.get_value(POT_FUNCTION_ID_CV_CHANNEL);

	if (pot_b_value < POT_CV_CHANNEL_THRESHOLD) {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelA;
	} else {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelB;
	}
}

void MidiToCVEngine::update_cc_setting() {
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_MODE)) return;
	uint8_t pot_c_value = pot_multi_function_.get_value(POT_FUNCTION_ID_MODE);
	mode_ = MidiToCV::Mode((4 * pot_c_value) / 256);
}

void MidiToCVEngine::load_settings() {
	uint8_t pot_a_value = pots_.get(POT_MIDI_CHANNEL);
	uint8_t binned_value = pot_a_value / 16;
	midi_channel_ = binned_value + 1;
	set_midi_channel(midi_channel_);

	uint8_t pot_b_value = pots_.get(POT_CV_CHANNEL);
	if (pot_b_value < POT_CV_CHANNEL_THRESHOLD) {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelA;
	} else {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelB;
	}
	set_pitch_channel(cv_channel_);

	uint8_t pot_c_value = pots_.get(POT_MODE);
	mode_ = MidiToCV::Mode((4 * pot_c_value) / 256);
	set_mode(mode_);
}

void MidiToCVEngine::log_runtime_snapshot() {
	absolute_time_t now = get_absolute_time();

	if (telemetry_last_log_time_ != 0) {
		int64_t elapsed_ms = absolute_time_diff_us(telemetry_last_log_time_, now) / 1000;
		if (elapsed_ms < static_cast<int64_t>(RUNTIME_SNAPSHOT_INTERVAL_MS)) {
			return;
		}
	}

	telemetry_last_log_time_ = now;

	LOG_INFO(
		"APP",
		"snapshot state=%s midi_ch=%u cv=%s cc_mode=%s note_playing=%u tempo=n/a swing=n/a randomness=n/a length=n/a play=n/a",
		state_to_string(state_),
		midi_channel_,
		cv_channel_to_string(cv_channel_),
		mode_to_string(mode_),
		is_note_playing() ? 1 : 0
	);
}

const char* MidiToCVEngine::state_to_string(State state) const {
	switch (state) {
		case State::kDefault:
			return "default";
		case State::kSetMidiChannel:
			return "set-midi-channel";
		case State::kSetCVChannel:
			return "set-cv-channel";
		case State::kPanicStarted:
			return "panic-started";
		default:
			return "unknown";
	}
}

const char* MidiToCVEngine::cv_channel_to_string(brain::io::AudioCvOutChannel cv_channel) const {
	switch (cv_channel) {
		case brain::io::AudioCvOutChannel::kChannelA:
			return "A";
		case brain::io::AudioCvOutChannel::kChannelB:
			return "B";
		default:
			return "unknown";
	}
}

const char* MidiToCVEngine::mode_to_string(MidiToCV::Mode mode) const {
	switch (mode) {
		case MidiToCV::Mode::kDefault:
			return "velocity";
		case MidiToCV::Mode::kModWheel:
			return "modwheel";
		case MidiToCV::Mode::kUnison:
			return "unison";
		case MidiToCV::Mode::kDuo:
			return "duo";
		default:
			return "unknown";
	}
}
