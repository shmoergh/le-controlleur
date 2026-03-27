#include "sequencer-engine.h"

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
	randomness_pot_value_(0),
	mutation_probability_(0.0f),
	previous_randomness_pot_value_(255),
	previous_length_(0),
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
	update_randomness_or_length_from_pot3(true);
}

void SequencerEngine::on_button_b_release() {
	if (!initialized_ || !shift_active_) {
		return;
	}

	shift_active_ = false;
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

	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelA, step_a.voltage);
	dac_.set_voltage(brain::io::AudioCvOutChannel::kChannelB, step_b.voltage);

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
