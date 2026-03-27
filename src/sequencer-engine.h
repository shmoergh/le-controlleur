#ifndef SEQUENCER_ENGINE_H
#define SEQUENCER_ENGINE_H

#include <array>
#include <cstdint>

#include "brain-io/audio-cv-out.h"
#include "brain-io/pulse.h"
#include "brain-ui/button-led.h"
#include "brain-ui/leds.h"
#include "brain-ui/pot-multi-function.h"
#include "brain-ui/pots.h"

struct Step {
	uint16_t pitch_q8;
	bool gate;
};

struct Sequence {
	static constexpr uint8_t kMaxSteps = 64;
	std::array<Step, kMaxSteps> steps;
	uint8_t length;
	uint8_t position;
};

enum class QuantizationMode : uint8_t {
	kUnquantized = 0,
	kChromatic = 1,
	kMajor = 2,
	kMinor = 3,
	kPentatonic = 4,
	kExtra = 5
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
	bool external_sync_enabled() const;
	float swing() const;
	float randomness() const;
	uint8_t sequence_length() const;
	uint8_t range_octaves() const;
	const char* quantization_mode_name() const;
	float last_raw_voltage() const;
	float last_quantized_voltage() const;
	uint32_t base_interval_us() const;
	uint32_t current_interval_us() const;
	const char* gate_history() const;

private:
	static constexpr uint8_t POT_INDEX_BPM = 0;
	static constexpr uint8_t POT_INDEX_RANGE_OR_QUANTIZATION = 1;
	static constexpr uint8_t POT_INDEX_RANDOMNESS_OR_LENGTH = 2;
	static constexpr uint8_t POT_FUNCTION_ID_NONE = 255;
	static constexpr uint8_t POT_FUNCTION_ID_BPM = 1;
	static constexpr uint8_t POT_FUNCTION_ID_SWING = 2;
	static constexpr uint8_t POT_FUNCTION_ID_RANGE = 3;
	static constexpr uint8_t POT_FUNCTION_ID_RANDOMNESS = 4;
	static constexpr uint8_t POT_FUNCTION_ID_QUANTIZATION = 5;
	static constexpr uint8_t POT_FUNCTION_ID_LENGTH = 6;
	static constexpr uint8_t POT_FUNCTION_PICKUP_HYSTERESIS = 1;
	static constexpr uint8_t NUM_POTS = 3;
	static constexpr uint16_t BPM_MIN = 60;
	static constexpr uint16_t BPM_MAX = 240;
	static constexpr uint8_t SEQUENCE_LENGTH_MIN = 2;
	static constexpr uint8_t SEQUENCE_LENGTH_MAX = 32;
	static constexpr uint8_t RANGE_OCTAVES_MIN = 0;
	static constexpr uint8_t RANGE_OCTAVES_MAX = 6;
	static constexpr uint8_t QUANTIZATION_MODE_COUNT = 6;
	static constexpr uint8_t STEPS_PER_QUARTER_NOTE = 4;
	static constexpr uint32_t GATE_PULSE_US = 20000;
	static constexpr uint32_t BUTTON_LED_BLINK_MS = 80;
	static constexpr uint32_t BUTTON_LED_BLINK_INTERVAL_MS = 40;
	static constexpr uint8_t POT_LED_SOFT_BRIGHTNESS = 48;
	static constexpr uint16_t PITCH_Q8_PER_SEMITONE = 256;
	static constexpr uint16_t SEMITONES_PER_OCTAVE = 12;
	static constexpr uint16_t SOURCE_RANGE_OCTAVES = 5;
	static constexpr uint16_t RANDOM_MAX_Q8 = SOURCE_RANGE_OCTAVES * SEMITONES_PER_OCTAVE * PITCH_Q8_PER_SEMITONE;
	static constexpr uint64_t POT_LED_OVERLAY_HOLD_US = 1000ULL * 1000ULL;
	static constexpr uint8_t POT_LED_ACTIVITY_RAW_THRESHOLD = 3;

	Sequence sequence_a_;
	std::array<Step, Sequence::kMaxSteps> sequence_b_steps_;
	brain::ui::Pots pots_;
	brain::ui::PotMultiFunction pot_multi_function_;
	brain::io::AudioCvOut dac_;
	brain::io::Pulse gate_;
	brain::ui::Leds leds_;
	brain::ui::ButtonLed button_led_;

	bool initialized_;
	bool playing_;
	bool gate_active_;
	uint16_t bpm_;
	uint32_t tick_interval_us_;
	uint64_t next_tick_due_us_;
	uint64_t gate_off_time_us_;
	uint32_t tick_counter_;
	uint32_t current_step_interval_us_;
	bool shift_active_;
	bool last_shift_context_;
	uint8_t swing_pot_value_;
	uint8_t range_octaves_;
	QuantizationMode quantization_mode_;
	uint8_t randomness_pot_value_;
	uint8_t mutation_threshold_;
	bool external_sync_enabled_;
	bool last_pulse_in_high_;
	uint64_t last_external_tick_us_;
	float last_raw_voltage_;
	float last_quantized_voltage_;
	bool pot_led_overlay_active_;
	uint64_t pot_led_overlay_last_change_us_;
	std::array<uint8_t, NUM_POTS> last_pot_raw_values_;
	std::array<uint8_t, brain::ui::NO_OF_LEDS> gate_history_fifo_;
	char gate_history_text_[brain::ui::NO_OF_LEDS + 1];
	uint32_t rng_state_a_;
	uint32_t rng_state_b_;

	void init_sequence();
	void init_io();
	void init_pot_functions();
	void set_active_pot_functions(uint8_t pot_0_function, uint8_t pot_1_function, uint8_t pot_2_function);
	void update_pot_mappings(bool force_apply = false);
	void update_bpm_from_pot(bool force_apply = false);
	void update_swing_from_pot1(bool force_apply = false);
	void update_range_or_quantization_from_pot2(bool force_apply = false);
	void update_randomness_or_length_from_pot3(bool force_apply = false);
	uint8_t map_sequence_length_with_soft_snap(uint8_t pot_value) const;
	bool is_power_of_two_sequence_length(uint8_t length) const;
	uint8_t sequence_length_led_count(uint8_t length) const;
	void apply_mutation_for_step(uint8_t step_index);
	void update_pot_led_overlay(uint64_t now_us);
	void show_active_pot_overlay(uint8_t pot_index);
	uint8_t active_pot_percent_255(uint8_t pot_index) const;
	uint8_t active_pot_led_mask(uint8_t pot_index) const;
	uint8_t percent_to_led_mask(uint8_t percent_255) const;
	void reset_gate_history();
	void push_gate_history(bool gate_high);
	void refresh_gate_history_view();
	uint8_t gate_history_mask() const;
	void tick(uint64_t now_us);
	void reset_transport();
	uint16_t apply_pitch_range(uint16_t source_q8) const;
	uint16_t quantize_pitch(uint16_t pitch_q8) const;
	uint32_t compute_next_step_interval_us() const;
	static const char* quantization_mode_to_string(QuantizationMode mode);
	static uint32_t next_random(uint32_t& state);
	static uint8_t random_u8(uint32_t& state);
	static float pitch_q8_to_voltage(uint16_t pitch_q8);
};

#endif
