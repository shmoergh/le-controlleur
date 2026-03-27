#include "sequencer-engine.h"

#include <cmath>

#include "pico/time.h"

#include "brain-ui/pots.h"
#include "debug-log.h"

SequencerEngine::SequencerEngine() :
	leds_(true),
	initialized_(false),
	playing_(false),
	gate_active_(false),
	bpm_(120),
	tick_interval_us_(125000),
	next_tick_due_us_(0),
	gate_off_time_us_(0),
	tick_counter_(0),
	current_step_interval_us_(125000),
	shift_active_(false),
	last_shift_context_(false),
	swing_amount_(0.0f),
	range_octaves_(3),
	quantization_mode_(QuantizationMode::kChromatic),
	randomness_pot_value_(0),
	mutation_probability_(0.0f),
	last_raw_voltage_(0.0f),
	last_quantized_voltage_(0.0f),
	gate_history_fifo_{0, 0, 0, 0, 0, 0},
	gate_history_text_{'0', '0', '0', '0', '0', '0', '\0'},
	rng_state_a_(0x12345678u),
	rng_state_b_(0x87654321u) {
	init_sequence();
	init_io();
}

void SequencerEngine::update() {
	if (!initialized_) {
		return;
	}

	leds_.update();
	button_led_.update();
	update_pot_mappings();

	const uint64_t now_us = time_us_64();

	if (gate_active_ && now_us >= gate_off_time_us_) {
		gate_.set(false);
		gate_active_ = false;
	}

	if (!playing_) {
		return;
	}

	if (next_tick_due_us_ == 0) {
		next_tick_due_us_ = now_us;
	}

	uint8_t safety_counter = 0;
	while (now_us >= next_tick_due_us_ && safety_counter < 8) {
		tick(next_tick_due_us_);
		current_step_interval_us_ = compute_next_step_interval_us();
		next_tick_due_us_ += current_step_interval_us_;
		++safety_counter;
	}
}

void SequencerEngine::on_mode_enter() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	shift_active_ = false;
	last_shift_context_ = shift_active_;
	update_pot_mappings(true);
}

void SequencerEngine::on_mode_exit() {
	if (!initialized_) {
		return;
	}

	reset_transport();
	shift_active_ = false;
	last_shift_context_ = shift_active_;
}

void SequencerEngine::on_button_a_short_press() {
	if (!initialized_) {
		return;
	}

	playing_ = !playing_;

	if (playing_) {
		next_tick_due_us_ = 0;
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
	update_pot_mappings(true);
}

void SequencerEngine::on_button_b_release() {
	if (!initialized_ || !shift_active_) {
		return;
	}

	shift_active_ = false;
	update_pot_mappings(true);
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
	leds_.init();
	leds_.off_all();
	button_led_.init();
	button_led_.off();
	reset_gate_history();

	init_pot_functions();
	initialized_ = true;
	update_pot_mappings(true);
}

void SequencerEngine::init_pot_functions() {
	pot_multi_function_.init();

	brain::ui::PotFunctionConfig bpm_cfg;
	bpm_cfg.function_id = POT_FUNCTION_ID_BPM;
	bpm_cfg.pot_index = POT_INDEX_BPM;
	bpm_cfg.min_value = 0;
	bpm_cfg.max_value = 255;
	bpm_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(bpm_ - BPM_MIN) * 255u) / (BPM_MAX - BPM_MIN));
	bpm_cfg.mode = brain::ui::PotMode::kValueScale;
	bpm_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(bpm_cfg);

	brain::ui::PotFunctionConfig swing_cfg;
	swing_cfg.function_id = POT_FUNCTION_ID_SWING;
	swing_cfg.pot_index = POT_INDEX_BPM;
	swing_cfg.min_value = 0;
	swing_cfg.max_value = 255;
	swing_cfg.initial_value = 0;
	swing_cfg.mode = brain::ui::PotMode::kValueScale;
	swing_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(swing_cfg);

	brain::ui::PotFunctionConfig range_cfg;
	range_cfg.function_id = POT_FUNCTION_ID_RANGE;
	range_cfg.pot_index = POT_INDEX_RANGE_OR_QUANTIZATION;
	range_cfg.min_value = 0;
	range_cfg.max_value = 255;
	range_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(range_octaves_) * 255u) / RANGE_OCTAVES_MAX);
	range_cfg.mode = brain::ui::PotMode::kValueScale;
	range_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(range_cfg);

	brain::ui::PotFunctionConfig quant_cfg;
	quant_cfg.function_id = POT_FUNCTION_ID_QUANTIZATION;
	quant_cfg.pot_index = POT_INDEX_RANGE_OR_QUANTIZATION;
	quant_cfg.min_value = 0;
	quant_cfg.max_value = 255;
	quant_cfg.initial_value = static_cast<uint8_t>((static_cast<uint32_t>(static_cast<uint8_t>(quantization_mode_)) * 255u) / (QUANTIZATION_MODE_COUNT - 1));
	quant_cfg.mode = brain::ui::PotMode::kValueScale;
	quant_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(quant_cfg);

	brain::ui::PotFunctionConfig randomness_cfg;
	randomness_cfg.function_id = POT_FUNCTION_ID_RANDOMNESS;
	randomness_cfg.pot_index = POT_INDEX_RANDOMNESS_OR_LENGTH;
	randomness_cfg.min_value = 0;
	randomness_cfg.max_value = 255;
	randomness_cfg.initial_value = randomness_pot_value_;
	randomness_cfg.mode = brain::ui::PotMode::kValueScale;
	randomness_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(randomness_cfg);

	brain::ui::PotFunctionConfig length_cfg;
	length_cfg.function_id = POT_FUNCTION_ID_LENGTH;
	length_cfg.pot_index = POT_INDEX_RANDOMNESS_OR_LENGTH;
	length_cfg.min_value = 0;
	length_cfg.max_value = 255;
	length_cfg.initial_value = static_cast<uint8_t>(((static_cast<uint32_t>(sequence_a_.length - 1u)) * 255u) / 63u);
	length_cfg.mode = brain::ui::PotMode::kValueScale;
	length_cfg.pickup_hysteresis = POT_FUNCTION_PICKUP_HYSTERESIS;
	pot_multi_function_.register_function(length_cfg);
}

void SequencerEngine::set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function) {
	const uint8_t active_functions[NUM_POTS] = {pot_0_function, pot_1_function, pot_2_function};
	pot_multi_function_.set_active_functions(active_functions, NUM_POTS);
	pot_multi_function_.update(pots_);
}

void SequencerEngine::update_pot_mappings(bool force_apply) {
	const bool shift_context_changed = (shift_active_ != last_shift_context_);
	if (shift_active_) {
		set_active_pot_functions(POT_FUNCTION_ID_SWING, POT_FUNCTION_ID_QUANTIZATION, POT_FUNCTION_ID_LENGTH);
	} else {
		set_active_pot_functions(POT_FUNCTION_ID_BPM, POT_FUNCTION_ID_RANGE, POT_FUNCTION_ID_RANDOMNESS);
	}

	if (shift_context_changed) {
		pot_multi_function_.clear_changed_flags();
		last_shift_context_ = shift_active_;
		if (!force_apply) {
			return;
		}
	}

	update_bpm_from_pot(force_apply);
	update_swing_from_pot1(force_apply);
	update_range_or_quantization_from_pot2(force_apply);
	update_randomness_or_length_from_pot3(force_apply);
	pot_multi_function_.clear_changed_flags();
}

void SequencerEngine::update_bpm_from_pot(bool force_apply) {
	if (!force_apply && !pot_multi_function_.get_changed(POT_FUNCTION_ID_BPM)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(POT_FUNCTION_ID_BPM);
	const uint16_t span = static_cast<uint16_t>(BPM_MAX - BPM_MIN);
	const uint16_t mapped_bpm = static_cast<uint16_t>(BPM_MIN + ((static_cast<uint32_t>(pot_value) * span) / 255));

	bpm_ = mapped_bpm;
	tick_interval_us_ = 60000000u / (bpm_ * STEPS_PER_QUARTER_NOTE);
	current_step_interval_us_ = compute_next_step_interval_us();
}

void SequencerEngine::update_swing_from_pot1(bool force_apply) {
	if (!shift_active_) {
		return;
	}
	if (!force_apply && !pot_multi_function_.get_changed(POT_FUNCTION_ID_SWING)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(POT_FUNCTION_ID_SWING);
	swing_amount_ = (static_cast<float>(pot_value) / 255.0f) * SWING_MAX;
	current_step_interval_us_ = compute_next_step_interval_us();
}

void SequencerEngine::update_range_or_quantization_from_pot2(bool force_apply) {
	const uint8_t function_id = shift_active_ ? POT_FUNCTION_ID_QUANTIZATION : POT_FUNCTION_ID_RANGE;
	if (!force_apply && !pot_multi_function_.get_changed(function_id)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(function_id);

	if (shift_active_) {
		const uint8_t mode_index = static_cast<uint8_t>((static_cast<uint32_t>(pot_value) * QUANTIZATION_MODE_COUNT) / 256u);
		quantization_mode_ = static_cast<QuantizationMode>(mode_index);
		return;
	}

	const uint8_t span = static_cast<uint8_t>(RANGE_OCTAVES_MAX - RANGE_OCTAVES_MIN + 1);
	const uint8_t mapped_range = static_cast<uint8_t>(
		RANGE_OCTAVES_MIN + ((static_cast<uint32_t>(pot_value) * span) / 256u)
	);
	range_octaves_ = mapped_range;
}

void SequencerEngine::update_randomness_or_length_from_pot3(bool force_apply) {
	const uint8_t function_id = shift_active_ ? POT_FUNCTION_ID_LENGTH : POT_FUNCTION_ID_RANDOMNESS;
	if (!force_apply && !pot_multi_function_.get_changed(function_id)) {
		return;
	}
	const uint8_t pot_value = pot_multi_function_.get_value(function_id);

	if (shift_active_) {
		const uint8_t new_length = static_cast<uint8_t>(1 + ((static_cast<uint32_t>(pot_value) * 63u) / 255u));
		sequence_a_.length = new_length;
		if (sequence_a_.position >= sequence_a_.length) {
			sequence_a_.position = 0;
		}
		return;
	}

	randomness_pot_value_ = pot_value;
	mutation_probability_ = static_cast<float>(randomness_pot_value_) / 255.0f;
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

void SequencerEngine::reset_gate_history() {
	gate_history_fifo_.fill(0);
	refresh_gate_history_view();
	leds_.off_all();
}

void SequencerEngine::push_gate_history(bool gate_high) {
	for (uint8_t i = 0; i < (brain::ui::NO_OF_LEDS - 1); ++i) {
		gate_history_fifo_[i] = gate_history_fifo_[i + 1];
	}
	gate_history_fifo_[brain::ui::NO_OF_LEDS - 1] = gate_high ? 1 : 0;
	refresh_gate_history_view();
	leds_.set_from_mask(gate_history_mask());
}

void SequencerEngine::refresh_gate_history_view() {
	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		gate_history_text_[i] = gate_history_fifo_[i] ? '1' : '0';
	}
	gate_history_text_[brain::ui::NO_OF_LEDS] = '\0';
}

uint8_t SequencerEngine::gate_history_mask() const {
	uint8_t mask = 0;
	for (uint8_t i = 0; i < brain::ui::NO_OF_LEDS; ++i) {
		if (gate_history_fifo_[i] != 0) {
			mask |= static_cast<uint8_t>(1u << i);
		}
	}
	return mask;
}

void SequencerEngine::tick(uint64_t now_us) {
	const uint8_t step_index = sequence_a_.position;
	apply_mutation_for_step(step_index);
	const Step& step_a = sequence_a_.steps[step_index];
	const Step& step_b = sequence_b_steps_[step_index];
	const float scaled_voltage_a = apply_pitch_range(step_a.voltage);
	const float scaled_voltage_b = apply_pitch_range(step_b.voltage);
	const float output_voltage_a = quantize_voltage(scaled_voltage_a);
	const float output_voltage_b = quantize_voltage(scaled_voltage_b);

	if (step_a.gate) {
		dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, output_voltage_a);
		dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, output_voltage_b);
		last_raw_voltage_ = scaled_voltage_a;
		last_quantized_voltage_ = output_voltage_a;
		gate_.set(true);
		gate_active_ = true;
		gate_off_time_us_ = now_us + GATE_PULSE_US;
	} else {
		gate_.set(false);
		gate_active_ = false;
	}
	push_gate_history(step_a.gate);

	if ((tick_counter_ % STEPS_PER_QUARTER_NOTE) == 0) {
		button_led_.blink_duration(BUTTON_LED_BLINK_MS, BUTTON_LED_BLINK_INTERVAL_MS);
	}

	tick_counter_++;
	sequence_a_.position = static_cast<uint8_t>((step_index + 1) % sequence_a_.length);
}

void SequencerEngine::reset_transport() {
	playing_ = false;
	sequence_a_.position = 0;
	next_tick_due_us_ = 0;
	gate_off_time_us_ = 0;
	gate_active_ = false;
	tick_counter_ = 0;
	current_step_interval_us_ = tick_interval_us_;
	gate_.set(false);
	reset_gate_history();
	button_led_.off();
}

uint32_t SequencerEngine::compute_next_step_interval_us() const {
	const uint32_t base = tick_interval_us_;
	if (swing_amount_ <= 0.0f) {
		return base;
	}

	const uint32_t swing_delta = static_cast<uint32_t>(static_cast<float>(base) * swing_amount_);
	if ((tick_counter_ & 1u) == 0u) {
		return (base > swing_delta) ? (base - swing_delta) : 1u;
	}
	return base + swing_delta;
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

float SequencerEngine::swing() const {
	return swing_amount_;
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

uint32_t SequencerEngine::base_interval_us() const {
	return tick_interval_us_;
}

uint32_t SequencerEngine::current_interval_us() const {
	return current_step_interval_us_;
}

const char* SequencerEngine::gate_history() const {
	return gate_history_text_;
}
