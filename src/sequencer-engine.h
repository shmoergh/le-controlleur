#ifndef SEQUENCER_ENGINE_H
#define SEQUENCER_ENGINE_H

#include <array>
#include <cstdint>

#include "brain-io/audio-cv-out.h"
#include "brain-io/pulse.h"
#include "brain-ui/button-led.h"
#include "brain-ui/pots.h"

struct Step {
	float voltage;
	bool gate;
};

struct Sequence {
	static constexpr uint8_t kMaxSteps = 64;
	std::array<Step, kMaxSteps> steps;
	uint8_t length;
	uint8_t position;
};

class SequencerEngine {
public:
	SequencerEngine();
	void update();
	void on_mode_enter();
	void on_mode_exit();
	void on_button_a_short_press();
	void on_button_b_press();
	void on_button_b_release();
	uint16_t tempo_bpm() const;
	float randomness() const;
	uint8_t sequence_length() const;

private:
	static constexpr uint8_t POT_INDEX_BPM = 0;
	static constexpr uint8_t POT_INDEX_RANDOMNESS_OR_LENGTH = 2;
	static constexpr uint16_t BPM_MIN = 60;
	static constexpr uint16_t BPM_MAX = 240;
	static constexpr uint8_t STEPS_PER_QUARTER_NOTE = 4;
	static constexpr uint32_t GATE_PULSE_US = 20000;
	static constexpr uint32_t BUTTON_LED_BLINK_MS = 80;
	static constexpr uint32_t BUTTON_LED_BLINK_INTERVAL_MS = 40;
	static constexpr float RANDOM_VOLTAGE_MAX = 5.0f;

	Sequence sequence_a_;
	std::array<Step, Sequence::kMaxSteps> sequence_b_steps_;
	brain::ui::Pots pots_;
	brain::io::AudioCvOut dac_;
	brain::io::Pulse gate_;
	brain::ui::ButtonLed button_led_;

	bool initialized_;
	bool playing_;
	bool gate_active_;
	uint16_t bpm_;
	uint32_t tick_interval_us_;
	uint64_t last_tick_time_us_;
	uint64_t gate_off_time_us_;
	uint32_t tick_counter_;
	bool shift_active_;
	uint8_t randomness_pot_value_;
	float mutation_probability_;
	uint8_t previous_randomness_pot_value_;
	uint8_t previous_length_;
	uint32_t rng_state_a_;
	uint32_t rng_state_b_;

	void init_sequence();
	void init_io();
	void update_bpm_from_pot(bool force_log = false);
	void update_randomness_or_length_from_pot3(bool force_log = false);
	void apply_mutation_for_step(uint8_t step_index);
	void tick(uint64_t now_us);
	void reset_transport();
	static uint32_t next_random(uint32_t& state);
	static float random_unit(uint32_t& state);
};

#endif
