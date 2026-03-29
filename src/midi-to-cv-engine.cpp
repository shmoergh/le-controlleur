#include "midi-to-cv-engine.h"
#include "debug-log.h"
#include "settings-storage.h"

MidiToCVEngine::MidiToCVEngine(brain::io::AudioCvOutChannel cv_channel, uint8_t midi_channel) :
	pots_(),
	leds_(true)
{
	init(cv_channel, midi_channel);
	set_max_cc_voltage(5);
	if (!enable_calibrated_output(true)) {
		LOG_INFO("M2CV", "CV calibration unavailable; using raw output");
	} else {
		LOG_INFO("M2CV", "CV calibration loaded from flash");
	}

	midi_channel_ = midi_channel;
	cv_channel_ = cv_channel;
	state_ = State::kDefault;

	// Init leds
	leds_.init();
	leds_.startup_animation();
	reset_leds_ = false;
	playhead_led_ = 0;
	key_pressed_ = false;
	has_persisted_midi_channel_ = false;
	persisted_midi_channel_ = 0;
	midi_channel_entry_raw_ = 0;
	cv_channel_entry_raw_ = 0;
	mode_entry_raw_ = 0;
	midi_channel_pickup_armed_ = false;
	cv_channel_pickup_armed_ = false;
	mode_pickup_armed_ = false;

	// Pots setup
	brain::ui::PotsConfig pots_config = brain::ui::create_default_config();
	pots_config.simple = true;
	pots_config.output_resolution = 8;
	pots_.init(pots_config);

	// Load settings
	mode_ = MidiToCV::Mode::kDefault;
	load_settings();
	init_pot_functions();
}

void MidiToCVEngine::on_button_a_press() {
	if (state_ == State::kDefault) {
		state_ = State::kSetMidiChannel;
		arm_midi_channel_pickup();
	}
}

void MidiToCVEngine::on_button_a_release() {
	if (state_ == State::kSetMidiChannel) {
		set_midi_channel(midi_channel_);
		persist_midi_channel_if_needed();
		reset_pot_function_context();
	}
	state_ = State::kDefault;
}

void MidiToCVEngine::on_button_b_press() {
	if (state_ == State::kDefault) {
		state_ = State::kSetCVChannel;
		arm_cv_settings_pickup();
	}
}

void MidiToCVEngine::on_button_b_release() {
	if (state_ == State::kSetCVChannel) {
		set_pitch_channel(cv_channel_);
		MidiToCV::set_mode(mode_);
		persist_cv_settings_if_needed();
		reset_pot_function_context();
	}
	state_ = State::kDefault;
}

void MidiToCVEngine::update() {
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
}

State MidiToCVEngine::get_state() const {
	return state_;
}

void MidiToCVEngine::panic() {
	MidiToCV::set_gate(false);
	MidiToCV::reset_note_stack();
	reset_pot_function_context();
	leds_.off_all();
	state_ = State::kDefault;
}

void MidiToCVEngine::play_startup_animation() {
	leds_.startup_animation();
}

uint8_t MidiToCVEngine::get_midi_channel() const {
	return midi_channel_;
}

void MidiToCVEngine::init_pot_functions() {
	pot_multi_function_.init();
	const uint8_t initial_midi_channel_value = static_cast<uint8_t>(
		clamp(0, 255, static_cast<int32_t>((midi_channel_ - 1) * 16 + 8))
	);
	const uint8_t initial_cv_channel_value =
		(cv_channel_ == brain::io::AudioCvOutChannel::kChannelA) ? 63u : 191u;
	const uint8_t initial_mode_value = static_cast<uint8_t>(
		clamp(0, 255, static_cast<int32_t>(static_cast<uint8_t>(mode_) * 64 + 32))
	);

	brain::ui::PotFunctionConfig midi_channel_cfg;
	midi_channel_cfg.function_id = POT_FUNCTION_ID_MIDI_CHANNEL;
	midi_channel_cfg.pot_index = POT_MIDI_CHANNEL;
	midi_channel_cfg.min_value = 0;
	midi_channel_cfg.max_value = 255;
	midi_channel_cfg.initial_value = initial_midi_channel_value;
	midi_channel_cfg.mode = PotMode::kValueScale;
	midi_channel_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(midi_channel_cfg);

	brain::ui::PotFunctionConfig cv_channel_cfg;
	cv_channel_cfg.function_id = POT_FUNCTION_ID_CV_CHANNEL;
	cv_channel_cfg.pot_index = POT_CV_CHANNEL;
	cv_channel_cfg.min_value = 0;
	cv_channel_cfg.max_value = 255;
	cv_channel_cfg.initial_value = initial_cv_channel_value;
	cv_channel_cfg.mode = PotMode::kValueScale;
	cv_channel_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(cv_channel_cfg);

	brain::ui::PotFunctionConfig mode_cfg;
	mode_cfg.function_id = POT_FUNCTION_ID_MODE;
	mode_cfg.pot_index = POT_MODE;
	mode_cfg.min_value = 0;
	mode_cfg.max_value = 255;
	mode_cfg.initial_value = initial_mode_value;
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
	if (!is_pickup_armed(POT_MIDI_CHANNEL, midi_channel_entry_raw_, midi_channel_pickup_armed_)) return;
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_MIDI_CHANNEL)) return;
	uint8_t pot_a_value = pot_multi_function_.get_value(POT_FUNCTION_ID_MIDI_CHANNEL);

	// Divide into 16 bins (0-15) for stable MIDI channel selection
	uint8_t binned_value = pot_a_value / 16;
	midi_channel_ = binned_value + 1;
}

void MidiToCVEngine::update_cv_channel_setting() {
	if (!is_pickup_armed(POT_CV_CHANNEL, cv_channel_entry_raw_, cv_channel_pickup_armed_)) return;
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_CV_CHANNEL)) return;
	uint8_t pot_b_value = pot_multi_function_.get_value(POT_FUNCTION_ID_CV_CHANNEL);

	if (pot_b_value < POT_CV_CHANNEL_THRESHOLD) {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelA;
	} else {
		cv_channel_ = brain::io::AudioCvOutChannel::kChannelB;
	}
}

void MidiToCVEngine::update_cc_setting() {
	if (!is_pickup_armed(POT_MODE, mode_entry_raw_, mode_pickup_armed_)) return;
	if (!pot_multi_function_.get_changed(POT_FUNCTION_ID_MODE)) return;
	uint8_t pot_c_value = pot_multi_function_.get_value(POT_FUNCTION_ID_MODE);
	mode_ = MidiToCV::Mode((4 * pot_c_value) / 256);
}

void MidiToCVEngine::load_settings() {
	uint8_t persisted_channel = 0;
	if (load_persisted_midi_channel(persisted_channel) && persisted_channel >= 1 && persisted_channel <= 16) {
		midi_channel_ = persisted_channel;
		has_persisted_midi_channel_ = true;
		persisted_midi_channel_ = persisted_channel;
	} else {
		uint8_t pot_a_value = pots_.get(POT_MIDI_CHANNEL);
		uint8_t binned_value = pot_a_value / 16;
		midi_channel_ = binned_value + 1;
		has_persisted_midi_channel_ = false;
		persisted_midi_channel_ = 0;
	}
	set_midi_channel(midi_channel_);

	uint8_t persisted_cv_channel = 0;
	if (load_persisted_midi_cv_channel(persisted_cv_channel) && persisted_cv_channel <= 1u) {
		cv_channel_ = (persisted_cv_channel == 0u)
			? brain::io::AudioCvOutChannel::kChannelA
			: brain::io::AudioCvOutChannel::kChannelB;
	} else {
		uint8_t pot_b_value = pots_.get(POT_CV_CHANNEL);
		cv_channel_ = (pot_b_value < POT_CV_CHANNEL_THRESHOLD)
			? brain::io::AudioCvOutChannel::kChannelA
			: brain::io::AudioCvOutChannel::kChannelB;
	}
	set_pitch_channel(cv_channel_);

	uint8_t persisted_mode = static_cast<uint8_t>(MidiToCV::Mode::kDefault);
	if (load_persisted_midi_mode(persisted_mode) && persisted_mode <= static_cast<uint8_t>(MidiToCV::Mode::kDuo)) {
		mode_ = MidiToCV::Mode(persisted_mode);
	} else {
		uint8_t pot_c_value = pots_.get(POT_MODE);
		mode_ = MidiToCV::Mode((4 * pot_c_value) / 256);
	}
	set_mode(mode_);
}

void MidiToCVEngine::persist_midi_channel_if_needed() {
	if (has_persisted_midi_channel_ && persisted_midi_channel_ == midi_channel_) {
		return;
	}

	if (save_persisted_midi_channel(midi_channel_)) {
		has_persisted_midi_channel_ = true;
		persisted_midi_channel_ = midi_channel_;
	} else {
		LOG_ERROR("M2CV", "failed to save midi_channel=%u to storage", midi_channel_);
	}
}

void MidiToCVEngine::persist_cv_settings_if_needed() {
	if (!save_persisted_midi_cv_channel(
			cv_channel_ == brain::io::AudioCvOutChannel::kChannelA ? 0u : 1u)) {
		LOG_ERROR("M2CV", "failed to save cv_channel to storage");
	}

	if (!save_persisted_midi_mode(static_cast<uint8_t>(mode_))) {
		LOG_ERROR("M2CV", "failed to save mode=%u to storage", static_cast<unsigned>(mode_));
	}
}

uint8_t MidiToCVEngine::read_stable_pot(uint8_t pot_index) {
	(void) pots_.get(pot_index);
	return static_cast<uint8_t>(pots_.get(pot_index));
}

bool MidiToCVEngine::is_pickup_armed(uint8_t pot_index, uint8_t entry_raw, bool& armed_flag) {
	if (armed_flag) {
		return true;
	}

	uint8_t current_raw = static_cast<uint8_t>(pots_.get(pot_index));
	int32_t delta = static_cast<int32_t>(current_raw) - static_cast<int32_t>(entry_raw);
	if (delta < 0) {
		delta = -delta;
	}
	if (delta > POT_MOVEMENT_ACTIVATION_THRESHOLD) {
		armed_flag = true;
	}

	return armed_flag;
}

void MidiToCVEngine::arm_midi_channel_pickup() {
	midi_channel_entry_raw_ = read_stable_pot(POT_MIDI_CHANNEL);
	midi_channel_pickup_armed_ = false;
}

void MidiToCVEngine::arm_cv_settings_pickup() {
	cv_channel_entry_raw_ = read_stable_pot(POT_CV_CHANNEL);
	mode_entry_raw_ = read_stable_pot(POT_MODE);
	cv_channel_pickup_armed_ = false;
	mode_pickup_armed_ = false;
}
