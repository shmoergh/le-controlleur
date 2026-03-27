#include "sequencer-engine.h"

#include <cmath>

#include "pico/time.h"

#include "brain-ui/pots.h"
#include "debug-log.h"

SequencerEngine::SequencerEngine() :
	initialized_(false),
	playing_(false),
	gate_active_(false),
	bpm_(120),
	tick_interval_us_(125000),
	last_tick_time_us_(0),
	gate_off_time_us_(0),
	tick_counter_(0),
	shift_active_(false),
	range_octaves_(3),
	previous_range_octaves_(255),
	quantization_mode_(QuantizationMode::kChromatic),
	previous_quantization_mode_index_(255),
	randomness_pot_value_(0),
	mutation_probability_(0.0f),
	previous_randomness_pot_value_(255),
	previous_length_(0),
	last_raw_voltage_(0.0f),
	last_quantized_voltage_(0.0f),
	rng_state_a_(0x12345678u),
	rng_state_b_(0x87654321u) {
	init_sequence();
	init_io();
}

void SequencerEngine::update() {
	if (!initialized_) {
		return;
	}

	button_led_.update();
	update_bpm_from_pot();
	update_range_or_quantization_from_pot2();
	update_randomness_or_length_from_pot3();

	const uint64_t now_us = time_us_64();

	if (gate_active_ && now_us >= gate_off_time_us_) {
		gate_.set(false);
		gate_active_ = false;
	}

	if (!playing_) {
		return;
	}

	if (last_tick_time_us_ == 0 || (now_us - last_tick_time_us_) >= tick_interval_us_) {
		tick(now_us);
	}
}

void SequencerEngine::on_mode_enter() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	shift_active_ = false;
	update_bpm_from_pot(true);
	update_range_or_quantization_from_pot2(true);
	update_randomness_or_length_from_pot3(true);
}

void SequencerEngine::on_mode_exit() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	shift_active_ = false;
}

void SequencerEngine::on_button_a_short_press() {
	if (!initialized_) {
		return;
	}

	playing_ = !playing_;

	if (playing_) {
		last_tick_time_us_ = 0;
		tick_counter_ = 0;
	} else {
		gate_.set(false);
		gate_active_ = false;
		button_led_.off();
	}
}

void SequencerEngine::on_button_b_press() {
	if (!initialized_ || shift_active_) {
		return;
	}

	shift_active_ = true;
	update_range_or_quantization_from_pot2(true);
	update_randomness_or_length_from_pot3(true);
}

void SequencerEngine::on_button_b_release() {
	if (!initialized_ || !shift_active_) {
		return;
	}

	shift_active_ = false;
	update_range_or_quantization_from_pot2(true);
	update_randomness_or_length_from_pot3(true);
}

void SequencerEngine::init_sequence() {
	sequence_a_.steps.fill({0.0f, false});
	sequence_b_steps_.fill({0.0f, false});
	sequence_a_.length = 8;
	sequence_a_.position = 0;

	// Deterministic bring-up pattern.
	sequence_a_.steps[0] = {0.0f, true};
	sequence_a_.steps[1] = {0.5f, false};
	sequence_a_.steps[2] = {1.0f, true};
	sequence_a_.steps[3] = {1.5f, false};
	sequence_a_.steps[4] = {2.0f, true};
	sequence_a_.steps[5] = {2.5f, false};
	sequence_a_.steps[6] = {3.0f, true};
	sequence_a_.steps[7] = {3.5f, true};

	sequence_b_steps_[0] = {0.25f, true};
	sequence_b_steps_[1] = {0.75f, true};
	sequence_b_steps_[2] = {1.25f, false};
	sequence_b_steps_[3] = {1.75f, true};
	sequence_b_steps_[4] = {2.25f, false};
	sequence_b_steps_[5] = {2.75f, true};
	sequence_b_steps_[6] = {3.25f, false};
	sequence_b_steps_[7] = {3.75f, true};
}

void SequencerEngine::init_io() {
	brain::ui::PotsConfig pots_config = brain::ui::create_default_config();
	// Sequencer reads Pot 1 and Pot 3 continuously; keep mux settling enabled
	// to avoid cross-channel bleed between adjacent reads.
	pots_config.simple = false;
	pots_config.output_resolution = 8;
	pots_config.settling_delay_us = 150;
	pots_config.samples_per_read = 3;
	pots_.init(pots_config);

	if (!dac_.init()) {
		LOG_ERROR("SEQ", "DAC init failed");
		initialized_ = false;
		return;
	}

	dac_.set_coupling(brain::io::AudioCvOutChannel::kChannelA, brain::io::AudioCvOutCoupling::kDcCoupled);
	dac_.set_coupling(brain::io::AudioCvOutChannel::kChannelB, brain::io::AudioCvOutCoupling::kDcCoupled);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, 0.0f);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, 0.0f);

	gate_.begin();
	gate_.set(false);
	button_led_.init();
	button_led_.off();

	initialized_ = true;
	update_bpm_from_pot(true);
	update_range_or_quantization_from_pot2(true);
}

void SequencerEngine::update_bpm_from_pot(bool force_log) {
	const uint8_t pot_value = pots_.get(POT_INDEX_BPM);
	const uint16_t span = static_cast<uint16_t>(BPM_MAX - BPM_MIN);
	const uint16_t mapped_bpm = static_cast<uint16_t>(BPM_MIN + ((static_cast<uint32_t>(pot_value) * span) / 255));

	if (!force_log && mapped_bpm == bpm_) {
		return;
	}

	bpm_ = mapped_bpm;
	tick_interval_us_ = 60000000u / (bpm_ * STEPS_PER_QUARTER_NOTE);
}

void SequencerEngine::update_range_or_quantization_from_pot2(bool force_log) {
	const uint8_t pot_value = pots_.get(POT_INDEX_RANGE_OR_QUANTIZATION);

	if (shift_active_) {
		const uint8_t mode_index = static_cast<uint8_t>((static_cast<uint32_t>(pot_value) * QUANTIZATION_MODE_COUNT) / 256u);
		if (!force_log && mode_index == previous_quantization_mode_index_) {
			return;
		}

		quantization_mode_ = static_cast<QuantizationMode>(mode_index);
		previous_quantization_mode_index_ = mode_index;
		return;
	}

	const uint8_t span = static_cast<uint8_t>(RANGE_OCTAVES_MAX - RANGE_OCTAVES_MIN + 1);
	const uint8_t mapped_range = static_cast<uint8_t>(
		RANGE_OCTAVES_MIN + ((static_cast<uint32_t>(pot_value) * span) / 256u)
	);

	if (!force_log && mapped_range == previous_range_octaves_) {
		return;
	}

	range_octaves_ = mapped_range;
	previous_range_octaves_ = mapped_range;
}

void SequencerEngine::update_randomness_or_length_from_pot3(bool force_log) {
	const uint8_t pot_value = pots_.get(POT_INDEX_RANDOMNESS_OR_LENGTH);

	if (shift_active_) {
		const uint8_t new_length = static_cast<uint8_t>(1 + ((static_cast<uint32_t>(pot_value) * 63u) / 255u));
		if (!force_log && new_length == sequence_a_.length) {
			return;
		}

		sequence_a_.length = new_length;
		if (sequence_a_.position >= sequence_a_.length) {
			sequence_a_.position = 0;
		}

		previous_length_ = new_length;
		return;
	}

	if (!force_log && pot_value == previous_randomness_pot_value_) {
		return;
	}

	randomness_pot_value_ = pot_value;
	mutation_probability_ = static_cast<float>(randomness_pot_value_) / 255.0f;
	previous_randomness_pot_value_ = pot_value;
}

void SequencerEngine::apply_mutation_for_step(uint8_t step_index) {
	Step& step_a = sequence_a_.steps[step_index];
	Step& step_b = sequence_b_steps_[step_index];

	const float old_voltage_a = step_a.voltage;
	const float old_voltage_b = step_b.voltage;
	const bool old_gate = step_a.gate;

	bool mutated_a = false;
	bool mutated_b = false;
	bool mutated_gate = false;

	if (mutation_probability_ > 0.0f && random_unit(rng_state_a_) < mutation_probability_) {
		step_a.voltage = random_unit(rng_state_a_) * RANDOM_VOLTAGE_MAX;
		mutated_a = true;
	}

	if (mutation_probability_ > 0.0f && random_unit(rng_state_b_) < mutation_probability_) {
		step_b.voltage = random_unit(rng_state_b_) * RANDOM_VOLTAGE_MAX;
		mutated_b = true;
	}

	if (mutation_probability_ > 0.0f && random_unit(rng_state_a_) < mutation_probability_) {
		step_a.gate = random_unit(rng_state_a_) >= 0.4f;
		mutated_gate = (step_a.gate != old_gate);
	}

	(void) old_voltage_a;
	(void) old_voltage_b;
	(void) old_gate;
	(void) mutated_a;
	(void) mutated_b;
	(void) mutated_gate;
}

void SequencerEngine::tick(uint64_t now_us) {
	last_tick_time_us_ = now_us;

	const uint8_t step_index = sequence_a_.position;
	apply_mutation_for_step(step_index);
	const Step& step_a = sequence_a_.steps[step_index];
	const Step& step_b = sequence_b_steps_[step_index];
	const float scaled_voltage_a = apply_pitch_range(step_a.voltage);
	const float scaled_voltage_b = apply_pitch_range(step_b.voltage);
	const float output_voltage_a = quantize_voltage(scaled_voltage_a);
	const float output_voltage_b = quantize_voltage(scaled_voltage_b);

	last_raw_voltage_ = scaled_voltage_a;
	last_quantized_voltage_ = output_voltage_a;

	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, output_voltage_a);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, output_voltage_b);

	if (step_a.gate) {
		gate_.set(true);
		gate_active_ = true;
		gate_off_time_us_ = now_us + GATE_PULSE_US;
	} else {
		gate_.set(false);
		gate_active_ = false;
	}

	if ((tick_counter_ % STEPS_PER_QUARTER_NOTE) == 0) {
		button_led_.blink_duration(BUTTON_LED_BLINK_MS, BUTTON_LED_BLINK_INTERVAL_MS);
	}

	tick_counter_++;
	sequence_a_.position = static_cast<uint8_t>((step_index + 1) % sequence_a_.length);
}

void SequencerEngine::reset_transport() {
	playing_ = false;
	sequence_a_.position = 0;
	last_tick_time_us_ = 0;
	gate_off_time_us_ = 0;
	gate_active_ = false;
	tick_counter_ = 0;
	gate_.set(false);
	button_led_.off();
}

float SequencerEngine::apply_pitch_range(float source_voltage) const {
	if (range_octaves_ == 0) {
		return 0.0f;
	}

	float normalized = source_voltage / RANDOM_VOLTAGE_MAX;
	if (normalized < 0.0f) {
		normalized = 0.0f;
	}
	if (normalized > 1.0f) {
		normalized = 1.0f;
	}
	return normalized * static_cast<float>(range_octaves_);
}

float SequencerEngine::quantize_voltage(float voltage) const {
	if (quantization_mode_ == QuantizationMode::kUnquantized) {
		return voltage;
	}

	const float max_voltage = static_cast<float>(range_octaves_);
	if (voltage <= 0.0f) {
		return 0.0f;
	}
	if (voltage >= max_voltage) {
		return max_voltage;
	}

	static constexpr uint8_t CHROMATIC[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	static constexpr uint8_t MAJOR[] = {0, 2, 4, 5, 7, 9, 11};
	static constexpr uint8_t MINOR[] = {0, 2, 3, 5, 7, 8, 10};
	static constexpr uint8_t PENTATONIC[] = {0, 2, 4, 7, 9};
	static constexpr uint8_t EXTRA[] = {0, 1, 4, 5, 7, 8, 11};

	const uint8_t* scale = CHROMATIC;
	uint8_t scale_size = sizeof(CHROMATIC);

	switch (quantization_mode_) {
		case QuantizationMode::kChromatic:
			scale = CHROMATIC;
			scale_size = sizeof(CHROMATIC);
			break;
		case QuantizationMode::kMajor:
			scale = MAJOR;
			scale_size = sizeof(MAJOR);
			break;
		case QuantizationMode::kMinor:
			scale = MINOR;
			scale_size = sizeof(MINOR);
			break;
		case QuantizationMode::kPentatonic:
			scale = PENTATONIC;
			scale_size = sizeof(PENTATONIC);
			break;
		case QuantizationMode::kExtra:
			scale = EXTRA;
			scale_size = sizeof(EXTRA);
			break;
		case QuantizationMode::kUnquantized:
		default:
			return voltage;
	}

	const float semitone = voltage * 12.0f;
	const int32_t rounded = static_cast<int32_t>(lroundf(semitone));
	const int32_t octave_center = rounded / 12;

	float best_distance = 1e9f;
	int32_t best_semitone = rounded;

	for (int32_t octave = octave_center - 1; octave <= octave_center + 1; ++octave) {
		for (uint8_t i = 0; i < scale_size; ++i) {
			const int32_t candidate = (octave * 12) + static_cast<int32_t>(scale[i]);
			const float distance = fabsf(static_cast<float>(candidate) - semitone);
			if (distance < best_distance) {
				best_distance = distance;
				best_semitone = candidate;
			}
		}
	}

	float quantized = static_cast<float>(best_semitone) / 12.0f;
	if (quantized < 0.0f) {
		quantized = 0.0f;
	}
	if (quantized > max_voltage) {
		quantized = max_voltage;
	}
	return quantized;
}

const char* SequencerEngine::quantization_mode_to_string(QuantizationMode mode) {
	switch (mode) {
		case QuantizationMode::kUnquantized:
			return "Unquantized";
		case QuantizationMode::kChromatic:
			return "Chromatic";
		case QuantizationMode::kMajor:
			return "Major";
		case QuantizationMode::kMinor:
			return "Minor";
		case QuantizationMode::kPentatonic:
			return "Pentatonic";
		case QuantizationMode::kExtra:
			return "Extra";
		default:
			return "Unknown";
	}
}

uint32_t SequencerEngine::next_random(uint32_t& state) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

float SequencerEngine::random_unit(uint32_t& state) {
	const uint32_t value = next_random(state) & 0x00FFFFFFu;
	return static_cast<float>(value) / 16777215.0f;
}

uint16_t SequencerEngine::tempo_bpm() const {
	return bpm_;
}

float SequencerEngine::randomness() const {
	return mutation_probability_;
}

uint8_t SequencerEngine::sequence_length() const {
	return sequence_a_.length;
}

uint8_t SequencerEngine::range_octaves() const {
	return range_octaves_;
}

const char* SequencerEngine::quantization_mode_name() const {
	return quantization_mode_to_string(quantization_mode_);
}

float SequencerEngine::last_raw_voltage() const {
	return last_raw_voltage_;
}

float SequencerEngine::last_quantized_voltage() const {
	return last_quantized_voltage_;
}
